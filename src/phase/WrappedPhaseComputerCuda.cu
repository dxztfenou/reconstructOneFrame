#include "phase/WrappedPhaseComputer.h"

#include "calibration_model/CudaRectification.cuh"

#include <cuda_runtime.h>
#include <math_constants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace reconstruct_one_frame {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr int kWrappedPhaseBlockSize = 256;

using cuda_rectification::RemapCalibration;
using cuda_rectification::hasRectificationCalibration;
using cuda_rectification::makeRemapCalibration;
using cuda_rectification::rectifiedToRawPixel;

__device__ float sampleBilinear(const unsigned char* image,
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

__global__ void computeWrappedPhaseKernel(const unsigned char* const* steps,
                                          int stepCount,
                                          int width,
                                          int height,
                                          int pixelCount,
                                          int direction,
                                          int bMin,
                                          RemapCalibration remap,
                                          int useRectification,
                                          float* phase,
                                          float* modulation,
                                          float* modulationStats)
{
    const int pixel = blockIdx.x * blockDim.x + threadIdx.x;
    float modulationValue = 0.0F;
    if (pixel < pixelCount) {
        float sinSum = 0.0F;
        float cosSum = 0.0F;
        // Rectify each intensity sample before atan2/fringe-order computation.
        // Remapping the final phase is mathematically different at phase wraps.
        float sourceX = static_cast<float>(pixel % width);
        float sourceY = static_cast<float>(pixel / width);
        bool valid = useRectification == 0 ||
            rectifiedToRawPixel(pixel % width, pixel / width, remap, sourceX, sourceY);
        const float sign = direction == 0 ? 1.0F : static_cast<float>(direction);
        for (int step = 0; step < stepCount; ++step) {
            const float intensity = useRectification != 0
                ? sampleBilinear(steps[step], width, height, sourceX, sourceY)
                : static_cast<float>(steps[step][pixel]);
            if (!isfinite(intensity)) {
                valid = false;
                break;
            }
            const float angle = sign * 2.0F * kPi * static_cast<float>(step) / static_cast<float>(stepCount);
            sinSum += intensity * sinf(angle);
            cosSum += intensity * cosf(angle);
        }
        if (valid) {
            modulationValue = 2.0F * sqrtf(sinSum * sinSum + cosSum * cosSum) / static_cast<float>(stepCount);
            phase[pixel] = (bMin != -1 && modulationValue < static_cast<float>(bMin))
                ? CUDART_NAN_F
                : atan2f(-sinSum, cosSum);
            modulation[pixel] = modulationValue;
        } else {
            phase[pixel] = CUDART_NAN_F;
            modulation[pixel] = 0.0F;
        }
    }

    __shared__ float blockSum[kWrappedPhaseBlockSize];
    __shared__ float blockMin[kWrappedPhaseBlockSize];
    __shared__ float blockMax[kWrappedPhaseBlockSize];
    blockSum[threadIdx.x] = pixel < pixelCount ? modulationValue : 0.0F;
    blockMin[threadIdx.x] = pixel < pixelCount ? modulationValue : CUDART_INF_F;
    blockMax[threadIdx.x] = pixel < pixelCount ? modulationValue : 0.0F;
    __syncthreads();

    for (int stride = blockDim.x / 2; stride > 0; stride /= 2) {
        if (threadIdx.x < stride) {
            blockSum[threadIdx.x] += blockSum[threadIdx.x + stride];
            blockMin[threadIdx.x] = fminf(blockMin[threadIdx.x], blockMin[threadIdx.x + stride]);
            blockMax[threadIdx.x] = fmaxf(blockMax[threadIdx.x], blockMax[threadIdx.x + stride]);
        }
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        atomicAdd(&modulationStats[0], blockSum[0]);
        atomicMin(reinterpret_cast<int*>(&modulationStats[1]), __float_as_int(blockMin[0]));
        atomicMax(reinterpret_cast<int*>(&modulationStats[2]), __float_as_int(blockMax[0]));
    }
}

struct DeviceBuffer {
    void* ptr = nullptr;
    std::size_t capacity = 0;

    DeviceBuffer() = default;
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    DeviceBuffer(DeviceBuffer&& other) noexcept
        : ptr(other.ptr),
          capacity(other.capacity)
    {
        other.ptr = nullptr;
        other.capacity = 0;
    }

    ~DeviceBuffer()
    {
        if (ptr != nullptr) {
            cudaFree(ptr);
        }
    }
};

struct WrappedPhaseWorkspace {
    std::vector<DeviceBuffer> images;
    DeviceBuffer pointerTable;
    DeviceBuffer phase;
    DeviceBuffer modulation;
    DeviceBuffer modulationStats;
};

Status cudaStatus(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return {};
    }
    return {StatusCode::CudaKernelFailed, "WrappedPhaseComputerCuda", std::string(operation) + ": " + cudaGetErrorString(error)};
}

Status ensureCapacity(DeviceBuffer& buffer, std::size_t bytes, const char* operation)
{
    if (buffer.ptr != nullptr && buffer.capacity >= bytes) {
        return {};
    }
    if (buffer.ptr != nullptr) {
        const Status freeStatus = cudaStatus(cudaFree(buffer.ptr), "cudaFree undersized wrapped phase buffer");
        buffer.ptr = nullptr;
        buffer.capacity = 0;
        if (!freeStatus.ok()) {
            return freeStatus;
        }
    }
    Status status = cudaStatus(cudaMalloc(&buffer.ptr, bytes), operation);
    if (status.ok()) {
        buffer.capacity = bytes;
    }
    return status;
}

std::vector<const StripeImage*> collectFrequencySteps(const std::vector<StripeImage>& images,
                                                      int frequencyIndex,
                                                      int requiredSteps)
{
    std::vector<const StripeImage*> steps(static_cast<std::size_t>(requiredSteps), nullptr);
    for (const StripeImage& image : images) {
        if (image.frequencyIndex == frequencyIndex &&
            image.phaseStepIndex >= 0 &&
            image.phaseStepIndex < requiredSteps) {
            steps[static_cast<std::size_t>(image.phaseStepIndex)] = &image;
        }
    }
    return steps;
}

Status ensureCudaCompatible(const std::vector<const StripeImage*>& steps)
{
    for (const StripeImage* step : steps) {
        if (step == nullptr) {
            return {StatusCode::InputPhaseStepMissing, "WrappedPhaseComputerCuda", "phase step missing before CUDA wrapped phase compute"};
        }
        if (step->image.elementType != ImageElementType::UInt8 || step->image.channels != 1) {
            return {StatusCode::InputTypeUnsupported, "WrappedPhaseComputerCuda", "CUDA phase-3 path currently supports uint8 single-channel stripes"};
        }
        if (step->image.strideBytes != step->image.width) {
            return {StatusCode::InputStrideInvalid, "WrappedPhaseComputerCuda", "CUDA phase-3 path currently requires packed uint8 rows"};
        }
    }
    return {};
}

Status computeOneCuda(CameraSide camera,
                      const std::vector<StripeImage>& images,
                      const StripeRequirement& requirement,
                      int bMin,
                      const RemapCalibration* remap,
                      WrappedPhaseFrequencyResult& output,
                      WrappedPhaseWorkspace& workspace,
                      bool materializeModulation)
{
    const std::vector<const StripeImage*> steps =
        collectFrequencySteps(images, requirement.frequencyIndex, requirement.requiredPhaseSteps);
    Status status = ensureCudaCompatible(steps);
    if (!status.ok()) {
        return status;
    }

    const ImageView& first = steps.front()->image;
    const int pixelCount = first.width * first.height * first.channels;
    const std::size_t pixelBytes = static_cast<std::size_t>(pixelCount);
    output.camera = camera;
    output.frequencyIndex = requirement.frequencyIndex;
    output.frequencyValue = requirement.frequencyValue;
    output.width = first.width;
    output.height = first.height;
    output.phase.assign(static_cast<std::size_t>(pixelCount), 0.0F);
    if (materializeModulation) {
        output.modulation.assign(static_cast<std::size_t>(pixelCount), 0.0F);
    } else {
        output.modulation.clear();
    }
    output.validPixelCount = static_cast<std::size_t>(pixelCount);

    workspace.images.resize(static_cast<std::size_t>(requirement.requiredPhaseSteps));
    std::vector<const unsigned char*> deviceImagePtrs(static_cast<std::size_t>(requirement.requiredPhaseSteps), nullptr);
    for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
        DeviceBuffer& deviceImage = workspace.images[static_cast<std::size_t>(step)];
        status = ensureCapacity(deviceImage, pixelBytes, "cudaMalloc image");
        if (!status.ok()) {
            return status;
        }
        status = cudaStatus(cudaMemcpy(deviceImage.ptr,
                                       steps[static_cast<std::size_t>(step)]->image.data,
                                       pixelBytes,
                                       cudaMemcpyHostToDevice),
                            "cudaMemcpy image H2D");
        if (!status.ok()) {
            return status;
        }
        deviceImagePtrs[static_cast<std::size_t>(step)] =
            static_cast<const unsigned char*>(deviceImage.ptr);
    }

    const std::size_t pointerTableBytes = sizeof(unsigned char*) * deviceImagePtrs.size();
    status = ensureCapacity(workspace.pointerTable, pointerTableBytes, "cudaMalloc step pointer table");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(workspace.pointerTable.ptr,
                                   deviceImagePtrs.data(),
                                   pointerTableBytes,
                                   cudaMemcpyHostToDevice),
                        "cudaMemcpy step pointer table H2D");
    if (!status.ok()) {
        return status;
    }

    const std::size_t outputBytes = sizeof(float) * static_cast<std::size_t>(pixelCount);
    status = ensureCapacity(workspace.phase, outputBytes, "cudaMalloc phase");
    if (!status.ok()) {
        return status;
    }
    status = ensureCapacity(workspace.modulation, outputBytes, "cudaMalloc modulation");
    if (!status.ok()) {
        return status;
    }
    status = ensureCapacity(workspace.modulationStats, sizeof(float) * 3, "cudaMalloc modulation stats");
    if (!status.ok()) {
        return status;
    }

    std::array<float, 3> modulationStats = {
        0.0F,
        std::numeric_limits<float>::infinity(),
        0.0F
    };
    status = cudaStatus(cudaMemcpy(workspace.modulationStats.ptr,
                                   modulationStats.data(),
                                   sizeof(float) * modulationStats.size(),
                                   cudaMemcpyHostToDevice),
                        "cudaMemcpy modulation stats H2D");
    if (!status.ok()) {
        return status;
    }

    const int gridSize = (pixelCount + kWrappedPhaseBlockSize - 1) / kWrappedPhaseBlockSize;
    const RemapCalibration remapValue = remap == nullptr ? RemapCalibration{} : *remap;
    computeWrappedPhaseKernel<<<gridSize, kWrappedPhaseBlockSize>>>(
        static_cast<const unsigned char* const*>(workspace.pointerTable.ptr),
        requirement.requiredPhaseSteps,
        first.width,
        first.height,
        pixelCount,
        requirement.phaseStepDirection,
        bMin,
        remapValue,
        remap == nullptr ? 0 : 1,
        static_cast<float*>(workspace.phase.ptr),
        static_cast<float*>(workspace.modulation.ptr),
        static_cast<float*>(workspace.modulationStats.ptr));
    status = cudaStatus(cudaGetLastError(), "computeWrappedPhaseKernel launch");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaDeviceSynchronize(), "computeWrappedPhaseKernel synchronize");
    if (!status.ok()) {
        return status;
    }

    status = cudaStatus(cudaMemcpy(output.phase.data(), workspace.phase.ptr, outputBytes, cudaMemcpyDeviceToHost), "cudaMemcpy phase D2H");
    if (!status.ok()) {
        return status;
    }
    output.validPixelCount = static_cast<std::size_t>(
        std::count_if(output.phase.begin(), output.phase.end(), [](float phase) {
            return std::isfinite(phase);
        }));
    if (materializeModulation) {
        status = cudaStatus(cudaMemcpy(output.modulation.data(), workspace.modulation.ptr, outputBytes, cudaMemcpyDeviceToHost), "cudaMemcpy modulation D2H");
        if (!status.ok()) {
            return status;
        }
    }
    status = cudaStatus(cudaMemcpy(modulationStats.data(),
                                   workspace.modulationStats.ptr,
                                   sizeof(float) * modulationStats.size(),
                                   cudaMemcpyDeviceToHost),
                        "cudaMemcpy modulation stats D2H");
    if (!status.ok()) {
        return status;
    }
    output.meanModulation = static_cast<double>(modulationStats[0]) / static_cast<double>(pixelCount);
    output.minModulation = modulationStats[1];
    output.maxModulation = modulationStats[2];
    return {};
}

} // namespace

struct WrappedPhaseCudaWorkspace::Impl {
    WrappedPhaseWorkspace workspace;
};

WrappedPhaseCudaWorkspace::WrappedPhaseCudaWorkspace()
    : impl_(std::make_unique<Impl>())
{
}

WrappedPhaseCudaWorkspace::~WrappedPhaseCudaWorkspace() = default;
WrappedPhaseCudaWorkspace::WrappedPhaseCudaWorkspace(WrappedPhaseCudaWorkspace&&) noexcept = default;
WrappedPhaseCudaWorkspace& WrappedPhaseCudaWorkspace::operator=(WrappedPhaseCudaWorkspace&&) noexcept = default;

void WrappedPhaseCudaWorkspace::reset() noexcept
{
    impl_.reset();
}

WrappedPhaseResult computeWrappedPhaseCudaImpl(const StripeFrameGroup& frame,
                                               const ReconsConfig& config,
                                               const CalibrationModel* calibration,
                                               WrappedPhaseCudaWorkspace& workspaceHandle,
                                               const WrappedPhaseOptions& options)
{
    if (!workspaceHandle.impl_) {
        workspaceHandle.impl_ = std::make_unique<WrappedPhaseCudaWorkspace::Impl>();
    }
    WrappedPhaseWorkspace& workspace = workspaceHandle.impl_->workspace;
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    if (error != cudaSuccess || deviceCount <= 0) {
        WrappedPhaseResult result;
        result.stats.stageName = "wrapped_phase_compute_cuda";
        result.stats.inputImageCount = frame.leftStripes.size() + frame.rightStripes.size();
        result.status = {StatusCode::CudaInitFailed, "WrappedPhaseComputerCuda", cudaGetErrorString(error)};
        result.stats.status = result.status;
        return result;
    }

    WrappedPhaseResult result;
    result.stats.stageName = "wrapped_phase_compute_cuda";
    result.stats.inputImageCount = frame.leftStripes.size() + frame.rightStripes.size();
    // Partial calibration stays in the sensor-domain compatibility path; only
    // a complete stereo contract may label the produced phase as Rectified.
    const bool rectificationAvailable = calibration != nullptr && hasRectificationCalibration(*calibration);
    RemapCalibration leftRemap;
    RemapCalibration rightRemap;
    if (rectificationAvailable) {
        leftRemap = makeRemapCalibration(calibration->leftIntrinsics,
                                         calibration->leftDistortion,
                                         calibration->rectificationLeft,
                                         calibration->projectionLeft);
        rightRemap = makeRemapCalibration(calibration->rightIntrinsics,
                                          calibration->rightDistortion,
                                          calibration->rectificationRight,
                                          calibration->projectionRight);
        result.coordinateDomain = PhaseCoordinateDomain::Rectified;
    }

    for (const StripeRequirement& requirement : config.stripeRequirements) {
        WrappedPhaseFrequencyResult left;
        Status status = computeOneCuda(CameraSide::Left,
                                        frame.leftStripes,
                                        requirement,
                                        config.bMin,
                                        rectificationAvailable ? &leftRemap : nullptr,
                                       left,
                                       workspace,
                                       options.materializeModulation);
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }

        WrappedPhaseFrequencyResult right;
        status = computeOneCuda(CameraSide::Right,
                                 frame.rightStripes,
                                 requirement,
                                 config.bMin,
                                 rectificationAvailable ? &rightRemap : nullptr,
                                right,
                                workspace,
                                options.materializeModulation);
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }

        result.stats.validImageCount += static_cast<std::size_t>(requirement.requiredPhaseSteps) * 2;
        result.stats.checkedPixels += left.phase.size() + right.phase.size();
        result.stats.cudaComputedPixels += left.phase.size() + right.phase.size();
        result.stats.meanPixelValue += left.meanModulation + right.meanModulation;
        result.frequencies.push_back(std::move(left));
        result.frequencies.push_back(std::move(right));
    }

    if (!result.frequencies.empty()) {
        result.stats.meanPixelValue /= static_cast<double>(result.frequencies.size());
    }
    if (result.stats.meanPixelValue < options.minMeanModulation) {
        result.status = {StatusCode::PhaseQualityInsufficient, "WrappedPhaseComputerCuda", "CUDA wrapped phase mean modulation is below threshold"};
        result.stats.status = result.status;
        return result;
    }

    result.status = {};
    result.stats.status = {};
    return result;
}

WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           WrappedPhaseCudaWorkspace& workspace,
                                           const WrappedPhaseOptions& options)
{
    return computeWrappedPhaseCudaImpl(frame, config, nullptr, workspace, options);
}

WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const CalibrationModel& calibration,
                                           WrappedPhaseCudaWorkspace& workspace,
                                           const WrappedPhaseOptions& options)
{
    return computeWrappedPhaseCudaImpl(frame, config, &calibration, workspace, options);
}

WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const WrappedPhaseOptions& options)
{
    WrappedPhaseCudaWorkspace workspace;
    return computeWrappedPhaseCuda(frame, config, workspace, options);
}

WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const CalibrationModel& calibration,
                                           const WrappedPhaseOptions& options)
{
    WrappedPhaseCudaWorkspace workspace;
    return computeWrappedPhaseCuda(frame, config, calibration, workspace, options);
}

} // namespace reconstruct_one_frame
