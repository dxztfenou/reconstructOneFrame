#include "reconstruction/PointCloudReconstructor.h"

#include <cuda_runtime.h>
#include <math_constants.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

namespace {

constexpr float kFloatEpsilon = 1.0e-6F;

using SteadyClock = std::chrono::steady_clock;

double elapsedMilliseconds(SteadyClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
}

struct DeviceBuffer {
    void* ptr = nullptr;

    DeviceBuffer() = default;
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    ~DeviceBuffer()
    {
        if (ptr != nullptr) {
            cudaFree(ptr);
        }
    }

    template <typename T>
    T* as() noexcept
    {
        return static_cast<T*>(ptr);
    }

    template <typename T>
    const T* as() const noexcept
    {
        return static_cast<const T*>(ptr);
    }
};

struct HostFloat3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct DisparityWindow {
    bool enabled = false;
    float minDisparity = 0.0F;
    float maxDisparity = 0.0F;
};

struct RemapCalibration {
    float k[9] = {};
    float dist[8] = {};
    float rInv[9] = {};
    float p[12] = {};
    int distCount = 0;
};

struct ColorCorrection {
    float matrix[12] = {};
    float gamma = 1.0F;
};

Status cudaStatus(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return {};
    }
    return {StatusCode::CudaKernelFailed, "PointCloudReconstructorCuda", std::string(operation) + ": " + cudaGetErrorString(error)};
}

bool isValidPoint(const HostFloat3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           !(point.x == 0.0F && point.y == 0.0F && point.z == 0.0F);
}

DisparityWindow makeDisparityWindow(const CalibrationModel& calibration,
                                    const ReconsConfig& config,
                                    int width)
{
    DisparityWindow window;
    if (!config.disparityWindowEnabled || calibration.qMatrix.size() != 16 || width <= 1) {
        return window;
    }

    const float q23 = static_cast<float>(calibration.qMatrix[11]);
    const float q32 = static_cast<float>(calibration.qMatrix[14]);
    const float q33 = static_cast<float>(calibration.qMatrix[15]);
    if (!std::isfinite(q23) || !std::isfinite(q32) || !std::isfinite(q33) ||
        std::fabs(q32) <= kFloatEpsilon) {
        return window;
    }

    const float minZ = static_cast<float>(std::min(config.disparityWindowMinZ, config.disparityWindowMaxZ));
    const float maxZ = static_cast<float>(std::max(config.disparityWindowMinZ, config.disparityWindowMaxZ));
    const float d0 = (q23 / minZ - q33) / q32;
    const float d1 = (q23 / maxZ - q33) / q32;
    if (!std::isfinite(d0) || !std::isfinite(d1)) {
        return window;
    }

    const float limit = static_cast<float>(width - 1);
    const float margin = static_cast<float>(std::max(0.0, config.disparityWindowMargin));
    window.minDisparity = std::max(std::min(d0, d1) - margin, -limit);
    window.maxDisparity = std::min(std::max(d0, d1) + margin, limit);
    window.enabled = window.minDisparity <= window.maxDisparity;
    return window;
}

bool hasRectificationCalibration(const CalibrationModel& calibration)
{
    return calibration.leftIntrinsics.size() == 9 &&
           calibration.rightIntrinsics.size() == 9 &&
           !calibration.leftDistortion.empty() &&
           !calibration.rightDistortion.empty() &&
           calibration.rectificationLeft.size() == 9 &&
           calibration.rectificationRight.size() == 9 &&
           calibration.projectionLeft.size() == 12 &&
           calibration.projectionRight.size() == 12;
}

std::array<double, 9> invert3x3(const std::vector<double>& matrix)
{
    const double a = matrix[0];
    const double b = matrix[1];
    const double c = matrix[2];
    const double d = matrix[3];
    const double e = matrix[4];
    const double f = matrix[5];
    const double g = matrix[6];
    const double h = matrix[7];
    const double i = matrix[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) <= 1.0e-12) {
        return {1.0, 0.0, 0.0,
                0.0, 1.0, 0.0,
                0.0, 0.0, 1.0};
    }
    const double inv = 1.0 / det;
    return {
        (e * i - f * h) * inv,
        (c * h - b * i) * inv,
        (b * f - c * e) * inv,
        (f * g - d * i) * inv,
        (a * i - c * g) * inv,
        (c * d - a * f) * inv,
        (d * h - e * g) * inv,
        (b * g - a * h) * inv,
        (a * e - b * d) * inv
    };
}

RemapCalibration makeRemapCalibration(const std::vector<double>& intrinsics,
                                      const std::vector<double>& distortion,
                                      const std::vector<double>& rectification,
                                      const std::vector<double>& projection)
{
    RemapCalibration remap;
    for (std::size_t i = 0; i < intrinsics.size() && i < 9; ++i) {
        remap.k[i] = static_cast<float>(intrinsics[i]);
    }
    for (std::size_t i = 0; i < distortion.size() && i < 8; ++i) {
        remap.dist[i] = static_cast<float>(distortion[i]);
    }
    remap.distCount = static_cast<int>(std::min<std::size_t>(distortion.size(), 8));
    const std::array<double, 9> inverse = invert3x3(rectification);
    for (std::size_t i = 0; i < inverse.size(); ++i) {
        remap.rInv[i] = static_cast<float>(inverse[i]);
    }
    for (std::size_t i = 0; i < projection.size() && i < 12; ++i) {
        remap.p[i] = static_cast<float>(projection[i]);
    }
    return remap;
}

__device__ float sampleBilinear(const float* image, int width, int height, float x, float y)
{
    if (!isfinite(x) || !isfinite(y) || x < 0.0F || y < 0.0F ||
        x > static_cast<float>(width - 1) || y > static_cast<float>(height - 1)) {
        return CUDART_NAN_F;
    }
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    const int x1 = min(x0 + 1, width - 1);
    const int y1 = min(y0 + 1, height - 1);
    const float dx = x - static_cast<float>(x0);
    const float dy = y - static_cast<float>(y0);
    const float v00 = image[y0 * width + x0];
    const float v01 = image[y0 * width + x1];
    const float v10 = image[y1 * width + x0];
    const float v11 = image[y1 * width + x1];
    if (!isfinite(v00) || !isfinite(v01) || !isfinite(v10) || !isfinite(v11)) {
        return CUDART_NAN_F;
    }
    const float top = v00 * (1.0F - dx) + v01 * dx;
    const float bottom = v10 * (1.0F - dx) + v11 * dx;
    return top * (1.0F - dy) + bottom * dy;
}

__device__ float sampleBilinearColor(const unsigned char* image,
                                     int width,
                                     int height,
                                     int channel,
                                     float x,
                                     float y)
{
    if (!isfinite(x) || !isfinite(y) || x < 0.0F || y < 0.0F ||
        x > static_cast<float>(width - 1) || y > static_cast<float>(height - 1)) {
        return 0.0F;
    }
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    const int x1 = min(x0 + 1, width - 1);
    const int y1 = min(y0 + 1, height - 1);
    const float dx = x - static_cast<float>(x0);
    const float dy = y - static_cast<float>(y0);
    const float v00 = static_cast<float>(image[(y0 * width + x0) * 3 + channel]);
    const float v01 = static_cast<float>(image[(y0 * width + x1) * 3 + channel]);
    const float v10 = static_cast<float>(image[(y1 * width + x0) * 3 + channel]);
    const float v11 = static_cast<float>(image[(y1 * width + x1) * 3 + channel]);
    const float top = v00 * (1.0F - dx) + v01 * dx;
    const float bottom = v10 * (1.0F - dx) + v11 * dx;
    return top * (1.0F - dy) + bottom * dy;
}

__device__ bool rectifiedToRawPixel(int u,
                                    int v,
                                    RemapCalibration calibration,
                                    float& srcX,
                                    float& srcY)
{
    const float fxNew = calibration.p[0];
    const float fyNew = calibration.p[5];
    const float cxNew = calibration.p[2];
    const float cyNew = calibration.p[6];
    if (fabsf(fxNew) <= 1.0e-6F || fabsf(fyNew) <= 1.0e-6F) {
        return false;
    }

    const float xr = (static_cast<float>(u) - cxNew) / fxNew;
    const float yr = (static_cast<float>(v) - cyNew) / fyNew;
    const float rz = calibration.rInv[6] * xr + calibration.rInv[7] * yr + calibration.rInv[8];
    if (fabsf(rz) <= 1.0e-6F) {
        return false;
    }
    const float x = (calibration.rInv[0] * xr + calibration.rInv[1] * yr + calibration.rInv[2]) / rz;
    const float y = (calibration.rInv[3] * xr + calibration.rInv[4] * yr + calibration.rInv[5]) / rz;

    const float k1 = calibration.distCount > 0 ? calibration.dist[0] : 0.0F;
    const float k2 = calibration.distCount > 1 ? calibration.dist[1] : 0.0F;
    const float p1 = calibration.distCount > 2 ? calibration.dist[2] : 0.0F;
    const float p2 = calibration.distCount > 3 ? calibration.dist[3] : 0.0F;
    const float k3 = calibration.distCount > 4 ? calibration.dist[4] : 0.0F;
    const float r2 = x * x + y * y;
    const float r4 = r2 * r2;
    const float r6 = r4 * r2;
    const float radial = 1.0F + k1 * r2 + k2 * r4 + k3 * r6;
    const float xDist = x * radial + 2.0F * p1 * x * y + p2 * (r2 + 2.0F * x * x);
    const float yDist = y * radial + p1 * (r2 + 2.0F * y * y) + 2.0F * p2 * x * y;
    srcX = calibration.k[0] * xDist + calibration.k[2];
    srcY = calibration.k[4] * yDist + calibration.k[5];
    return isfinite(srcX) && isfinite(srcY);
}

__global__ void remapPhaseKernel(const float* input,
                                 float* output,
                                 int width,
                                 int height,
                                 RemapCalibration calibration)
{
    const int u = blockIdx.x * blockDim.x + threadIdx.x;
    const int v = blockIdx.y * blockDim.y + threadIdx.y;
    if (u >= width || v >= height) {
        return;
    }
    const int idx = v * width + u;
    output[idx] = CUDART_NAN_F;

    float srcX = 0.0F;
    float srcY = 0.0F;
    if (!rectifiedToRawPixel(u, v, calibration, srcX, srcY)) {
        return;
    }
    output[idx] = sampleBilinear(input, width, height, srcX, srcY);
}

__global__ void remapCorrectColorKernel(const unsigned char* input,
                                        unsigned char* output,
                                        int width,
                                        int height,
                                        RemapCalibration calibration,
                                        ColorCorrection correction,
                                        int useRectification)
{
    const int u = blockIdx.x * blockDim.x + threadIdx.x;
    const int v = blockIdx.y * blockDim.y + threadIdx.y;
    if (u >= width || v >= height) {
        return;
    }
    const int idx = v * width + u;
    output[idx * 3 + 0] = 0;
    output[idx * 3 + 1] = 0;
    output[idx * 3 + 2] = 0;

    float srcX = static_cast<float>(u);
    float srcY = static_cast<float>(v);
    if (useRectification != 0 && !rectifiedToRawPixel(u, v, calibration, srcX, srcY)) {
        return;
    }
    const float b = sampleBilinearColor(input, width, height, 0, srcX, srcY);
    const float g = sampleBilinearColor(input, width, height, 1, srcX, srcY);
    const float r = sampleBilinearColor(input, width, height, 2, srcX, srcY);
    const float features[4] = {b, g, r, 1.0F};
    for (int channel = 0; channel < 3; ++channel) {
        float corrected = 0.0F;
        for (int feature = 0; feature < 4; ++feature) {
            corrected += correction.matrix[channel * 4 + feature] * features[feature];
        }
        corrected = fminf(255.0F, fmaxf(0.0F, corrected));
        const int lutIndex = max(0, min(255, static_cast<int>(corrected)));
        const float gammaCorrected =
            powf(static_cast<float>(lutIndex) / 255.0F, correction.gamma) * 255.0F;
        output[idx * 3 + channel] =
            static_cast<unsigned char>(max(0, min(255, __float2int_rn(gammaCorrected))));
    }
}

__device__ bool leftRightConsistent(const float* left,
                                    const float* right,
                                    int row,
                                    int leftX,
                                    int rightX,
                                    int width,
                                    float threshold,
                                    float minDisparity,
                                    float maxDisparity,
                                    int useWindow,
                                    float tolerance,
                                    int useSubpixel)
{
    const float rightPhase = right[row * width + rightX];
    int begin = 0;
    int end = width - 1;
    if (useWindow != 0) {
        begin = max(0, static_cast<int>(ceilf(static_cast<float>(rightX) + minDisparity)));
        end = min(width - 1, static_cast<int>(floorf(static_cast<float>(rightX) + maxDisparity)));
        if (begin > end) {
            return false;
        }
    }

    float bestCost = FLT_MAX;
    int bestX = -1;
    for (int x = begin; x <= end; ++x) {
        const float leftPhase = left[row * width + x];
        if (!isfinite(leftPhase)) {
            continue;
        }
        const float cost = fabsf(leftPhase - rightPhase);
        if (cost < bestCost) {
            bestCost = cost;
            bestX = x;
        }
    }
    if (bestX < 0 || bestCost >= threshold) {
        return false;
    }

    float bestXFloat = static_cast<float>(bestX);
    const float bestLeftPhase = left[row * width + bestX];
    if (useSubpixel != 0 && isfinite(bestLeftPhase)) {
        if (rightPhase > bestLeftPhase && bestX + 1 < width) {
            const float nextPhase = left[row * width + bestX + 1];
            if (isfinite(nextPhase) && nextPhase > bestLeftPhase && rightPhase <= nextPhase) {
                bestXFloat = static_cast<float>(bestX) +
                             (rightPhase - bestLeftPhase) / (nextPhase - bestLeftPhase);
            }
        } else if (rightPhase < bestLeftPhase && bestX - 1 >= 0) {
            const float previousPhase = left[row * width + bestX - 1];
            if (isfinite(previousPhase) && previousPhase < bestLeftPhase && rightPhase >= previousPhase) {
                bestXFloat = static_cast<float>(bestX) -
                             (bestLeftPhase - rightPhase) / (bestLeftPhase - previousPhase);
            }
        }
    }
    return fabsf(bestXFloat - static_cast<float>(leftX)) <= tolerance;
}

__device__ bool rightPhaseMonotonicSupported(const float* rightPhaseRow,
                                             int width,
                                             int bestX,
                                             float leftPhase,
                                             float bestPhase,
                                             int radius,
                                             float minimumSlope)
{
    const int checkedRadius = max(1, min(radius, 5));
    const float slope = fmaxf(0.0F, minimumSlope);
    if (!isfinite(leftPhase) || !isfinite(bestPhase)) {
        return false;
    }
    if (fabsf(leftPhase - bestPhase) <= slope) {
        bool hasLowerNeighbor = false;
        bool hasHigherNeighbor = false;
        for (int step = 1; step <= checkedRadius; ++step) {
            const int lowerX = bestX - step;
            if (lowerX >= 0) {
                const float lower = rightPhaseRow[lowerX];
                hasLowerNeighbor = hasLowerNeighbor ||
                                   (isfinite(lower) && lower < bestPhase - slope);
            }
            const int higherX = bestX + step;
            if (higherX < width) {
                const float higher = rightPhaseRow[higherX];
                hasHigherNeighbor = hasHigherNeighbor ||
                                    (isfinite(higher) && higher > bestPhase + slope);
            }
        }
        if (bestX == 0) {
            return hasHigherNeighbor;
        }
        if (bestX == width - 1) {
            return hasLowerNeighbor;
        }
        return hasLowerNeighbor && hasHigherNeighbor;
    }
    if (leftPhase > bestPhase) {
        for (int step = 1; step <= checkedRadius; ++step) {
            const int x = bestX + step;
            if (x >= width) {
                break;
            }
            const float neighbor = rightPhaseRow[x];
            if (isfinite(neighbor) &&
                neighbor > bestPhase + slope &&
                neighbor >= leftPhase - slope) {
                return true;
            }
        }
    } else {
        for (int step = 1; step <= checkedRadius; ++step) {
            const int x = bestX - step;
            if (x < 0) {
                break;
            }
            const float neighbor = rightPhaseRow[x];
            if (isfinite(neighbor) &&
                neighbor < bestPhase - slope &&
                neighbor <= leftPhase + slope) {
                return true;
            }
        }
    }
    return false;
}

__global__ void computeDisparityKernel(const float* left,
                                       const float* right,
                                       float* disparity,
                                       float* scores,
                                       int* candidateCounts,
                                       int width,
                                       int height,
                                       float threshold,
                                       float minDisparity,
                                       float maxDisparity,
                                       int useWindow,
                                       int useUniqueness,
                                       int maxCandidateCount,
                                       float minSecondBestGap,
                                       int useLeftRightConsistency,
                                       float leftRightTolerance,
                                       int useRightPhaseMonotonicity,
                                       int rightPhaseMonotonicRadius,
                                       float rightPhaseMinimumSlope,
                                       int useSubpixel,
                                       unsigned int* rejectionCounts)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }

    const int idx = y * width + x;
    disparity[idx] = 0.0F;
    scores[idx] = threshold;
    candidateCounts[idx] = 0;
    const float leftPhase = left[idx];
    if (!isfinite(leftPhase) || fabsf(leftPhase) < 0.001F) {
        return;
    }

    int begin = 0;
    int end = width - 1;
    if (useWindow != 0) {
        begin = max(0, static_cast<int>(ceilf(static_cast<float>(x) - maxDisparity)));
        end = min(width - 1, static_cast<int>(floorf(static_cast<float>(x) - minDisparity)));
        if (begin > end) {
            return;
        }
    }

    float bestCost = FLT_MAX;
    float secondBestCost = FLT_MAX;
    int bestX = -1;
    int nearCandidateCount = 0;
    for (int rx = begin; rx <= end; ++rx) {
        const float rightPhase = right[y * width + rx];
        if (!isfinite(rightPhase) || x - rx > width - 1) {
            continue;
        }
        const float cost = fabsf(leftPhase - rightPhase);
        if (cost < threshold) {
            ++nearCandidateCount;
        }
        if (cost < bestCost) {
            secondBestCost = bestCost;
            bestCost = cost;
            bestX = rx;
        } else if (cost < secondBestCost) {
            secondBestCost = cost;
        }
    }

    candidateCounts[idx] = nearCandidateCount;
    bool matchOk = bestX >= 0 && bestCost < threshold;
    if (matchOk && useUniqueness != 0) {
        if (nearCandidateCount > max(1, maxCandidateCount)) {
            matchOk = false;
        } else if (secondBestCost < FLT_MAX && secondBestCost - bestCost < minSecondBestGap) {
            matchOk = false;
        }
    }
    if (matchOk && useLeftRightConsistency != 0) {
        if (!leftRightConsistent(left,
                                 right,
                                 y,
                                 x,
                                 bestX,
                                 width,
                                 threshold,
                                 minDisparity,
                                 maxDisparity,
                                 useWindow,
                                 leftRightTolerance,
                                 useSubpixel)) {
            matchOk = false;
            if (rejectionCounts != nullptr) {
                atomicAdd(&rejectionCounts[0], 1U);
            }
        }
    }
    if (matchOk && useRightPhaseMonotonicity != 0) {
        const float bestRightPhase = right[y * width + bestX];
        if (!rightPhaseMonotonicSupported(&right[y * width],
                                          width,
                                          bestX,
                                          leftPhase,
                                          bestRightPhase,
                                          rightPhaseMonotonicRadius,
                                          rightPhaseMinimumSlope)) {
            matchOk = false;
            if (rejectionCounts != nullptr) {
                atomicAdd(&rejectionCounts[1], 1U);
            }
        }
    }
    if (!matchOk) {
        return;
    }

    float bestXFloat = static_cast<float>(bestX);
    const float bestRightPhase = right[y * width + bestX];
    if (useSubpixel != 0) {
        if (leftPhase > bestRightPhase && bestX + 1 < width) {
            const float nextPhase = right[y * width + bestX + 1];
            if (isfinite(nextPhase) && nextPhase > bestRightPhase && leftPhase <= nextPhase) {
                bestXFloat = static_cast<float>(bestX) + (leftPhase - bestRightPhase) / (nextPhase - bestRightPhase);
            }
        } else if (leftPhase < bestRightPhase && bestX - 1 >= 0) {
            const float prevPhase = right[y * width + bestX - 1];
            if (isfinite(prevPhase) && prevPhase < bestRightPhase && leftPhase >= prevPhase) {
                bestXFloat = static_cast<float>(bestX) - (bestRightPhase - leftPhase) / (bestRightPhase - prevPhase);
            }
        }
    }

    disparity[idx] = static_cast<float>(x) - bestXFloat;
    scores[idx] = bestCost;
}

__global__ void filterDisparityLocalConsistencyKernel(const float* input,
                                                      float* output,
                                                      int width,
                                                      int height,
                                                      float threshold,
                                                      int radius,
                                                      int minSupport)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const int idx = y * width + x;
    output[idx] = 0.0F;
    const float center = input[idx];
    if (center == 0.0F || !isfinite(center)) {
        return;
    }

    int support = 0;
    const int r = max(1, min(radius, 5));
    for (int dy = -r; dy <= r; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int xx = x + dx;
            if (xx < 0 || xx >= width) {
                continue;
            }
            const float neighbor = input[yy * width + xx];
            if (neighbor != 0.0F && isfinite(neighbor) && fabsf(neighbor - center) <= threshold) {
                ++support;
            }
        }
    }
    if (support >= minSupport) {
        output[idx] = center;
    }
}

__global__ void reprojectKernel(const float* disparity,
                                float3* points,
                                const float* q,
                                int width,
                                int height,
                                float minZ,
                                float maxZ)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const int idx = y * width + x;
    points[idx] = make_float3(0.0F, 0.0F, 0.0F);
    const float d = disparity[idx];
    if (d == 0.0F || !isfinite(d)) {
        return;
    }

    const float q03 = q[3];
    const float q13 = q[7];
    const float q23 = q[11];
    const float q32 = q[14];
    const float q33 = q[15];
    const float w = q32 * d + q33;
    if (fabsf(w) <= 1.0e-6F || fabsf(q23) <= 1.0e-6F) {
        return;
    }
    const float z = q23 / w;
    if (!isfinite(z) || z < minZ || z > maxZ) {
        return;
    }
    const float px = (static_cast<float>(x) + q03) * z / q23;
    const float py = (static_cast<float>(y) + q13) * z / q23;
    if (!isfinite(px) || !isfinite(py)) {
        return;
    }
    points[idx] = make_float3(px, py, z);
}

__device__ bool validDevicePoint(float3 point, float minZ, float maxZ)
{
    return !(point.x == 0.0F && point.y == 0.0F && point.z == 0.0F) &&
           isfinite(point.x) && isfinite(point.y) && isfinite(point.z) &&
           point.z >= minZ && point.z <= maxZ;
}

__device__ bool nonZeroDevicePoint(float3 point)
{
    return !(point.x == 0.0F && point.y == 0.0F && point.z == 0.0F);
}

__device__ bool validCountDevicePoint(float3 point)
{
    return nonZeroDevicePoint(point) &&
           isfinite(point.x) && isfinite(point.y) && isfinite(point.z);
}

__global__ void countPointStagesKernel(const float3* rawPoints,
                                       const float3* smoothedPoints,
                                       const float3* filteredPoints,
                                       unsigned int* counts,
                                       int pixelCount)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }
    if (validCountDevicePoint(rawPoints[idx])) {
        atomicAdd(&counts[0], 1U);
    }
    if (validCountDevicePoint(smoothedPoints[idx])) {
        atomicAdd(&counts[1], 1U);
    }
    if (validCountDevicePoint(filteredPoints[idx])) {
        atomicAdd(&counts[2], 1U);
    }
}

__device__ int reflectCoordinate(int coordinate, int size)
{
    if (coordinate < 0) {
        coordinate = -coordinate;
    }
    if (coordinate >= size) {
        coordinate = 2 * size - coordinate - 1;
    }
    return coordinate;
}

__global__ void gaussianBlurPointZKernel(const float3* input,
                                         float3* output,
                                         int width,
                                         int height)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    constexpr float weights[9] = {
        1.96519161e-05F, 2.39409349e-04F, 1.07295826e-03F,
        1.76900911e-03F, 1.07295826e-03F, 2.39409349e-04F,
        1.96519161e-05F, 2.39409349e-04F, 2.91660295e-03F
    };
    const int idx = y * width + x;
    float sumZ = 0.0F;
    float totalWeight = 0.0F;
    int wIdx = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx, ++wIdx) {
            const int xx = reflectCoordinate(x + dx, width);
            const int yy = reflectCoordinate(y + dy, height);
            const float3 point = input[yy * width + xx];
            const float weight = weights[wIdx];
            sumZ += point.z * weight;
            totalWeight += weight;
        }
    }
    output[idx] = make_float3(input[idx].x,
                              input[idx].y,
                              totalWeight > 0.0F ? sumZ / totalWeight : 0.0F);
}

__global__ void nonConnectedFilterKernel(const float3* input,
                                         float3* output,
                                         int width,
                                         int height,
                                         float distanceThreshold,
                                         int minNeighbors,
                                         int enableFilter)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const int idx = y * width + x;
    output[idx] = make_float3(0.0F, 0.0F, 0.0F);
    const float3 point = input[idx];
    if (!nonZeroDevicePoint(point)) {
        return;
    }
    if (enableFilter == 0) {
        output[idx] = point;
        return;
    }

    int neighbors = 0;
    int totalValidNeighbors = 0;
    float avgDistance = 0.0F;
    int closeNeighbors = 0;
    constexpr int innerRadius = 1;
    constexpr int outerRadius = 7;
    for (int dy = -outerRadius; dy <= outerRadius; ++dy) {
        for (int dx = -outerRadius; dx <= outerRadius; ++dx) {
            const int xx = x + dx;
            const int yy = y + dy;
            if (xx < 0 || xx >= width || yy < 0 || yy >= height) {
                continue;
            }
            const float3 neighbor = input[yy * width + xx];
            if (!nonZeroDevicePoint(neighbor)) {
                continue;
            }
            ++totalValidNeighbors;
            const float dist = sqrtf((point.x - neighbor.x) * (point.x - neighbor.x) +
                                     (point.y - neighbor.y) * (point.y - neighbor.y) +
                                     (point.z - neighbor.z) * (point.z - neighbor.z));
            if (abs(dx) <= innerRadius && abs(dy) <= innerRadius && dist < distanceThreshold) {
                ++neighbors;
            }
            if (dist < 1.0F) {
                avgDistance += dist;
                ++closeNeighbors;
            }
        }
    }
    if (closeNeighbors > 0) {
        avgDistance /= static_cast<float>(closeNeighbors);
    }
    if (neighbors >= minNeighbors &&
        totalValidNeighbors >= static_cast<int>(4 * outerRadius * outerRadius * 0.90F) &&
        avgDistance < 0.8F) {
        output[idx] = point;
    }
}

__device__ float3 subtractPoint(float3 a, float3 b)
{
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ float3 crossPoint(float3 a, float3 b)
{
    return make_float3(a.y * b.z - a.z * b.y,
                       a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x);
}

__global__ void normalKernel(const float3* points,
                             float3* normals,
                             int width,
                             int height,
                             float minZ,
                             float maxZ)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const int idx = y * width + x;
    normals[idx] = make_float3(0.0F, 0.0F, 0.0F);
    int radius = 6;
    if (x < radius || x >= width - radius || y < radius || y >= height - radius) {
        radius = 2;
    }
    if (x < radius || x >= width - radius || y < radius || y >= height - radius) {
        return;
    }
    const float3 center = points[idx];
    const float3 px0 = points[y * width + (x - radius)];
    const float3 px1 = points[y * width + (x + radius)];
    const float3 py0 = points[(y - radius) * width + x];
    const float3 py1 = points[(y + radius) * width + x];
    if (!validDevicePoint(center, minZ, maxZ) ||
        !validDevicePoint(px0, minZ, maxZ) ||
        !validDevicePoint(px1, minZ, maxZ) ||
        !validDevicePoint(py0, minZ, maxZ) ||
        !validDevicePoint(py1, minZ, maxZ)) {
        return;
    }
    const float3 dx = subtractPoint(px1, px0);
    const float3 dy = subtractPoint(py1, py0);
    float3 normal = crossPoint(dx, dy);
    normal.x = -normal.x;
    normal.y = -normal.y;
    normal.z = -normal.z;
    const float norm = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (norm > 1.0e-6F) {
        normals[idx] = make_float3(normal.x / norm, normal.y / norm, normal.z / norm);
    }
}

std::uint8_t readGrayPixel(const ImageView& image, int pixelIndex)
{
    if (image.data == nullptr || image.width <= 0 || image.height <= 0 || image.channels <= 0) {
        return 0;
    }
    const int x = pixelIndex % image.width;
    const int y = pixelIndex / image.width;
    const auto* row = static_cast<const std::uint8_t*>(image.data) + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.strideBytes);
    if (image.elementType == ImageElementType::UInt8) {
        return row[static_cast<std::size_t>(x) * static_cast<std::size_t>(image.channels)];
    }
    return 0;
}

float computeHighFrequencyModulation(const StripeFrameGroup& frame,
                                     const ReconsConfig& config,
                                     int pixelIndex)
{
    if (config.stripeRequirements.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const StripeRequirement& highFrequency = config.stripeRequirements.back();
    if (highFrequency.requiredPhaseSteps <= 1) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    float sinSum = 0.0F;
    float cosSum = 0.0F;
    int foundSteps = 0;
    for (int step = 0; step < highFrequency.requiredPhaseSteps; ++step) {
        const StripeImage* stripeForStep = nullptr;
        for (const StripeImage& stripe : frame.leftStripes) {
            if (stripe.frequencyIndex == highFrequency.frequencyIndex && stripe.phaseStepIndex == step) {
                stripeForStep = &stripe;
                break;
            }
        }
        if (stripeForStep == nullptr) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        const float value = static_cast<float>(readGrayPixel(stripeForStep->image, pixelIndex));
        const float angle = 2.0F * CUDART_PI_F * static_cast<float>(step) /
                            static_cast<float>(highFrequency.requiredPhaseSteps);
        sinSum += value * std::sin(angle);
        cosSum += value * std::cos(angle);
        ++foundSteps;
    }
    if (foundSteps <= 1) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const float sinCoeff = 2.0F * sinSum / static_cast<float>(foundSteps);
    const float cosCoeff = 2.0F * cosSum / static_cast<float>(foundSteps);
    return std::sqrt(sinCoeff * sinCoeff + cosCoeff * cosCoeff);
}

void assignColor(const StripeFrameGroup& frame, int pixelIndex, PointCloudVertex& vertex)
{
    const StripeImage* source = nullptr;
    for (const StripeImage& stripe : frame.leftStripes) {
        if (source == nullptr || stripe.projectorIndex > source->projectorIndex) {
            source = &stripe;
        }
    }
    const std::uint8_t gray = source == nullptr ? 0 : readGrayPixel(source->image, pixelIndex);
    vertex.r = gray;
    vertex.g = gray;
    vertex.b = gray;
}

} // namespace

PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame,
                                                         const PointCloudOutputOptions& outputOptions)
{
    PointCloudReconstructionResult result;
    result.stats.stageName = "point_cloud_reconstruct_cuda";
    if (!unwrappedPhase.status.ok()) {
        result.status = unwrappedPhase.status;
        result.stats.status = result.status;
        return result;
    }
    if (calibration.qMatrix.size() != 16) {
        result.status = {StatusCode::CalibrationFieldMissing, "PointCloudReconstructorCuda", "Q matrix is required for phase-5 point cloud reconstruction"};
        result.stats.status = result.status;
        return result;
    }
    if (unwrappedPhase.left.width <= 0 || unwrappedPhase.left.height <= 0 ||
        unwrappedPhase.left.width != unwrappedPhase.right.width ||
        unwrappedPhase.left.height != unwrappedPhase.right.height) {
        result.status = {StatusCode::MatchingFailed, "PointCloudReconstructorCuda", "left/right absolute phase dimensions do not match"};
        result.stats.status = result.status;
        return result;
    }
    const std::size_t expectedPhaseSize =
        static_cast<std::size_t>(unwrappedPhase.left.width) *
        static_cast<std::size_t>(unwrappedPhase.left.height);
    if (unwrappedPhase.left.absolutePhase.size() != expectedPhaseSize ||
        unwrappedPhase.right.absolutePhase.size() != expectedPhaseSize) {
        result.status = {StatusCode::MatchingFailed, "PointCloudReconstructorCuda", "absolute phase size must match width * height"};
        result.stats.status = result.status;
        return result;
    }

    int deviceCount = 0;
    const cudaError_t deviceError = cudaGetDeviceCount(&deviceCount);
    if (deviceError != cudaSuccess || deviceCount <= 0) {
        result.status = {StatusCode::CudaInitFailed, "PointCloudReconstructorCuda", cudaGetErrorString(deviceError)};
        result.stats.status = result.status;
        return result;
    }

    const int width = unwrappedPhase.left.width;
    const int height = unwrappedPhase.left.height;
    const int pixelCount = width * height;
    const std::size_t phaseBytes = sizeof(float) * static_cast<std::size_t>(pixelCount);
    const std::size_t pointBytes = sizeof(float3) * static_cast<std::size_t>(pixelCount);
    const bool materializeVertices = outputOptions.materializeVertices;
    const bool materializeQualityGrid = outputOptions.materializeQualityGrid;
    const bool materializePointCloud = materializeVertices || materializeQualityGrid;
    const bool computeNormals = materializePointCloud;
    const bool materializeColor = materializeVertices && config.colorTextureEnabled;
    const ImageView* colorInput = nullptr;
    if (materializeColor) {
        colorInput = frame.leftColor.data != nullptr ? &frame.leftColor : &frame.color;
        if (colorInput->data == nullptr) {
            result.status = {StatusCode::InputMissing, "PointCloudReconstructorCuda", "color texture is enabled but left color input is missing"};
            result.stats.status = result.status;
            return result;
        }
        if (colorInput->width != width || colorInput->height != height) {
            result.status = {StatusCode::InputSizeMismatch, "PointCloudReconstructorCuda", "color texture dimensions do not match phase dimensions"};
            result.stats.status = result.status;
            return result;
        }
        if (colorInput->elementType != ImageElementType::UInt8 || colorInput->channels != 3) {
            result.status = {StatusCode::InputTypeUnsupported, "PointCloudReconstructorCuda", "color texture must be packed UInt8 BGR"};
            result.stats.status = result.status;
            return result;
        }
        if (colorInput->strideBytes < width * 3) {
            result.status = {StatusCode::InputStrideInvalid, "PointCloudReconstructorCuda", "color texture stride is smaller than packed BGR row"};
            result.stats.status = result.status;
            return result;
        }
    }
    result.width = width;
    result.height = height;
    double deviceAllocationMs = 0.0;
    double hostToDeviceMs = 0.0;
    double kernelMs = 0.0;
    double hostAllocationMs = 0.0;
    double deviceToHostMs = 0.0;
    double materializeMs = 0.0;

    auto timingStart = SteadyClock::now();
    DeviceBuffer dLeft;
    DeviceBuffer dRight;
    DeviceBuffer dLeftRectified;
    DeviceBuffer dRightRectified;
    DeviceBuffer dDisparity;
    DeviceBuffer dDisparityFiltered;
    DeviceBuffer dScores;
    DeviceBuffer dCandidateCounts;
    DeviceBuffer dMatchingRejectionCounts;
    DeviceBuffer dPointCounts;
    DeviceBuffer dQ;
    DeviceBuffer dRawPoints;
    DeviceBuffer dSmoothedPoints;
    DeviceBuffer dFilteredPoints;
    DeviceBuffer dNormals;
    DeviceBuffer dRawColor;
    DeviceBuffer dRectifiedColor;
    Status status;
    for (auto* buffer : {&dLeft, &dRight, &dLeftRectified, &dRightRectified, &dDisparity, &dDisparityFiltered, &dScores}) {
        status = cudaStatus(cudaMalloc(&buffer->ptr, phaseBytes), "cudaMalloc phase/disparity buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    for (auto* buffer : {&dRawPoints, &dFilteredPoints}) {
        status = cudaStatus(cudaMalloc(&buffer->ptr, pointBytes), "cudaMalloc point buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (config.pointCloudSmoothingEnabled) {
        status = cudaStatus(cudaMalloc(&dSmoothedPoints.ptr, pointBytes), "cudaMalloc smoothed point buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (computeNormals) {
        status = cudaStatus(cudaMalloc(&dNormals.ptr, pointBytes), "cudaMalloc normal buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    const std::size_t colorBytes = static_cast<std::size_t>(pixelCount) * 3;
    if (materializeColor) {
        status = cudaStatus(cudaMalloc(&dRawColor.ptr, colorBytes), "cudaMalloc raw color buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMalloc(&dRectifiedColor.ptr, colorBytes), "cudaMalloc rectified color buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    status = cudaStatus(cudaMalloc(&dQ.ptr, sizeof(float) * 16), "cudaMalloc Q buffer");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMalloc(&dPointCounts.ptr, sizeof(unsigned int) * 3), "cudaMalloc point counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    const std::size_t candidateBytes = sizeof(int) * static_cast<std::size_t>(pixelCount);
    status = cudaStatus(cudaMalloc(&dCandidateCounts.ptr, candidateBytes), "cudaMalloc candidate count buffer");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMalloc(&dMatchingRejectionCounts.ptr, sizeof(unsigned int) * 2), "cudaMalloc matching rejection counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemset(dMatchingRejectionCounts.ptr, 0, sizeof(unsigned int) * 2), "cudaMemset matching rejection counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemset(dPointCounts.ptr, 0, sizeof(unsigned int) * 3), "cudaMemset point counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    deviceAllocationMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    std::vector<float> q(16, 0.0F);
    for (std::size_t i = 0; i < q.size(); ++i) {
        q[i] = static_cast<float>(calibration.qMatrix[i]);
    }
    status = cudaStatus(cudaMemcpy(dLeft.ptr, unwrappedPhase.left.absolutePhase.data(), phaseBytes, cudaMemcpyHostToDevice), "cudaMemcpy left phase H2D");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemcpy(dRight.ptr, unwrappedPhase.right.absolutePhase.data(), phaseBytes, cudaMemcpyHostToDevice), "cudaMemcpy right phase H2D");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemcpy(dQ.ptr, q.data(), sizeof(float) * q.size(), cudaMemcpyHostToDevice), "cudaMemcpy Q H2D");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    if (materializeColor) {
        status = cudaStatus(cudaMemcpy2D(dRawColor.ptr,
                                        static_cast<std::size_t>(width) * 3,
                                        colorInput->data,
                                        static_cast<std::size_t>(colorInput->strideBytes),
                                        static_cast<std::size_t>(width) * 3,
                                        static_cast<std::size_t>(height),
                                        cudaMemcpyHostToDevice),
                            "cudaMemcpy2D raw color H2D");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    hostToDeviceMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    const float* leftPhaseForMatching = dLeft.as<const float>();
    const float* rightPhaseForMatching = dRight.as<const float>();
    const bool rectificationAvailable = hasRectificationCalibration(calibration);
    RemapCalibration leftRemap;
    if (rectificationAvailable) {
        leftRemap = makeRemapCalibration(calibration.leftIntrinsics,
                                         calibration.leftDistortion,
                                         calibration.rectificationLeft,
                                         calibration.projectionLeft);
        const RemapCalibration rightRemap = makeRemapCalibration(calibration.rightIntrinsics,
                                                                 calibration.rightDistortion,
                                                                 calibration.rectificationRight,
                                                                 calibration.projectionRight);
        remapPhaseKernel<<<grid, block>>>(dLeft.as<const float>(), dLeftRectified.as<float>(), width, height, leftRemap);
        status = cudaStatus(cudaGetLastError(), "remapPhaseKernel left launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        remapPhaseKernel<<<grid, block>>>(dRight.as<const float>(), dRightRectified.as<float>(), width, height, rightRemap);
        status = cudaStatus(cudaGetLastError(), "remapPhaseKernel right launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        leftPhaseForMatching = dLeftRectified.as<const float>();
        rightPhaseForMatching = dRightRectified.as<const float>();
    }
    if (materializeColor) {
        ColorCorrection correction;
        for (std::size_t i = 0; i < config.colorCorrectionMatrix.size() && i < 12; ++i) {
            correction.matrix[i] = static_cast<float>(config.colorCorrectionMatrix[i]);
        }
        correction.gamma = static_cast<float>(config.colorGamma);
        remapCorrectColorKernel<<<grid, block>>>(dRawColor.as<const unsigned char>(),
                                                 dRectifiedColor.as<unsigned char>(),
                                                 width,
                                                 height,
                                                 leftRemap,
                                                 correction,
                                                 rectificationAvailable ? 1 : 0);
        status = cudaStatus(cudaGetLastError(), "remapCorrectColorKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }

    const DisparityWindow disparityWindow = makeDisparityWindow(calibration, config, width);
    computeDisparityKernel<<<grid, block>>>(leftPhaseForMatching,
                                            rightPhaseForMatching,
                                            dDisparity.as<float>(),
                                            dScores.as<float>(),
                                            dCandidateCounts.as<int>(),
                                            width,
                                            height,
                                            static_cast<float>(config.phaseDiffThreshold),
                                            disparityWindow.minDisparity,
                                            disparityWindow.maxDisparity,
                                            disparityWindow.enabled ? 1 : 0,
                                            config.matchingUniquenessEnabled ? 1 : 0,
                                            config.matchingMaxCandidateCount,
                                            static_cast<float>(config.matchingMinSecondBestGap),
                                            config.matchingLeftRightConsistencyEnabled ? 1 : 0,
                                            static_cast<float>(config.matchingLeftRightTolerance),
                                            config.matchingRightPhaseMonotonicEnabled ? 1 : 0,
                                            config.matchingRightPhaseMonotonicRadius,
                                            static_cast<float>(config.matchingRightPhaseMinSlope),
                                            config.disparitySubpixelEnabled ? 1 : 0,
                                            dMatchingRejectionCounts.as<unsigned int>());
    status = cudaStatus(cudaGetLastError(), "computeDisparityKernel launch");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }

    const float* disparityForReproject = dDisparity.as<const float>();
    if (config.disparityLocalConsistencyEnabled) {
        filterDisparityLocalConsistencyKernel<<<grid, block>>>(dDisparity.as<const float>(),
                                                               dDisparityFiltered.as<float>(),
                                                               width,
                                                               height,
                                                               static_cast<float>(config.disparityLocalConsistencyThreshold),
                                                               config.disparityLocalConsistencyRadius,
                                                               config.disparityLocalConsistencyMinSupport);
        status = cudaStatus(cudaGetLastError(), "filterDisparityLocalConsistencyKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        disparityForReproject = dDisparityFiltered.as<const float>();
    }

    reprojectKernel<<<grid, block>>>(disparityForReproject,
                                     dRawPoints.as<float3>(),
                                     dQ.as<const float>(),
                                     width,
                                     height,
                                     static_cast<float>(config.minZ),
                                     static_cast<float>(config.maxZ));
    status = cudaStatus(cudaGetLastError(), "reprojectKernel launch");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    const float3* pointsBeforeFilter = dRawPoints.as<const float3>();
    if (config.pointCloudSmoothingEnabled) {
        gaussianBlurPointZKernel<<<grid, block>>>(dRawPoints.as<const float3>(),
                                                  dSmoothedPoints.as<float3>(),
                                                  width,
                                                  height);
        status = cudaStatus(cudaGetLastError(), "gaussianBlurPointZKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        pointsBeforeFilter = dSmoothedPoints.as<const float3>();
    }
    const int enableFilter = (config.pointCloudFilterEnabled && width >= 32 && height >= 32) ? 1 : 0;
    nonConnectedFilterKernel<<<grid, block>>>(pointsBeforeFilter,
                                              dFilteredPoints.as<float3>(),
                                              width,
                                              height,
                                              0.1F,
                                              5,
                                              enableFilter);
    status = cudaStatus(cudaGetLastError(), "nonConnectedFilterKernel launch");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    constexpr int countBlockSize = 256;
    const int countGridSize = (pixelCount + countBlockSize - 1) / countBlockSize;
    countPointStagesKernel<<<countGridSize, countBlockSize>>>(dRawPoints.as<const float3>(),
                                                              pointsBeforeFilter,
                                                              dFilteredPoints.as<const float3>(),
                                                              dPointCounts.as<unsigned int>(),
                                                              pixelCount);
    status = cudaStatus(cudaGetLastError(), "countPointStagesKernel launch");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    if (computeNormals) {
        normalKernel<<<grid, block>>>(dFilteredPoints.as<const float3>(),
                                      dNormals.as<float3>(),
                                      width,
                                      height,
                                      static_cast<float>(config.minZ),
                                      static_cast<float>(config.maxZ));
        status = cudaStatus(cudaGetLastError(), "normalKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    status = cudaStatus(cudaDeviceSynchronize(), "point cloud kernels synchronize");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    kernelMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    std::vector<HostFloat3> filteredPoints;
    std::vector<HostFloat3> normals;
    std::vector<float> matchScores;
    std::vector<int> candidateCounts;
    std::vector<unsigned char> rectifiedColor;
    if (materializePointCloud) {
        filteredPoints.resize(static_cast<std::size_t>(pixelCount));
        normals.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeQualityGrid) {
        matchScores.resize(static_cast<std::size_t>(pixelCount));
        candidateCounts.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeColor) {
        rectifiedColor.resize(colorBytes);
    }
    std::array<unsigned int, 2> matchingRejectionCounts = {};
    std::array<unsigned int, 3> pointCounts = {};
    hostAllocationMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    if (materializePointCloud) {
        status = cudaStatus(cudaMemcpy(filteredPoints.data(), dFilteredPoints.ptr, pointBytes, cudaMemcpyDeviceToHost), "cudaMemcpy filtered points D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemcpy(normals.data(), dNormals.ptr, pointBytes, cudaMemcpyDeviceToHost), "cudaMemcpy normals D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (materializeQualityGrid) {
        status = cudaStatus(cudaMemcpy(matchScores.data(), dScores.ptr, phaseBytes, cudaMemcpyDeviceToHost), "cudaMemcpy match scores D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemcpy(candidateCounts.data(), dCandidateCounts.ptr, candidateBytes, cudaMemcpyDeviceToHost), "cudaMemcpy candidate counts D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (materializeColor) {
        status = cudaStatus(cudaMemcpy(rectifiedColor.data(),
                                       dRectifiedColor.ptr,
                                       colorBytes,
                                       cudaMemcpyDeviceToHost),
                            "cudaMemcpy rectified color D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    status = cudaStatus(cudaMemcpy(matchingRejectionCounts.data(),
                                   dMatchingRejectionCounts.ptr,
                                   sizeof(unsigned int) * matchingRejectionCounts.size(),
                                   cudaMemcpyDeviceToHost),
                        "cudaMemcpy matching rejection counters D2H");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemcpy(pointCounts.data(),
                                   dPointCounts.ptr,
                                   sizeof(unsigned int) * pointCounts.size(),
                                   cudaMemcpyDeviceToHost),
                        "cudaMemcpy point counters D2H");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    deviceToHostMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    result.rawValidPointCount = pointCounts[0];
    result.smoothedGridValidPointCount = pointCounts[1];
    result.filteredGridValidPointCount = pointCounts[2];
    result.normalsComputed = computeNormals;
    result.leftRightRejectedPointCount = matchingRejectionCounts[0];
    result.rightPhaseMonotonicRejectedPointCount = matchingRejectionCounts[1];
    result.matchingSummary =
        "matchingLeftRightRejected=" + std::to_string(result.leftRightRejectedPointCount) +
        ",matchingRightPhaseMonotonicRejected=" + std::to_string(result.rightPhaseMonotonicRejectedPointCount);

    if (materializeQualityGrid) {
        result.gridPoints.assign(static_cast<std::size_t>(pixelCount), {});
    }
    if (materializeVertices) {
        result.vertices.reserve(result.filteredGridValidPointCount);
    }
    for (std::size_t idx = 0; idx < filteredPoints.size(); ++idx) {
        if (materializeQualityGrid) {
            PointCloudGridPoint& gridPoint = result.gridPoints[idx];
            gridPoint.x = filteredPoints[idx].x;
            gridPoint.y = filteredPoints[idx].y;
            gridPoint.z = filteredPoints[idx].z;
            gridPoint.nx = normals[idx].x;
            gridPoint.ny = normals[idx].y;
            gridPoint.nz = normals[idx].z;
            gridPoint.matchCost = matchScores[idx];
            gridPoint.modulation = computeHighFrequencyModulation(frame, config, static_cast<int>(idx));
            gridPoint.candidateCount = candidateCounts[idx];
        }
        if (!isValidPoint(filteredPoints[idx])) {
            continue;
        }
        if (materializeVertices) {
            PointCloudVertex vertex;
            vertex.x = filteredPoints[idx].x;
            vertex.y = filteredPoints[idx].y;
            vertex.z = filteredPoints[idx].z;
            vertex.nx = normals[idx].x;
            vertex.ny = normals[idx].y;
            vertex.nz = normals[idx].z;
            vertex.u = static_cast<int>(idx % static_cast<std::size_t>(width));
            vertex.v = static_cast<int>(idx / static_cast<std::size_t>(width));
            if (materializeColor) {
                vertex.b = rectifiedColor[idx * 3 + 0];
                vertex.g = rectifiedColor[idx * 3 + 1];
                vertex.r = rectifiedColor[idx * 3 + 2];
            } else {
                assignColor(frame, static_cast<int>(idx), vertex);
            }
            result.vertices.push_back(vertex);
        }
    }
    if (materializeColor) {
        result.rectifiedColorBgr = std::move(rectifiedColor);
    }
    materializeMs = elapsedMilliseconds(timingStart);
    result.pointCloudStageSummary =
        "rawValid=" + std::to_string(result.rawValidPointCount) +
        ",smoothedValid=" + std::to_string(result.smoothedGridValidPointCount) +
        ",filteredValid=" + std::to_string(result.filteredGridValidPointCount) +
        ",smoothingApplied=" + std::string(config.pointCloudSmoothingEnabled ? "true" : "false") +
        ",filterApplied=" + std::string(enableFilter != 0 ? "true" : "false") +
        ",colorTextureApplied=" + std::string(materializeColor ? "true" : "false") +
        ",verticesMaterialized=" + std::string(materializeVertices ? "true" : "false") +
        ",qualityGridMaterialized=" + std::string(materializeQualityGrid ? "true" : "false") +
        ",deviceAllocationMs=" + std::to_string(deviceAllocationMs) +
        ",hostToDeviceMs=" + std::to_string(hostToDeviceMs) +
        ",kernelMs=" + std::to_string(kernelMs) +
        ",hostAllocationMs=" + std::to_string(hostAllocationMs) +
        ",deviceToHostMs=" + std::to_string(deviceToHostMs) +
        ",materializeMs=" + std::to_string(materializeMs);

    result.stats.inputImageCount = 2;
    result.stats.validImageCount = result.filteredGridValidPointCount;
    result.stats.checkedPixels = static_cast<std::size_t>(pixelCount);
    result.stats.cudaComputedPixels = static_cast<std::size_t>(pixelCount);
    result.stats.rejectedImageCount = static_cast<std::size_t>(pixelCount) - result.filteredGridValidPointCount;
    result.stats.blackPixels = result.rawValidPointCount;
    result.stats.saturatedPixels = result.filteredGridValidPointCount;
    result.stats.blackPixelRatio = static_cast<double>(result.rawValidPointCount) / static_cast<double>(pixelCount);
    result.stats.saturatedPixelRatio = static_cast<double>(result.filteredGridValidPointCount) / static_cast<double>(pixelCount);
    result.stats.minPixelValue = static_cast<double>(result.rawValidPointCount);
    result.stats.maxPixelValue = static_cast<double>(result.filteredGridValidPointCount);
    result.stats.meanPixelValue = static_cast<double>(result.filteredGridValidPointCount);
    if (result.filteredGridValidPointCount == 0) {
        result.status = {StatusCode::ReconstructionInsufficient, "PointCloudReconstructorCuda", "CUDA reconstruction produced zero valid point-cloud vertices"};
        result.stats.status = result.status;
        return result;
    }

    result.status = {};
    result.stats.status = {};
    return result;
}

} // namespace reconstruct_one_frame
