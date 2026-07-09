#include "phase/WrappedPhaseComputer.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace reconstruct_one_frame {

namespace {

constexpr float kPi = 3.14159265358979323846F;

__global__ void computeWrappedPhaseKernel(const unsigned char* const* steps,
                                          int stepCount,
                                          int pixelCount,
                                          int direction,
                                          float* phase,
                                          float* modulation)
{
    const int pixel = blockIdx.x * blockDim.x + threadIdx.x;
    if (pixel >= pixelCount) {
        return;
    }

    float sinSum = 0.0F;
    float cosSum = 0.0F;
    const float sign = direction == 0 ? 1.0F : static_cast<float>(direction);
    for (int step = 0; step < stepCount; ++step) {
        const float intensity = static_cast<float>(steps[step][pixel]);
        const float angle = sign * 2.0F * kPi * static_cast<float>(step) / static_cast<float>(stepCount);
        sinSum += intensity * sinf(angle);
        cosSum += intensity * cosf(angle);
    }
    phase[pixel] = atan2f(-sinSum, cosSum);
    modulation[pixel] = 2.0F * sqrtf(sinSum * sinSum + cosSum * cosSum) / static_cast<float>(stepCount);
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
};

Status cudaStatus(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return {};
    }
    return {StatusCode::CudaKernelFailed, "WrappedPhaseComputerCuda", std::string(operation) + ": " + cudaGetErrorString(error)};
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
                      WrappedPhaseFrequencyResult& output)
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
    output.modulation.assign(static_cast<std::size_t>(pixelCount), 0.0F);
    output.validPixelCount = static_cast<std::size_t>(pixelCount);

    std::vector<DeviceBuffer> deviceImages(static_cast<std::size_t>(requirement.requiredPhaseSteps));
    std::vector<const unsigned char*> deviceImagePtrs(static_cast<std::size_t>(requirement.requiredPhaseSteps), nullptr);
    for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
        status = cudaStatus(cudaMalloc(&deviceImages[static_cast<std::size_t>(step)].ptr, pixelBytes), "cudaMalloc image");
        if (!status.ok()) {
            return status;
        }
        status = cudaStatus(cudaMemcpy(deviceImages[static_cast<std::size_t>(step)].ptr,
                                       steps[static_cast<std::size_t>(step)]->image.data,
                                       pixelBytes,
                                       cudaMemcpyHostToDevice),
                            "cudaMemcpy image H2D");
        if (!status.ok()) {
            return status;
        }
        deviceImagePtrs[static_cast<std::size_t>(step)] =
            static_cast<const unsigned char*>(deviceImages[static_cast<std::size_t>(step)].ptr);
    }

    DeviceBuffer devicePtrs;
    status = cudaStatus(cudaMalloc(&devicePtrs.ptr, sizeof(unsigned char*) * deviceImagePtrs.size()), "cudaMalloc step pointer table");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(devicePtrs.ptr,
                                   deviceImagePtrs.data(),
                                   sizeof(unsigned char*) * deviceImagePtrs.size(),
                                   cudaMemcpyHostToDevice),
                        "cudaMemcpy step pointer table H2D");
    if (!status.ok()) {
        return status;
    }

    DeviceBuffer devicePhase;
    DeviceBuffer deviceModulation;
    const std::size_t outputBytes = sizeof(float) * static_cast<std::size_t>(pixelCount);
    status = cudaStatus(cudaMalloc(&devicePhase.ptr, outputBytes), "cudaMalloc phase");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMalloc(&deviceModulation.ptr, outputBytes), "cudaMalloc modulation");
    if (!status.ok()) {
        return status;
    }

    constexpr int blockSize = 256;
    const int gridSize = (pixelCount + blockSize - 1) / blockSize;
    computeWrappedPhaseKernel<<<gridSize, blockSize>>>(
        static_cast<const unsigned char* const*>(devicePtrs.ptr),
        requirement.requiredPhaseSteps,
        pixelCount,
        requirement.phaseStepDirection,
        static_cast<float*>(devicePhase.ptr),
        static_cast<float*>(deviceModulation.ptr));
    status = cudaStatus(cudaGetLastError(), "computeWrappedPhaseKernel launch");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaDeviceSynchronize(), "computeWrappedPhaseKernel synchronize");
    if (!status.ok()) {
        return status;
    }

    status = cudaStatus(cudaMemcpy(output.phase.data(), devicePhase.ptr, outputBytes, cudaMemcpyDeviceToHost), "cudaMemcpy phase D2H");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(output.modulation.data(), deviceModulation.ptr, outputBytes, cudaMemcpyDeviceToHost), "cudaMemcpy modulation D2H");
    if (!status.ok()) {
        return status;
    }

    double modulationSum = 0.0;
    double minModulation = std::numeric_limits<double>::infinity();
    double maxModulation = 0.0;
    for (float value : output.modulation) {
        const double modulation = static_cast<double>(value);
        modulationSum += modulation;
        minModulation = std::min(minModulation, modulation);
        maxModulation = std::max(maxModulation, modulation);
    }
    output.meanModulation = output.modulation.empty() ? 0.0 : modulationSum / static_cast<double>(output.modulation.size());
    output.minModulation = output.modulation.empty() ? 0.0 : minModulation;
    output.maxModulation = maxModulation;
    return {};
}

} // namespace

WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const WrappedPhaseOptions& options)
{
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

    for (const StripeRequirement& requirement : config.stripeRequirements) {
        WrappedPhaseFrequencyResult left;
        Status status = computeOneCuda(CameraSide::Left, frame.leftStripes, requirement, left);
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }

        WrappedPhaseFrequencyResult right;
        status = computeOneCuda(CameraSide::Right, frame.rightStripes, requirement, right);
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

} // namespace reconstruct_one_frame
