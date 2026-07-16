#include "reconstruction/PointCloudReconstructor.h"

#include "calibration_model/CudaRectification.cuh"

#include <cuda_runtime.h>
#include <math_constants.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

namespace {

constexpr float kFloatEpsilon = 1.0e-6F;
constexpr int kMatchingCounterLeftRightRejected = 0;
constexpr int kMatchingCounterRightPhaseMonotonicRejected = 1;
constexpr int kMatchingCounterLeftPhaseValid = 2;
constexpr int kMatchingCounterThresholdRejected = 3;
constexpr int kMatchingCounterUniquenessRejected = 4;
constexpr int kMatchingCounterAccepted = 5;
constexpr int kMatchingCounterSubpixelSuccess = 6;
constexpr int kMatchingCounterSubpixelFallback = 7;
constexpr int kMatchingCounterLeftQualityRejected = 8;
constexpr int kMatchingCounterRightQualitySkipped = 9;
constexpr int kMatchingCounterSubpixelFailureRejected = 10;
constexpr int kMatchingCounterCount = 11;

using SteadyClock = std::chrono::steady_clock;

Status cudaStatus(cudaError_t error, const char* operation);

double elapsedMilliseconds(SteadyClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
}

struct DeviceBuffer {
    void* ptr = nullptr;
    std::size_t capacity = 0U;

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

Status ensureCapacity(DeviceBuffer& buffer,
                      std::size_t bytes,
                      const char* operation,
                      bool* allocated = nullptr);

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

struct ColorCorrection {
    float matrix[12] = {};
    float gamma = 1.0F;
};

constexpr int kMaxQualityPhaseSteps = 16;

using cuda_rectification::RemapCalibration;
using cuda_rectification::hasRectificationCalibration;
using cuda_rectification::makeRemapCalibration;
using cuda_rectification::rectifiedToRawPixel;

struct QualityPhaseWeights {
    float sinWeights[kMaxQualityPhaseSteps] = {};
    float cosWeights[kMaxQualityPhaseSteps] = {};
    int stepCount = 0;
};

Status cudaStatus(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return {};
    }
    return {StatusCode::CudaKernelFailed, "PointCloudReconstructorCuda", std::string(operation) + ": " + cudaGetErrorString(error)};
}

class PhaseTexture2D {
public:
    PhaseTexture2D() = default;
    PhaseTexture2D(const PhaseTexture2D&) = delete;
    PhaseTexture2D& operator=(const PhaseTexture2D&) = delete;

    ~PhaseTexture2D()
    {
        if (texture_ != 0U) {
            cudaDestroyTextureObject(texture_);
        }
    }

    Status create(const float* devicePtr, int width, int height, const char* label)
    {
        cudaResourceDesc resource {};
        resource.resType = cudaResourceTypePitch2D;
        resource.res.pitch2D.devPtr = const_cast<float*>(devicePtr);
        resource.res.pitch2D.desc = cudaCreateChannelDesc<float>();
        resource.res.pitch2D.width = static_cast<std::size_t>(width);
        resource.res.pitch2D.height = static_cast<std::size_t>(height);
        resource.res.pitch2D.pitchInBytes = static_cast<std::size_t>(width) * sizeof(float);

        cudaTextureDesc textureDesc {};
        textureDesc.readMode = cudaReadModeElementType;
        textureDesc.addressMode[0] = cudaAddressModeClamp;
        textureDesc.addressMode[1] = cudaAddressModeClamp;
        textureDesc.filterMode = cudaFilterModeLinear;
        textureDesc.normalizedCoords = 0;

        return cudaStatus(cudaCreateTextureObject(&texture_, &resource, &textureDesc, nullptr), label);
    }

    cudaTextureObject_t get() const noexcept
    {
        return texture_;
    }

private:
    cudaTextureObject_t texture_ = 0U;
};

Status ensureCapacity(DeviceBuffer& buffer,
                      std::size_t bytes,
                      const char* operation,
                      bool* allocated)
{
    if (allocated != nullptr) {
        *allocated = false;
    }
    if (buffer.ptr != nullptr && buffer.capacity >= bytes) {
        return {};
    }
    if (buffer.ptr != nullptr) {
        const Status freeStatus = cudaStatus(cudaFree(buffer.ptr), "cudaFree undersized point cloud buffer");
        buffer.ptr = nullptr;
        buffer.capacity = 0U;
        if (!freeStatus.ok()) {
            return freeStatus;
        }
    }
    Status status = cudaStatus(cudaMalloc(&buffer.ptr, bytes), operation);
    if (status.ok()) {
        buffer.capacity = bytes;
        if (allocated != nullptr) {
            *allocated = true;
        }
    }
    return status;
}

bool isValidPoint(const HostFloat3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           !(point.x == 0.0F && point.y == 0.0F && point.z == 0.0F);
}

PointCloudVertex makeDiagnosticVertex(const HostFloat3& point,
                                      std::size_t pixelIndex,
                                      int width,
                                      std::uint8_t red,
                                      std::uint8_t green,
                                      std::uint8_t blue)
{
    PointCloudVertex vertex;
    vertex.x = point.x;
    vertex.y = point.y;
    vertex.z = point.z;
    vertex.u = static_cast<int>(pixelIndex % static_cast<std::size_t>(width));
    vertex.v = static_cast<int>(pixelIndex / static_cast<std::size_t>(width));
    vertex.r = red;
    vertex.g = green;
    vertex.b = blue;
    return vertex;
}

std::string formatMatchingDiagnosticsCsv(const PointCloudMatchingDiagnostics& diagnostics)
{
    std::ostringstream out;
    out << "metric,value\n";
    out << "enabled," << (diagnostics.enabled ? "true" : "false") << "\n";
    out << "left_phase_valid_pixels," << diagnostics.leftPhaseValidPixelCount << "\n";
    out << "threshold_rejected_pixels," << diagnostics.thresholdRejectedPixelCount << "\n";
    out << "uniqueness_rejected_pixels," << diagnostics.uniquenessRejectedPixelCount << "\n";
    out << "accepted_match_pixels," << diagnostics.acceptedMatchPixelCount << "\n";
    out << "left_right_rejected_points," << diagnostics.leftRightRejectedPointCount << "\n";
    out << "right_phase_monotonic_rejected_points,"
        << diagnostics.rightPhaseMonotonicRejectedPointCount << "\n";
    out << "left_quality_rejected_pixels," << diagnostics.leftQualityRejectedPixelCount << "\n";
    out << "right_candidate_quality_skipped," << diagnostics.rightCandidateQualitySkippedCount << "\n";
    out << "subpixel_failure_rejected," << diagnostics.subpixelFailureRejectedCount << "\n";
    out << "subpixel_success," << diagnostics.subpixelSuccessCount << "\n";
    out << "subpixel_fallback," << diagnostics.subpixelFallbackCount << "\n";
    out << "pixels_with_near_candidates," << diagnostics.pixelsWithNearCandidates << "\n";
    out << "ambiguous_candidate_pixels," << diagnostics.ambiguousCandidatePixelCount << "\n";
    out << "max_near_candidate_count," << diagnostics.maxNearCandidateCount << "\n";
    out << std::fixed << std::setprecision(6);
    out << "mean_near_candidate_count," << diagnostics.meanNearCandidateCount << "\n";
    out << "mean_accepted_match_cost," << diagnostics.meanAcceptedMatchCost << "\n";
    out << "max_accepted_match_cost," << diagnostics.maxAcceptedMatchCost << "\n";
    return out.str();
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

__device__ float sampleBilinearGrayU8(const unsigned char* image,
                                      int width,
                                      int height,
                                      float x,
                                      float y)
{
    if (!isfinite(x) || !isfinite(y) || x < 0.0F || y < 0.0F ||
        x >= static_cast<float>(width - 1) || y >= static_cast<float>(height - 1)) {
        return CUDART_NAN_F;
    }
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    const float dx = x - static_cast<float>(x0);
    const float dy = y - static_cast<float>(y0);
    const float v00 = static_cast<float>(image[y0 * width + x0]);
    const float v01 = static_cast<float>(image[y0 * width + x0 + 1]);
    const float v10 = static_cast<float>(image[(y0 + 1) * width + x0]);
    const float v11 = static_cast<float>(image[(y0 + 1) * width + x0 + 1]);
    const float top = v00 * (1.0F - dx) + v01 * dx;
    const float bottom = v10 * (1.0F - dx) + v11 * dx;
    return top * (1.0F - dy) + bottom * dy;
}

__device__ bool sampleColorNeighborhoodMax(const unsigned char* image,
                                           int width,
                                           int height,
                                           int channel,
                                           float x,
                                           float y,
                                           float& result)
{
    if (!isfinite(x) || !isfinite(y) || x < 0.0F || y < 0.0F ||
        x >= static_cast<float>(width - 1) || y >= static_cast<float>(height - 1)) {
        return false;
    }
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    const float v00 = static_cast<float>(image[(y0 * width + x0) * 3 + channel]);
    const float v01 = static_cast<float>(image[(y0 * width + x0 + 1) * 3 + channel]);
    const float v10 = static_cast<float>(image[((y0 + 1) * width + x0) * 3 + channel]);
    const float v11 = static_cast<float>(image[((y0 + 1) * width + x0 + 1) * 3 + channel]);
    result = fmaxf(fmaxf(v00, v01), fmaxf(v10, v11));
    return true;
}

__device__ float smoothHighlightCompression(float value)
{
    constexpr float threshold = 230.0F;
    constexpr float maximum = 250.0F;
    constexpr float softness = 0.8F;
    if (value <= threshold) {
        return value;
    }
    constexpr float maximumExcess = maximum - threshold;
    const float ratio = fminf(1.0F, fmaxf(0.0F, (value - threshold) / maximumExcess));
    const float smoothRatio = (1.0F - cosf(ratio * CUDART_PI_F)) * 0.5F;
    return threshold + smoothRatio * maximumExcess * softness +
        ratio * maximumExcess * (1.0F - softness);
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
                                        int useRectification,
                                        int compressHighlights)
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
    float samples[3] = {
        sampleBilinearColor(input, width, height, 0, srcX, srcY),
        sampleBilinearColor(input, width, height, 1, srcX, srcY),
        sampleBilinearColor(input, width, height, 2, srcX, srcY)
    };
    if (compressHighlights != 0) {
        for (float& sample : samples) {
            sample = fminf(255.0F, fmaxf(0.0F, smoothHighlightCompression(sample)));
        }
    }
    const float features[4] = {samples[0], samples[1], samples[2], 1.0F};
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

__global__ void computeRectifiedHighFrequencyQualityKernel(
    const unsigned char* inputImages,
    float* modulation,
    unsigned char* lightFlags,
    int width,
    int height,
    RemapCalibration calibration,
    QualityPhaseWeights weights,
    int useRectification)
{
    const int u = blockIdx.x * blockDim.x + threadIdx.x;
    const int v = blockIdx.y * blockDim.y + threadIdx.y;
    if (u >= width || v >= height) {
        return;
    }
    const int idx = v * width + u;
    float srcX = static_cast<float>(u);
    float srcY = static_cast<float>(v);
    if (useRectification != 0 && !rectifiedToRawPixel(u, v, calibration, srcX, srcY)) {
        modulation[idx] = CUDART_NAN_F;
        lightFlags[idx] = 4U;
        return;
    }

    const int pixelCount = width * height;
    float sinSum = 0.0F;
    float cosSum = 0.0F;
    unsigned char flags = 0U;
    for (int step = 0; step < weights.stepCount; ++step) {
        const float value = sampleBilinearGrayU8(
            inputImages + step * pixelCount, width, height, srcX, srcY);
        if (!isfinite(value)) {
            flags |= 4U;
            continue;
        }
        if (value >= 250.0F) {
            flags |= 1U;
        }
        if (value <= 10.0F) {
            flags |= 2U;
        }
        sinSum += value * weights.sinWeights[step];
        cosSum += value * weights.cosWeights[step];
    }
    modulation[idx] = weights.stepCount > 1
        ? 2.0F * sqrtf(sinSum * sinSum + cosSum * cosSum) /
            static_cast<float>(weights.stepCount)
        : CUDART_NAN_F;
    lightFlags[idx] = flags;
}

__global__ void buildClear255MaskKernel(const unsigned char* input,
                                        unsigned char* output,
                                        int width,
                                        int height,
                                        RemapCalibration calibration,
                                        int useRectification)
{
    const int u = blockIdx.x * blockDim.x + threadIdx.x;
    const int v = blockIdx.y * blockDim.y + threadIdx.y;
    if (u >= width || v >= height) {
        return;
    }
    const int idx = v * width + u;
    output[idx] = 255U;
    float srcX = static_cast<float>(u);
    float srcY = static_cast<float>(v);
    if (useRectification != 0 && !rectifiedToRawPixel(u, v, calibration, srcX, srcY)) {
        return;
    }
    float localMaximum = 0.0F;
    if (sampleColorNeighborhoodMax(input, width, height, 0, srcX, srcY, localMaximum)) {
        output[idx] = localMaximum >= 250.0F ? 0U : 255U;
    }
}

__global__ void dilateInvalidMaskKernel(const unsigned char* input,
                                        unsigned char* output,
                                        int width,
                                        int height,
                                        int radius)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    unsigned char value = 255U;
    for (int dy = -radius; dy <= radius && value != 0U; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int dx = -radius; dx <= radius; ++dx) {
            const int xx = x + dx;
            if (xx >= 0 && xx < width && input[yy * width + xx] == 0U) {
                value = 0U;
                break;
            }
        }
    }
    output[y * width + x] = value;
}

__global__ void countInvalidMaskKernel(const unsigned char* mask,
                                       unsigned int* count,
                                       int pixelCount)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < pixelCount && mask[idx] == 0U) {
        atomicAdd(count, 1U);
    }
}

__device__ float samplePhase(cudaTextureObject_t phaseTexture, int x, int y)
{
    return tex2D<float>(phaseTexture, x, y);
}

__device__ bool leftRightConsistent(cudaTextureObject_t leftTexture,
                                    cudaTextureObject_t rightTexture,
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
    const float rightPhase = samplePhase(rightTexture, rightX, row);
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
        const float leftPhase = samplePhase(leftTexture, x, row);
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
    const float bestLeftPhase = samplePhase(leftTexture, bestX, row);
    if (useSubpixel != 0 && isfinite(bestLeftPhase)) {
        if (rightPhase > bestLeftPhase && bestX + 1 < width) {
            const float nextPhase = samplePhase(leftTexture, bestX + 1, row);
            if (isfinite(nextPhase) && nextPhase > bestLeftPhase && rightPhase <= nextPhase) {
                bestXFloat = static_cast<float>(bestX) +
                             (rightPhase - bestLeftPhase) / (nextPhase - bestLeftPhase);
            }
        } else if (rightPhase < bestLeftPhase && bestX - 1 >= 0) {
            const float previousPhase = samplePhase(leftTexture, bestX - 1, row);
            if (isfinite(previousPhase) && previousPhase < bestLeftPhase && rightPhase >= previousPhase) {
                bestXFloat = static_cast<float>(bestX) -
                             (bestLeftPhase - rightPhase) / (bestLeftPhase - previousPhase);
            }
        }
    }
    return fabsf(bestXFloat - static_cast<float>(leftX)) <= tolerance;
}

__device__ bool rightPhaseMonotonicSupported(cudaTextureObject_t rightTexture,
                                             int row,
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
                const float lower = samplePhase(rightTexture, lowerX, row);
                hasLowerNeighbor = hasLowerNeighbor ||
                                   (isfinite(lower) && lower < bestPhase - slope);
            }
            const int higherX = bestX + step;
            if (higherX < width) {
                const float higher = samplePhase(rightTexture, higherX, row);
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
            const float neighbor = samplePhase(rightTexture, x, row);
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
            const float neighbor = samplePhase(rightTexture, x, row);
            if (isfinite(neighbor) &&
                neighbor < bestPhase - slope &&
                neighbor <= leftPhase + slope) {
                return true;
            }
        }
    }
    return false;
}

__device__ bool candidatePassesQualityFilter(int idx,
                                             const float* modulation,
                                             const unsigned char* lightFlags,
                                             int pixelCount,
                                             float minModulation,
                                             int rejectSaturation,
                                             int rejectLowLight)
{
    if (idx < 0 || idx >= pixelCount) {
        return false;
    }
    if (lightFlags != nullptr) {
        const unsigned char flags = lightFlags[idx];
        if (rejectSaturation != 0 && (flags & 1U) != 0U) {
            return false;
        }
        if (rejectLowLight != 0 && (flags & (2U | 4U)) != 0U) {
            return false;
        }
    }
    if (modulation != nullptr) {
        const float value = modulation[idx];
        if (!isfinite(value) || value < minModulation) {
            return false;
        }
    }
    return true;
}

__global__ void computeDisparityKernel(cudaTextureObject_t leftTexture,
                                       cudaTextureObject_t rightTexture,
                                       const unsigned char* validMask,
                                       const float* leftQualityModulation,
                                       const unsigned char* leftQualityFlags,
                                       const float* rightQualityModulation,
                                       const unsigned char* rightQualityFlags,
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
                                       int rejectOnSubpixelFailure,
                                       int useCandidateQualityFilter,
                                       float candidateMinModulation,
                                       int candidateRejectSaturation,
                                       int candidateRejectLowLight,
                                       int collectDiagnostics,
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
    if (validMask != nullptr && validMask[idx] == 0U) {
        return;
    }
    const float leftPhase = samplePhase(leftTexture, x, y);
    if (!isfinite(leftPhase) || fabsf(leftPhase) < 0.001F) {
        return;
    }
    const int pixelCount = width * height;
    if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
        atomicAdd(&rejectionCounts[kMatchingCounterLeftPhaseValid], 1U);
    }
    if (useCandidateQualityFilter != 0 &&
        !candidatePassesQualityFilter(idx,
                                      leftQualityModulation,
                                      leftQualityFlags,
                                      pixelCount,
                                      candidateMinModulation,
                                      candidateRejectSaturation,
                                      candidateRejectLowLight)) {
        if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
            atomicAdd(&rejectionCounts[kMatchingCounterLeftQualityRejected], 1U);
        }
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
        const float rightPhase = samplePhase(rightTexture, rx, y);
        if (!isfinite(rightPhase) || x - rx > width - 1) {
            continue;
        }
        if (useCandidateQualityFilter != 0 &&
            !candidatePassesQualityFilter(y * width + rx,
                                          rightQualityModulation,
                                          rightQualityFlags,
                                          pixelCount,
                                          candidateMinModulation,
                                          candidateRejectSaturation,
                                          candidateRejectLowLight)) {
            if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
                atomicAdd(&rejectionCounts[kMatchingCounterRightQualitySkipped], 1U);
            }
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
    if (!matchOk) {
        if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
            atomicAdd(&rejectionCounts[kMatchingCounterThresholdRejected], 1U);
        }
        return;
    }
    if (matchOk && useUniqueness != 0) {
        if (nearCandidateCount > max(1, maxCandidateCount)) {
            matchOk = false;
        } else if (secondBestCost < FLT_MAX && secondBestCost - bestCost < minSecondBestGap) {
            matchOk = false;
        }
        if (!matchOk) {
            if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
                atomicAdd(&rejectionCounts[kMatchingCounterUniquenessRejected], 1U);
            }
            return;
        }
    }
    if (matchOk && useLeftRightConsistency != 0) {
        if (!leftRightConsistent(leftTexture,
                                 rightTexture,
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
                atomicAdd(&rejectionCounts[kMatchingCounterLeftRightRejected], 1U);
            }
        }
    }
    if (matchOk && useRightPhaseMonotonicity != 0) {
        const float bestRightPhase = samplePhase(rightTexture, bestX, y);
        if (!rightPhaseMonotonicSupported(rightTexture,
                                          y,
                                          width,
                                          bestX,
                                          leftPhase,
                                          bestRightPhase,
                                          rightPhaseMonotonicRadius,
                                          rightPhaseMinimumSlope)) {
            matchOk = false;
            if (rejectionCounts != nullptr) {
                atomicAdd(&rejectionCounts[kMatchingCounterRightPhaseMonotonicRejected], 1U);
            }
        }
    }
    if (!matchOk) {
        return;
    }

    float bestXFloat = static_cast<float>(bestX);
    const float bestRightPhase = samplePhase(rightTexture, bestX, y);
    bool subpixelResolved = false;
    bool subpixelFailed = false;
    if (useSubpixel != 0) {
        if (leftPhase > bestRightPhase && bestX + 1 < width) {
            const float nextPhase = samplePhase(rightTexture, bestX + 1, y);
            if (isfinite(nextPhase) && nextPhase > bestRightPhase && leftPhase <= nextPhase) {
                bestXFloat = static_cast<float>(bestX) + (leftPhase - bestRightPhase) / (nextPhase - bestRightPhase);
                subpixelResolved = true;
            } else {
                subpixelFailed = true;
            }
        } else if (leftPhase < bestRightPhase && bestX - 1 >= 0) {
            const float prevPhase = samplePhase(rightTexture, bestX - 1, y);
            if (isfinite(prevPhase) && prevPhase < bestRightPhase && leftPhase >= prevPhase) {
                bestXFloat = static_cast<float>(bestX) - (bestRightPhase - leftPhase) / (bestRightPhase - prevPhase);
                subpixelResolved = true;
            } else {
                subpixelFailed = true;
            }
        }
        subpixelFailed = subpixelFailed || !subpixelResolved;
    }
    if (rejectOnSubpixelFailure != 0 && subpixelFailed) {
        if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
            atomicAdd(&rejectionCounts[kMatchingCounterSubpixelFailureRejected], 1U);
        }
        return;
    }
    if (collectDiagnostics != 0 && rejectionCounts != nullptr) {
        atomicAdd(&rejectionCounts[kMatchingCounterAccepted], 1U);
        if (useSubpixel != 0) {
            atomicAdd(&rejectionCounts[subpixelResolved ? kMatchingCounterSubpixelSuccess
                                                        : kMatchingCounterSubpixelFallback],
                      1U);
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

struct HighFrequencyQualityPlan {
    std::vector<const ImageView*> images;
    std::vector<float> sinWeights;
    std::vector<float> cosWeights;
};

HighFrequencyQualityPlan makeHighFrequencyQualityPlan(const std::vector<StripeImage>& stripes,
                                                      const ReconsConfig& config)
{
    HighFrequencyQualityPlan plan;
    if (config.stripeRequirements.empty()) {
        return plan;
    }
    const StripeRequirement& highFrequency = config.stripeRequirements.back();
    if (highFrequency.requiredPhaseSteps <= 1) {
        return plan;
    }
    plan.images.reserve(static_cast<std::size_t>(highFrequency.requiredPhaseSteps));
    plan.sinWeights.reserve(static_cast<std::size_t>(highFrequency.requiredPhaseSteps));
    plan.cosWeights.reserve(static_cast<std::size_t>(highFrequency.requiredPhaseSteps));
    for (int step = 0; step < highFrequency.requiredPhaseSteps; ++step) {
        const StripeImage* stripeForStep = nullptr;
        for (const StripeImage& stripe : stripes) {
            if (stripe.frequencyIndex == highFrequency.frequencyIndex && stripe.phaseStepIndex == step) {
                stripeForStep = &stripe;
                break;
            }
        }
        if (stripeForStep == nullptr) {
            return {};
        }
        const float angle = 2.0F * CUDART_PI_F * static_cast<float>(step) /
                            static_cast<float>(highFrequency.requiredPhaseSteps);
        plan.images.push_back(&stripeForStep->image);
        plan.sinWeights.push_back(std::sin(angle));
        plan.cosWeights.push_back(std::cos(angle));
    }
    return plan;
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

struct PointCloudCudaWorkspace::Impl {
    DeviceBuffer left;
    DeviceBuffer right;
    DeviceBuffer leftRectified;
    DeviceBuffer rightRectified;
    DeviceBuffer disparity;
    DeviceBuffer disparityFiltered;
    DeviceBuffer scores;
    DeviceBuffer candidateCounts;
    DeviceBuffer matchingRejectionCounts;
    DeviceBuffer pointCounts;
    DeviceBuffer q;
    DeviceBuffer rawPoints;
    DeviceBuffer smoothedPoints;
    DeviceBuffer filteredPoints;
    DeviceBuffer normals;
    DeviceBuffer rawColor;
    DeviceBuffer rectifiedColor;
    DeviceBuffer clear255Mask;
    DeviceBuffer clear255MaskScratch;
    DeviceBuffer clear255RejectedCount;
    DeviceBuffer qualityInputImages;
    DeviceBuffer qualityModulation;
    DeviceBuffer qualityLightFlags;
    DeviceBuffer rightQualityInputImages;
    DeviceBuffer rightQualityModulation;
    DeviceBuffer rightQualityLightFlags;
};

PointCloudCudaWorkspace::PointCloudCudaWorkspace()
    : impl_(std::make_unique<Impl>())
{
}

PointCloudCudaWorkspace::~PointCloudCudaWorkspace() = default;
PointCloudCudaWorkspace::PointCloudCudaWorkspace(PointCloudCudaWorkspace&&) noexcept = default;
PointCloudCudaWorkspace& PointCloudCudaWorkspace::operator=(PointCloudCudaWorkspace&&) noexcept = default;

void PointCloudCudaWorkspace::reset() noexcept
{
    impl_.reset();
}

PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame,
                                                         PointCloudCudaWorkspace& workspaceHandle,
                                                         const PointCloudOutputOptions& outputOptions)
{
    if (!workspaceHandle.impl_) {
        workspaceHandle.impl_ = std::make_unique<PointCloudCudaWorkspace::Impl>();
    }
    PointCloudCudaWorkspace::Impl& workspace = *workspaceHandle.impl_;
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
    const bool materializeMatchingDiagnostics = outputOptions.materializeMatchingDiagnostics;
    const bool materializeStageVertices = outputOptions.materializeStageVertices;
    const bool materializeMatchingSignals =
        materializeQualityGrid || materializeMatchingDiagnostics || materializeStageVertices;
    const bool materializePointCloud =
        materializeVertices || materializeQualityGrid || materializeStageVertices;
    const bool computeNormals = materializePointCloud;
    const bool materializeColor = materializeVertices && config.colorTextureEnabled;
    const bool clear255Active = config.clear255 && frame.metalScan;
    const bool needsAuxiliaryColor = materializeColor || clear255Active;
    const bool needsMatchingQualitySignals = config.matchingCandidateQualityFilterEnabled;
    const bool needsLeftQualitySignals =
        (materializeQualityGrid && config.qualityInfoUseModulation) || needsMatchingQualitySignals;
    const HighFrequencyQualityPlan leftHighFrequencyQualityPlan =
        needsLeftQualitySignals
        ? makeHighFrequencyQualityPlan(frame.leftStripes, config)
        : HighFrequencyQualityPlan {};
    const HighFrequencyQualityPlan rightHighFrequencyQualityPlan =
        needsMatchingQualitySignals
        ? makeHighFrequencyQualityPlan(frame.rightStripes, config)
        : HighFrequencyQualityPlan {};
    const bool leftQualitySignalsActive =
        leftHighFrequencyQualityPlan.images.size() > 1U &&
        leftHighFrequencyQualityPlan.images.size() <= static_cast<std::size_t>(kMaxQualityPhaseSteps);
    const bool rightQualitySignalsActive =
        rightHighFrequencyQualityPlan.images.size() > 1U &&
        rightHighFrequencyQualityPlan.images.size() <= static_cast<std::size_t>(kMaxQualityPhaseSteps);
    const bool matchingQualityFilterActive =
        needsMatchingQualitySignals && leftQualitySignalsActive && rightQualitySignalsActive;
    if (needsMatchingQualitySignals && !matchingQualityFilterActive) {
        result.status = {StatusCode::InputMissing,
                         "PointCloudReconstructorCuda",
                         "matching candidate quality filter requires configured high-frequency stripeRequirements and left/right high-frequency stripes"};
        result.stats.status = result.status;
        return result;
    }
    const ImageView* colorInput = nullptr;
    if (needsAuxiliaryColor) {
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
    std::size_t deviceAllocationCount = 0U;

    auto timingStart = SteadyClock::now();
    DeviceBuffer& dLeft = workspace.left;
    DeviceBuffer& dRight = workspace.right;
    DeviceBuffer& dLeftRectified = workspace.leftRectified;
    DeviceBuffer& dRightRectified = workspace.rightRectified;
    DeviceBuffer& dDisparity = workspace.disparity;
    DeviceBuffer& dDisparityFiltered = workspace.disparityFiltered;
    DeviceBuffer& dScores = workspace.scores;
    DeviceBuffer& dCandidateCounts = workspace.candidateCounts;
    DeviceBuffer& dMatchingRejectionCounts = workspace.matchingRejectionCounts;
    DeviceBuffer& dPointCounts = workspace.pointCounts;
    DeviceBuffer& dQ = workspace.q;
    DeviceBuffer& dRawPoints = workspace.rawPoints;
    DeviceBuffer& dSmoothedPoints = workspace.smoothedPoints;
    DeviceBuffer& dFilteredPoints = workspace.filteredPoints;
    DeviceBuffer& dNormals = workspace.normals;
    DeviceBuffer& dRawColor = workspace.rawColor;
    DeviceBuffer& dRectifiedColor = workspace.rectifiedColor;
    DeviceBuffer& dClear255Mask = workspace.clear255Mask;
    DeviceBuffer& dClear255MaskScratch = workspace.clear255MaskScratch;
    DeviceBuffer& dClear255RejectedCount = workspace.clear255RejectedCount;
    DeviceBuffer& dQualityInputImages = workspace.qualityInputImages;
    DeviceBuffer& dQualityModulation = workspace.qualityModulation;
    DeviceBuffer& dQualityLightFlags = workspace.qualityLightFlags;
    DeviceBuffer& dRightQualityInputImages = workspace.rightQualityInputImages;
    DeviceBuffer& dRightQualityModulation = workspace.rightQualityModulation;
    DeviceBuffer& dRightQualityLightFlags = workspace.rightQualityLightFlags;
    Status status;
    auto ensureWorkspaceCapacity = [&](DeviceBuffer& buffer,
                                       std::size_t bytes,
                                       const char* operation) {
        bool allocated = false;
        Status capacityStatus = ensureCapacity(buffer, bytes, operation, &allocated);
        if (allocated) {
            ++deviceAllocationCount;
        }
        return capacityStatus;
    };
    for (auto* buffer : {&dLeft, &dRight, &dLeftRectified, &dRightRectified, &dDisparity, &dDisparityFiltered, &dScores}) {
        status = ensureWorkspaceCapacity(*buffer, phaseBytes, "cudaMalloc phase/disparity buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    for (auto* buffer : {&dRawPoints, &dFilteredPoints}) {
        status = ensureWorkspaceCapacity(*buffer, pointBytes, "cudaMalloc point buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (config.pointCloudSmoothingEnabled) {
        status = ensureWorkspaceCapacity(dSmoothedPoints, pointBytes, "cudaMalloc smoothed point buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (computeNormals) {
        status = ensureWorkspaceCapacity(dNormals, pointBytes, "cudaMalloc normal buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    const std::size_t colorBytes = static_cast<std::size_t>(pixelCount) * 3;
    if (needsAuxiliaryColor) {
        status = ensureWorkspaceCapacity(dRawColor, colorBytes, "cudaMalloc raw color buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (materializeColor) {
        status = ensureWorkspaceCapacity(dRectifiedColor, colorBytes, "cudaMalloc rectified color buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (clear255Active) {
        const std::size_t maskBytes = static_cast<std::size_t>(pixelCount);
        status = ensureWorkspaceCapacity(dClear255Mask, maskBytes, "cudaMalloc clear255 mask buffer");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        if (config.clear255DilateRadius > 0) {
            status = ensureWorkspaceCapacity(
                dClear255MaskScratch, maskBytes, "cudaMalloc clear255 dilation buffer");
            if (!status.ok()) {
                result.status = status;
                result.stats.status = status;
                return result;
            }
        }
        status = ensureWorkspaceCapacity(
            dClear255RejectedCount, sizeof(unsigned int), "cudaMalloc clear255 rejection counter");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemset(dClear255RejectedCount.ptr, 0, sizeof(unsigned int)),
                            "cudaMemset clear255 rejection counter");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (leftQualitySignalsActive) {
        const std::size_t qualityImageBytes = static_cast<std::size_t>(pixelCount) *
            leftHighFrequencyQualityPlan.images.size();
        status = ensureWorkspaceCapacity(
            dQualityInputImages, qualityImageBytes, "cudaMalloc quality input images");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = ensureWorkspaceCapacity(dQualityModulation, phaseBytes, "cudaMalloc quality modulation");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = ensureWorkspaceCapacity(
            dQualityLightFlags, static_cast<std::size_t>(pixelCount), "cudaMalloc quality light flags");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (rightQualitySignalsActive) {
        const std::size_t qualityImageBytes = static_cast<std::size_t>(pixelCount) *
            rightHighFrequencyQualityPlan.images.size();
        status = ensureWorkspaceCapacity(
            dRightQualityInputImages, qualityImageBytes, "cudaMalloc right quality input images");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = ensureWorkspaceCapacity(dRightQualityModulation, phaseBytes, "cudaMalloc right quality modulation");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = ensureWorkspaceCapacity(
            dRightQualityLightFlags, static_cast<std::size_t>(pixelCount), "cudaMalloc right quality light flags");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    status = ensureWorkspaceCapacity(dQ, sizeof(float) * 16, "cudaMalloc Q buffer");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = ensureWorkspaceCapacity(dPointCounts, sizeof(unsigned int) * 3, "cudaMalloc point counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    const std::size_t candidateBytes = sizeof(int) * static_cast<std::size_t>(pixelCount);
    status = ensureWorkspaceCapacity(dCandidateCounts, candidateBytes, "cudaMalloc candidate count buffer");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = ensureWorkspaceCapacity(
        dMatchingRejectionCounts,
        sizeof(unsigned int) * kMatchingCounterCount,
        "cudaMalloc matching rejection counters");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = cudaStatus(cudaMemset(dMatchingRejectionCounts.ptr,
                                   0,
                                   sizeof(unsigned int) * kMatchingCounterCount),
                        "cudaMemset matching rejection counters");
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
    if (needsAuxiliaryColor) {
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
    if (leftQualitySignalsActive) {
        const std::size_t imageBytes = static_cast<std::size_t>(pixelCount);
        for (std::size_t step = 0; step < leftHighFrequencyQualityPlan.images.size(); ++step) {
            const ImageView& image = *leftHighFrequencyQualityPlan.images[step];
            if (image.data == nullptr || image.width != width || image.height != height ||
                image.channels != 1 || image.elementType != ImageElementType::UInt8 ||
                image.strideBytes < width) {
                result.status = {StatusCode::InputTypeUnsupported,
                                 "PointCloudReconstructorCuda",
                                 "quality modulation requires packed UInt8 high-frequency stripes"};
                result.stats.status = result.status;
                return result;
            }
            status = cudaStatus(cudaMemcpy2D(
                                    static_cast<unsigned char*>(dQualityInputImages.ptr) + step * imageBytes,
                                    static_cast<std::size_t>(width),
                                    image.data,
                                    static_cast<std::size_t>(image.strideBytes),
                                    static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height),
                                    cudaMemcpyHostToDevice),
                                "cudaMemcpy2D quality stripe H2D");
            if (!status.ok()) {
                result.status = status;
                result.stats.status = status;
                return result;
            }
        }
    }
    if (rightQualitySignalsActive) {
        const std::size_t imageBytes = static_cast<std::size_t>(pixelCount);
        for (std::size_t step = 0; step < rightHighFrequencyQualityPlan.images.size(); ++step) {
            const ImageView& image = *rightHighFrequencyQualityPlan.images[step];
            if (image.data == nullptr || image.width != width || image.height != height ||
                image.channels != 1 || image.elementType != ImageElementType::UInt8 ||
                image.strideBytes < width) {
                result.status = {StatusCode::InputTypeUnsupported,
                                 "PointCloudReconstructorCuda",
                                 "matching candidate quality requires packed UInt8 right high-frequency stripes"};
                result.stats.status = result.status;
                return result;
            }
            status = cudaStatus(cudaMemcpy2D(
                                    static_cast<unsigned char*>(dRightQualityInputImages.ptr) + step * imageBytes,
                                    static_cast<std::size_t>(width),
                                    image.data,
                                    static_cast<std::size_t>(image.strideBytes),
                                    static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height),
                                    cudaMemcpyHostToDevice),
                                "cudaMemcpy2D right quality stripe H2D");
            if (!status.ok()) {
                result.status = status;
                result.stats.status = status;
                return result;
            }
        }
    }
    hostToDeviceMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    const float* leftPhaseForMatching = dLeft.as<const float>();
    const float* rightPhaseForMatching = dRight.as<const float>();
    const bool rectificationAvailable = hasRectificationCalibration(calibration);
    RemapCalibration leftRemap {};
    RemapCalibration rightRemap {};
    if (rectificationAvailable) {
        leftRemap = makeRemapCalibration(calibration.leftIntrinsics,
                                         calibration.leftDistortion,
                                         calibration.rectificationLeft,
                                         calibration.projectionLeft);
        rightRemap = makeRemapCalibration(calibration.rightIntrinsics,
                                          calibration.rightDistortion,
                                          calibration.rectificationRight,
                                          calibration.projectionRight);
        // Production phase is already rectified before atan2/unwrap. Keep the
        // old post-phase remap only for direct sensor-domain callers and tests.
        if (unwrappedPhase.coordinateDomain == PhaseCoordinateDomain::Sensor) {
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
    }
    if (leftQualitySignalsActive) {
        QualityPhaseWeights weights;
        weights.stepCount = static_cast<int>(leftHighFrequencyQualityPlan.images.size());
        for (int step = 0; step < weights.stepCount; ++step) {
            weights.sinWeights[step] = leftHighFrequencyQualityPlan.sinWeights[static_cast<std::size_t>(step)];
            weights.cosWeights[step] = leftHighFrequencyQualityPlan.cosWeights[static_cast<std::size_t>(step)];
        }
        computeRectifiedHighFrequencyQualityKernel<<<grid, block>>>(
            dQualityInputImages.as<const unsigned char>(),
            dQualityModulation.as<float>(),
            dQualityLightFlags.as<unsigned char>(),
            width,
            height,
            leftRemap,
            weights,
            rectificationAvailable ? 1 : 0);
        status = cudaStatus(cudaGetLastError(), "computeRectifiedHighFrequencyQualityKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (rightQualitySignalsActive) {
        QualityPhaseWeights weights;
        weights.stepCount = static_cast<int>(rightHighFrequencyQualityPlan.images.size());
        for (int step = 0; step < weights.stepCount; ++step) {
            weights.sinWeights[step] = rightHighFrequencyQualityPlan.sinWeights[static_cast<std::size_t>(step)];
            weights.cosWeights[step] = rightHighFrequencyQualityPlan.cosWeights[static_cast<std::size_t>(step)];
        }
        computeRectifiedHighFrequencyQualityKernel<<<grid, block>>>(
            dRightQualityInputImages.as<const unsigned char>(),
            dRightQualityModulation.as<float>(),
            dRightQualityLightFlags.as<unsigned char>(),
            width,
            height,
            rightRemap,
            weights,
            rectificationAvailable ? 1 : 0);
        status = cudaStatus(cudaGetLastError(), "computeRectifiedHighFrequencyQualityKernel right launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
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
                                                 rectificationAvailable ? 1 : 0,
                                                 config.colorHighlightCompressionEnabled && !frame.metalScan ? 1 : 0);
        status = cudaStatus(cudaGetLastError(), "remapCorrectColorKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }

    const unsigned char* validMaskForMatching = nullptr;
    if (clear255Active) {
        buildClear255MaskKernel<<<grid, block>>>(dRawColor.as<const unsigned char>(),
                                                 dClear255Mask.as<unsigned char>(),
                                                 width,
                                                 height,
                                                 leftRemap,
                                                 rectificationAvailable ? 1 : 0);
        status = cudaStatus(cudaGetLastError(), "buildClear255MaskKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        validMaskForMatching = dClear255Mask.as<const unsigned char>();
        if (config.clear255DilateRadius > 0) {
            dilateInvalidMaskKernel<<<grid, block>>>(dClear255Mask.as<const unsigned char>(),
                                                     dClear255MaskScratch.as<unsigned char>(),
                                                     width,
                                                     height,
                                                     config.clear255DilateRadius);
            status = cudaStatus(cudaGetLastError(), "dilateInvalidMaskKernel launch");
            if (!status.ok()) {
                result.status = status;
                result.stats.status = status;
                return result;
            }
            validMaskForMatching = dClear255MaskScratch.as<const unsigned char>();
        }
        constexpr int maskCountBlockSize = 256;
        const int maskCountGridSize = (pixelCount + maskCountBlockSize - 1) / maskCountBlockSize;
        countInvalidMaskKernel<<<maskCountGridSize, maskCountBlockSize>>>(
            validMaskForMatching, dClear255RejectedCount.as<unsigned int>(), pixelCount);
        status = cudaStatus(cudaGetLastError(), "countInvalidMaskKernel launch");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }

    // Keep the Legacy disparity hot-path sampling contract here: phase maps
    // are fetched through 2D textures with linear filtering, clamp addressing,
    // and unnormalized coordinates. Gauge-block diagnostics showed that the
    // phase maps are already close; the remaining RMS gap is in matching.
    PhaseTexture2D leftPhaseTexture;
    status = leftPhaseTexture.create(leftPhaseForMatching, width, height, "cudaCreateTextureObject left phase");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    PhaseTexture2D rightPhaseTexture;
    status = rightPhaseTexture.create(rightPhaseForMatching, width, height, "cudaCreateTextureObject right phase");
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }

    const DisparityWindow disparityWindow = makeDisparityWindow(calibration, config, width);
    computeDisparityKernel<<<grid, block>>>(leftPhaseTexture.get(),
                                            rightPhaseTexture.get(),
                                            validMaskForMatching,
                                            matchingQualityFilterActive ? dQualityModulation.as<const float>() : nullptr,
                                            matchingQualityFilterActive ? dQualityLightFlags.as<const unsigned char>() : nullptr,
                                            matchingQualityFilterActive ? dRightQualityModulation.as<const float>() : nullptr,
                                            matchingQualityFilterActive ? dRightQualityLightFlags.as<const unsigned char>() : nullptr,
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
                                            config.matchingRejectOnSubpixelFailure ? 1 : 0,
                                            matchingQualityFilterActive ? 1 : 0,
                                            static_cast<float>(config.matchingCandidateMinModulation),
                                            config.matchingCandidateRejectSaturation ? 1 : 0,
                                            config.matchingCandidateRejectLowLight ? 1 : 0,
                                            materializeMatchingDiagnostics ? 1 : 0,
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
    std::vector<HostFloat3> rawStagePoints;
    std::vector<HostFloat3> filterInputStagePoints;
    std::vector<HostFloat3> filteredPoints;
    std::vector<HostFloat3> normals;
    std::vector<float> debugDisparity;
    std::vector<float> matchScores;
    std::vector<int> candidateCounts;
    std::vector<float> qualityModulation;
    std::vector<unsigned char> qualityLightFlags;
    std::vector<unsigned char> clear255Mask;
    std::vector<unsigned char> rectifiedColor;
    if (materializePointCloud) {
        filteredPoints.resize(static_cast<std::size_t>(pixelCount));
        normals.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeStageVertices) {
        rawStagePoints.resize(static_cast<std::size_t>(pixelCount));
        filterInputStagePoints.resize(static_cast<std::size_t>(pixelCount));
        debugDisparity.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeMatchingSignals) {
        matchScores.resize(static_cast<std::size_t>(pixelCount));
        candidateCounts.resize(static_cast<std::size_t>(pixelCount));
    }
    if (leftQualitySignalsActive) {
        qualityModulation.resize(static_cast<std::size_t>(pixelCount));
        qualityLightFlags.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeQualityGrid && clear255Active) {
        clear255Mask.resize(static_cast<std::size_t>(pixelCount));
    }
    if (materializeColor) {
        rectifiedColor.resize(colorBytes);
    }
    std::array<unsigned int, kMatchingCounterCount> matchingRejectionCounts = {};
    std::array<unsigned int, 3> pointCounts = {};
    hostAllocationMs = elapsedMilliseconds(timingStart);

    timingStart = SteadyClock::now();
    if (materializeStageVertices) {
        status = cudaStatus(cudaMemcpy(debugDisparity.data(), disparityForReproject, phaseBytes, cudaMemcpyDeviceToHost), "cudaMemcpy debug disparity D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemcpy(rawStagePoints.data(), dRawPoints.ptr, pointBytes, cudaMemcpyDeviceToHost), "cudaMemcpy raw stage points D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemcpy(filterInputStagePoints.data(), pointsBeforeFilter, pointBytes, cudaMemcpyDeviceToHost), "cudaMemcpy filter-input stage points D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
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
    if (materializeMatchingSignals) {
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
    if (leftQualitySignalsActive) {
        status = cudaStatus(cudaMemcpy(qualityModulation.data(),
                                       dQualityModulation.ptr,
                                       phaseBytes,
                                       cudaMemcpyDeviceToHost),
                            "cudaMemcpy quality modulation D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        status = cudaStatus(cudaMemcpy(qualityLightFlags.data(),
                                       dQualityLightFlags.ptr,
                                       qualityLightFlags.size(),
                                       cudaMemcpyDeviceToHost),
                            "cudaMemcpy quality light flags D2H");
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
    }
    if (!clear255Mask.empty()) {
        status = cudaStatus(cudaMemcpy(clear255Mask.data(),
                                       validMaskForMatching,
                                       clear255Mask.size(),
                                       cudaMemcpyDeviceToHost),
                            "cudaMemcpy clear255 semantic mask D2H");
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
    unsigned int clear255RejectedCount = 0U;
    if (clear255Active) {
        status = cudaStatus(cudaMemcpy(&clear255RejectedCount,
                                       dClear255RejectedCount.ptr,
                                       sizeof(clear255RejectedCount),
                                       cudaMemcpyDeviceToHost),
                            "cudaMemcpy clear255 rejection counter D2H");
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
    result.filterDeletedPointCount =
        result.smoothedGridValidPointCount > result.filteredGridValidPointCount
            ? result.smoothedGridValidPointCount - result.filteredGridValidPointCount
            : 0U;
    result.normalsComputed = computeNormals;
    result.leftRightRejectedPointCount = matchingRejectionCounts[kMatchingCounterLeftRightRejected];
    result.rightPhaseMonotonicRejectedPointCount =
        matchingRejectionCounts[kMatchingCounterRightPhaseMonotonicRejected];
    result.clear255RejectedPixelCount = clear255RejectedCount;
    result.semanticMaskApplied = clear255Active;
    result.matchingSummary =
        "matchingLeftRightRejected=" + std::to_string(result.leftRightRejectedPointCount) +
        ",matchingRightPhaseMonotonicRejected=" + std::to_string(result.rightPhaseMonotonicRejectedPointCount) +
        ",clear255RejectedPixels=" + std::to_string(result.clear255RejectedPixelCount) +
        ",matchingQualityFilterActive=" + std::string(matchingQualityFilterActive ? "true" : "false");

    if (materializeMatchingDiagnostics) {
        PointCloudMatchingDiagnostics diagnostics;
        diagnostics.enabled = true;
        diagnostics.leftPhaseValidPixelCount = matchingRejectionCounts[kMatchingCounterLeftPhaseValid];
        diagnostics.thresholdRejectedPixelCount = matchingRejectionCounts[kMatchingCounterThresholdRejected];
        diagnostics.uniquenessRejectedPixelCount = matchingRejectionCounts[kMatchingCounterUniquenessRejected];
        diagnostics.acceptedMatchPixelCount = matchingRejectionCounts[kMatchingCounterAccepted];
        diagnostics.leftRightRejectedPointCount = result.leftRightRejectedPointCount;
        diagnostics.rightPhaseMonotonicRejectedPointCount = result.rightPhaseMonotonicRejectedPointCount;
        diagnostics.subpixelSuccessCount = matchingRejectionCounts[kMatchingCounterSubpixelSuccess];
        diagnostics.subpixelFallbackCount = matchingRejectionCounts[kMatchingCounterSubpixelFallback];
        diagnostics.leftQualityRejectedPixelCount = matchingRejectionCounts[kMatchingCounterLeftQualityRejected];
        diagnostics.rightCandidateQualitySkippedCount = matchingRejectionCounts[kMatchingCounterRightQualitySkipped];
        diagnostics.subpixelFailureRejectedCount = matchingRejectionCounts[kMatchingCounterSubpixelFailureRejected];

        std::size_t candidateSum = 0U;
        std::size_t acceptedCostCount = 0U;
        double acceptedCostSum = 0.0;
        for (std::size_t idx = 0; idx < candidateCounts.size(); ++idx) {
            const int candidates = candidateCounts[idx];
            if (candidates > 0) {
                ++diagnostics.pixelsWithNearCandidates;
                candidateSum += static_cast<std::size_t>(candidates);
                diagnostics.maxNearCandidateCount = std::max(
                    diagnostics.maxNearCandidateCount, static_cast<std::size_t>(candidates));
                if (candidates > 1) {
                    ++diagnostics.ambiguousCandidatePixelCount;
                }
            }
            if (idx < matchScores.size() &&
                matchScores[idx] >= 0.0F &&
                matchScores[idx] < static_cast<float>(config.phaseDiffThreshold)) {
                ++acceptedCostCount;
                acceptedCostSum += static_cast<double>(matchScores[idx]);
                diagnostics.maxAcceptedMatchCost =
                    std::max(diagnostics.maxAcceptedMatchCost, static_cast<double>(matchScores[idx]));
            }
        }
        if (diagnostics.pixelsWithNearCandidates > 0U) {
            diagnostics.meanNearCandidateCount =
                static_cast<double>(candidateSum) /
                static_cast<double>(diagnostics.pixelsWithNearCandidates);
        }
        if (acceptedCostCount > 0U) {
            diagnostics.meanAcceptedMatchCost =
                acceptedCostSum / static_cast<double>(acceptedCostCount);
        }
        result.matchingDiagnostics = diagnostics;
        result.matchingDiagnosticsCsv = formatMatchingDiagnosticsCsv(diagnostics);
        result.matchingSummary +=
            ",matchingDiagnosticsEnabled=true" +
            std::string(",leftPhaseValid=") + std::to_string(diagnostics.leftPhaseValidPixelCount) +
            ",thresholdRejected=" + std::to_string(diagnostics.thresholdRejectedPixelCount) +
            ",uniquenessRejected=" + std::to_string(diagnostics.uniquenessRejectedPixelCount) +
            ",acceptedMatches=" + std::to_string(diagnostics.acceptedMatchPixelCount) +
            ",subpixelSuccess=" + std::to_string(diagnostics.subpixelSuccessCount) +
            ",subpixelFallback=" + std::to_string(diagnostics.subpixelFallbackCount) +
            ",leftQualityRejected=" + std::to_string(diagnostics.leftQualityRejectedPixelCount) +
            ",rightCandidateQualitySkipped=" + std::to_string(diagnostics.rightCandidateQualitySkippedCount) +
            ",subpixelFailureRejected=" + std::to_string(diagnostics.subpixelFailureRejectedCount);
    } else {
        result.matchingSummary += ",matchingDiagnosticsEnabled=false";
    }

    if (materializeQualityGrid) {
        result.gridPoints.assign(static_cast<std::size_t>(pixelCount), {});
    }
    if (materializeVertices) {
        result.vertices.reserve(result.filteredGridValidPointCount);
    }
    if (materializeStageVertices) {
        result.rawStageVertices.reserve(result.rawValidPointCount);
        result.filterInputStageVertices.reserve(result.smoothedGridValidPointCount);
        result.filterDeletedStageVertices.reserve(result.filterDeletedPointCount);
    }
    for (std::size_t idx = 0; idx < filteredPoints.size(); ++idx) {
        if (materializeStageVertices) {
            const bool rawValid = isValidPoint(rawStagePoints[idx]);
            const bool filterInputValid = isValidPoint(filterInputStagePoints[idx]);
            const bool filteredValid = isValidPoint(filteredPoints[idx]);
            if (rawValid) {
                result.rawStageVertices.push_back(
                    makeDiagnosticVertex(rawStagePoints[idx], idx, width, 192U, 192U, 192U));
            }
            if (filterInputValid) {
                result.filterInputStageVertices.push_back(
                    makeDiagnosticVertex(filterInputStagePoints[idx], idx, width, 0U, 112U, 192U));
            }
            if (filterInputValid && !filteredValid) {
                result.filterDeletedStageVertices.push_back(
                    makeDiagnosticVertex(filterInputStagePoints[idx], idx, width, 220U, 40U, 40U));
            }
        }
        if (materializeQualityGrid) {
            PointCloudGridPoint& gridPoint = result.gridPoints[idx];
            gridPoint.x = filteredPoints[idx].x;
            gridPoint.y = filteredPoints[idx].y;
            gridPoint.z = filteredPoints[idx].z;
            gridPoint.nx = normals[idx].x;
            gridPoint.ny = normals[idx].y;
            gridPoint.nz = normals[idx].z;
            gridPoint.matchCost = matchScores[idx];
            gridPoint.candidateCount = candidateCounts[idx];
            gridPoint.semanticBackground = !clear255Mask.empty() && clear255Mask[idx] == 0U;
            if (leftQualitySignalsActive) {
                gridPoint.modulation = qualityModulation[idx];
                gridPoint.highFrequencySaturated = (qualityLightFlags[idx] & 1U) != 0U;
                gridPoint.highFrequencyLowLight = (qualityLightFlags[idx] & (2U | 4U)) != 0U;
            } else {
                gridPoint.modulation = std::numeric_limits<float>::quiet_NaN();
                gridPoint.highFrequencyLowLight = true;
            }
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
    if (materializeStageVertices) {
        result.debugDisparity = std::move(debugDisparity);
        result.debugMatchScores = matchScores;
        result.debugCandidateCounts = candidateCounts;
        result.debugDepthMap.assign(static_cast<std::size_t>(pixelCount), 0.0F);
        for (std::size_t idx = 0; idx < filteredPoints.size(); ++idx) {
            result.debugDepthMap[idx] = isValidPoint(filteredPoints[idx]) ? filteredPoints[idx].z : 0.0F;
        }
    }
    materializeMs = elapsedMilliseconds(timingStart);
    result.pointCloudStageSummary =
        "rawValid=" + std::to_string(result.rawValidPointCount) +
        ",smoothedValid=" + std::to_string(result.smoothedGridValidPointCount) +
        ",filteredValid=" + std::to_string(result.filteredGridValidPointCount) +
        ",phaseDomain=" + std::string(
            unwrappedPhase.coordinateDomain == PhaseCoordinateDomain::Rectified ? "rectified" : "sensor") +
        ",smoothingApplied=" + std::string(config.pointCloudSmoothingEnabled ? "true" : "false") +
        ",filterApplied=" + std::string(enableFilter != 0 ? "true" : "false") +
        ",colorTextureApplied=" + std::string(materializeColor ? "true" : "false") +
        ",verticesMaterialized=" + std::string(materializeVertices ? "true" : "false") +
        ",qualityGridMaterialized=" + std::string(materializeQualityGrid ? "true" : "false") +
        ",matchingDiagnosticsMaterialized=" +
        std::string(materializeMatchingDiagnostics ? "true" : "false") +
        ",matchingQualityFilterActive=" + std::string(matchingQualityFilterActive ? "true" : "false") +
        ",stageVerticesMaterialized=" + std::string(materializeStageVertices ? "true" : "false") +
        ",filterDeletedValid=" + std::to_string(result.filterDeletedPointCount) +
        ",clear255Active=" + std::string(clear255Active ? "true" : "false") +
        ",clear255RejectedPixels=" + std::to_string(clear255RejectedCount) +
        ",deviceAllocationCount=" + std::to_string(deviceAllocationCount) +
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

PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame,
                                                         const PointCloudOutputOptions& outputOptions)
{
    PointCloudCudaWorkspace workspace;
    return reconstructPointCloudCuda(
        unwrappedPhase, calibration, config, frame, workspace, outputOptions);
}

} // namespace reconstruct_one_frame
