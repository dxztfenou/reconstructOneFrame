#include "phase/PhaseUnwrapper.h"

#include <cuda_runtime.h>
#include <math_constants.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

namespace {

constexpr float kTwoPi = 6.28318530717958647692F;

struct DeviceBuffer {
    void* ptr = nullptr;
    std::size_t capacity = 0;

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

struct PhaseUnwrapWorkspace {
    DeviceBuffer phi0;
    DeviceBuffer phi1;
    DeviceBuffer phi2;
    DeviceBuffer ph12;
    DeviceBuffer ph23;
    DeviceBuffer ph123;
    DeviceBuffer abs23;
    DeviceBuffer absolute;
    DeviceBuffer filtered;
};

Status cudaStatus(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return {};
    }
    return {StatusCode::CudaKernelFailed, "PhaseUnwrapperCuda", std::string(operation) + ": " + cudaGetErrorString(error)};
}

Status ensureCapacity(DeviceBuffer& buffer, std::size_t bytes)
{
    if (buffer.ptr != nullptr && buffer.capacity >= bytes) {
        return {};
    }
    if (buffer.ptr != nullptr) {
        const Status freeStatus = cudaStatus(cudaFree(buffer.ptr), "cudaFree undersized phase unwrap buffer");
        buffer.ptr = nullptr;
        buffer.capacity = 0;
        if (!freeStatus.ok()) {
            return freeStatus;
        }
    }
    Status status = cudaStatus(cudaMalloc(&buffer.ptr, bytes), "cudaMalloc phase unwrap buffer");
    if (status.ok()) {
        buffer.capacity = bytes;
    }
    return status;
}

__global__ void computePhaseDiffKernel(const float* first,
                                       const float* second,
                                       float* diffOutput,
                                       int pixelCount)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }
    if (!isfinite(first[idx]) || !isfinite(second[idx])) {
        diffOutput[idx] = CUDART_NAN_F;
        return;
    }

    const float diff = second[idx] - first[idx];
    diffOutput[idx] = diff < 0.0F ? diff + kTwoPi : diff;
}

__global__ void computeAbsPhaseKernel(const float* shortWrapped,
                                      const float* baseWrapped,
                                      float rate,
                                      int useResidualGate,
                                      float maxRoundResidual,
                                      float* absolutePhase,
                                      int pixelCount)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }
    if (!isfinite(shortWrapped[idx]) || !isfinite(baseWrapped[idx])) {
        absolutePhase[idx] = CUDART_NAN_F;
        return;
    }

    const float value = (baseWrapped[idx] * rate - shortWrapped[idx]) / kTwoPi;
    const float rounded = roundf(value);
    if (useResidualGate != 0 && fabsf(value - rounded) > maxRoundResidual) {
        absolutePhase[idx] = CUDART_NAN_F;
        return;
    }
    absolutePhase[idx] = shortWrapped[idx] + kTwoPi * rounded;
}

__global__ void copyKernel(const float* input, float* output, int pixelCount)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < pixelCount) {
        output[idx] = input[idx];
    }
}

__device__ void swapValues(float& a, float& b)
{
    const float t = a;
    a = b;
    b = t;
}

__device__ void sortSmallWindow(float* values, int count)
{
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            const bool aNan = !isfinite(values[i]);
            const bool bNan = !isfinite(values[j]);
            if (aNan || (!bNan && values[i] > values[j])) {
                swapValues(values[i], values[j]);
            }
        }
    }
}

__global__ void medianFilterKernel(const float* input,
                                   float* output,
                                   int width,
                                   int height,
                                   int kernelSize)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }

    const int radius = kernelSize / 2;
    const int idx = y * width + x;
    if (kernelSize <= 1 || x < radius || x >= width - radius || y < radius || y >= height - radius) {
        output[idx] = input[idx];
        return;
    }

    float values[225];
    int count = 0;
    for (int ky = -radius; ky <= radius; ++ky) {
        for (int kx = -radius; kx <= radius; ++kx) {
            if (count < 225) {
                values[count++] = input[(y + ky) * width + (x + kx)];
            }
        }
    }

    sortSmallWindow(values, count);
    output[idx] = values[count / 2];
}

const WrappedPhaseFrequencyResult* findWrapped(const WrappedPhaseResult& wrappedPhase,
                                               CameraSide camera,
                                               int frequencyIndex)
{
    for (const WrappedPhaseFrequencyResult& frequency : wrappedPhase.frequencies) {
        if (frequency.camera == camera && frequency.frequencyIndex == frequencyIndex) {
            return &frequency;
        }
    }
    return nullptr;
}

Status requireThreeFrequencies(const WrappedPhaseResult& wrappedPhase,
                               CameraSide camera,
                               const WrappedPhaseFrequencyResult*& phi0,
                               const WrappedPhaseFrequencyResult*& phi1,
                               const WrappedPhaseFrequencyResult*& phi2)
{
    phi0 = findWrapped(wrappedPhase, camera, 0);
    phi1 = findWrapped(wrappedPhase, camera, 1);
    phi2 = findWrapped(wrappedPhase, camera, 2);
    if (phi0 == nullptr || phi1 == nullptr || phi2 == nullptr) {
        return {StatusCode::UnwrapFailed, "PhaseUnwrapperCuda", "legacy two-stage CUDA unwrap requires three wrapped frequencies"};
    }
    if (phi0->width != phi1->width || phi0->width != phi2->width ||
        phi0->height != phi1->height || phi0->height != phi2->height ||
        phi0->phase.size() != phi1->phase.size() || phi0->phase.size() != phi2->phase.size()) {
        return {StatusCode::UnwrapFailed, "PhaseUnwrapperCuda", "wrapped phase frequency dimensions do not match"};
    }
    return {};
}

void summarizePhase(UnwrappedPhaseCameraResult& output)
{
    output.validPixelCount = 0;
    output.invalidPixelCount = 0;
    output.minPhase = std::numeric_limits<double>::infinity();
    output.maxPhase = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (float value : output.absolutePhase) {
        if (!std::isfinite(value)) {
            ++output.invalidPixelCount;
            continue;
        }
        const double phase = static_cast<double>(value);
        ++output.validPixelCount;
        sum += phase;
        output.minPhase = std::min(output.minPhase, phase);
        output.maxPhase = std::max(output.maxPhase, phase);
    }
    if (output.validPixelCount == 0) {
        output.minPhase = 0.0;
        output.maxPhase = 0.0;
        output.meanPhase = 0.0;
        return;
    }
    output.meanPhase = sum / static_cast<double>(output.validPixelCount);
}

Status computeOneCameraCuda(const WrappedPhaseResult& wrappedPhase,
                            const ReconsConfig& config,
                            CameraSide camera,
                            UnwrappedPhaseCameraResult& output)
{
    const WrappedPhaseFrequencyResult* phi0 = nullptr;
    const WrappedPhaseFrequencyResult* phi1 = nullptr;
    const WrappedPhaseFrequencyResult* phi2 = nullptr;
    Status status = requireThreeFrequencies(wrappedPhase, camera, phi0, phi1, phi2);
    if (!status.ok()) {
        return status;
    }
    if (config.freqSeries.size() < 3 || config.freq23 <= 0) {
        return {StatusCode::ConfigInvalidValue, "PhaseUnwrapperCuda", "freqSeries[2] and freq23 are required for two-stage unwrap"};
    }

    const int width = phi2->width;
    const int height = phi2->height;
    const int pixelCount = width * height;
    const std::size_t bytes = sizeof(float) * static_cast<std::size_t>(pixelCount);

    output.camera = camera;
    output.width = width;
    output.height = height;
    output.absolutePhase.assign(static_cast<std::size_t>(pixelCount), std::numeric_limits<float>::quiet_NaN());

    thread_local PhaseUnwrapWorkspace workspace;
    DeviceBuffer& dPhi0 = workspace.phi0;
    DeviceBuffer& dPhi1 = workspace.phi1;
    DeviceBuffer& dPhi2 = workspace.phi2;
    DeviceBuffer& dPh12 = workspace.ph12;
    DeviceBuffer& dPh23 = workspace.ph23;
    DeviceBuffer& dPh123 = workspace.ph123;
    DeviceBuffer& dAbs23 = workspace.abs23;
    DeviceBuffer& dAbs = workspace.absolute;
    DeviceBuffer& dAbsFiltered = workspace.filtered;

    for (DeviceBuffer* buffer : {&dPhi0, &dPhi1, &dPhi2, &dPh12, &dPh23, &dPh123, &dAbs23, &dAbs, &dAbsFiltered}) {
        status = ensureCapacity(*buffer, bytes);
        if (!status.ok()) {
            return status;
        }
    }

    status = cudaStatus(cudaMemcpy(dPhi0.ptr, phi0->phase.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy phi0 H2D");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(dPhi1.ptr, phi1->phase.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy phi1 H2D");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(dPhi2.ptr, phi2->phase.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy phi2 H2D");
    if (!status.ok()) {
        return status;
    }

    constexpr int blockSize = 256;
    const int gridSize = (pixelCount + blockSize - 1) / blockSize;
    computePhaseDiffKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dPhi0.ptr),
                                                    static_cast<const float*>(dPhi1.ptr),
                                                    static_cast<float*>(dPh12.ptr),
                                                    pixelCount);
    status = cudaStatus(cudaGetLastError(), "computePhaseDiffKernel PH12 launch");
    if (!status.ok()) {
        return status;
    }
    computePhaseDiffKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dPhi1.ptr),
                                                    static_cast<const float*>(dPhi2.ptr),
                                                    static_cast<float*>(dPh23.ptr),
                                                    pixelCount);
    status = cudaStatus(cudaGetLastError(), "computePhaseDiffKernel PH23 launch");
    if (!status.ok()) {
        return status;
    }
    computePhaseDiffKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dPh12.ptr),
                                                    static_cast<const float*>(dPh23.ptr),
                                                    static_cast<float*>(dPh123.ptr),
                                                    pixelCount);
    status = cudaStatus(cudaGetLastError(), "computePhaseDiffKernel PH123 launch");
    if (!status.ok()) {
        return status;
    }

    const int useResidualGate = config.phaseUnwrapResidualGateEnabled ? 1 : 0;
    computeAbsPhaseKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dPh23.ptr),
                                                   static_cast<const float*>(dPh123.ptr),
                                                   static_cast<float>(config.freq23),
                                                   useResidualGate,
                                                   static_cast<float>(config.phaseUnwrapAbs23ResidualThreshold),
                                                   static_cast<float*>(dAbs23.ptr),
                                                   pixelCount);
    status = cudaStatus(cudaGetLastError(), "computeAbsPhaseKernel abs23 launch");
    if (!status.ok()) {
        return status;
    }

    const float finalRate = static_cast<float>(config.freqSeries[2]) / static_cast<float>(config.freq23);
    computeAbsPhaseKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dPhi2.ptr),
                                                   static_cast<const float*>(dAbs23.ptr),
                                                   finalRate,
                                                   useResidualGate,
                                                   static_cast<float>(config.phaseUnwrapFinalResidualThreshold),
                                                   static_cast<float*>(dAbs.ptr),
                                                   pixelCount);
    status = cudaStatus(cudaGetLastError(), "computeAbsPhaseKernel final launch");
    if (!status.ok()) {
        return status;
    }

    const float* finalDevice = static_cast<const float*>(dAbs.ptr);
    if (config.phaseFinalMedianFilterEnabled && config.medianKernelSize > 1 && config.medianKernelSize <= 15) {
        dim3 block(16, 16);
        dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
        medianFilterKernel<<<grid, block>>>(static_cast<const float*>(dAbs.ptr),
                                            static_cast<float*>(dAbsFiltered.ptr),
                                            width,
                                            height,
                                            config.medianKernelSize);
        status = cudaStatus(cudaGetLastError(), "medianFilterKernel final launch");
        if (!status.ok()) {
            return status;
        }
        finalDevice = static_cast<const float*>(dAbsFiltered.ptr);
    } else {
        copyKernel<<<gridSize, blockSize>>>(static_cast<const float*>(dAbs.ptr),
                                            static_cast<float*>(dAbsFiltered.ptr),
                                            pixelCount);
        status = cudaStatus(cudaGetLastError(), "copyKernel final launch");
        if (!status.ok()) {
            return status;
        }
        finalDevice = static_cast<const float*>(dAbsFiltered.ptr);
    }

    status = cudaStatus(cudaDeviceSynchronize(), "phase unwrap kernels synchronize");
    if (!status.ok()) {
        return status;
    }
    status = cudaStatus(cudaMemcpy(output.absolutePhase.data(), finalDevice, bytes, cudaMemcpyDeviceToHost), "cudaMemcpy absolute phase D2H");
    if (!status.ok()) {
        return status;
    }

    summarizePhase(output);
    if (output.validPixelCount == 0) {
        return {StatusCode::UnwrapFailed, "PhaseUnwrapperCuda", "CUDA unwrap produced zero valid absolute-phase pixels"};
    }
    return {};
}

} // namespace

UnwrappedPhaseResult computeUnwrappedPhaseCuda(const WrappedPhaseResult& wrappedPhase,
                                               const ReconsConfig& config,
                                               const PhaseUnwrapOptions&)
{
    UnwrappedPhaseResult result;
    result.stats.stageName = "phase_unwrap_cuda";
    result.stats.inputImageCount = wrappedPhase.stats.validImageCount;

    if (!wrappedPhase.status.ok()) {
        result.status = wrappedPhase.status;
        result.stats.status = wrappedPhase.status;
        return result;
    }
    if (config.frequencyCount < 3 || config.stripeRequirements.size() < 3) {
        result.status = {StatusCode::UnwrapFailed, "PhaseUnwrapperCuda", "phase-4 CUDA unwrap requires at least three frequencies"};
        result.stats.status = result.status;
        return result;
    }

    int deviceCount = 0;
    const cudaError_t error = cudaGetDeviceCount(&deviceCount);
    if (error != cudaSuccess || deviceCount <= 0) {
        result.status = {StatusCode::CudaInitFailed, "PhaseUnwrapperCuda", cudaGetErrorString(error)};
        result.stats.status = result.status;
        return result;
    }

    Status status = computeOneCameraCuda(wrappedPhase, config, CameraSide::Left, result.left);
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }
    status = computeOneCameraCuda(wrappedPhase, config, CameraSide::Right, result.right);
    if (!status.ok()) {
        result.status = status;
        result.stats.status = status;
        return result;
    }

    result.stats.validImageCount = 2;
    result.stats.checkedPixels = result.left.absolutePhase.size() + result.right.absolutePhase.size();
    result.stats.cudaComputedPixels = result.stats.checkedPixels;
    result.stats.rejectedImageCount = result.left.invalidPixelCount + result.right.invalidPixelCount;
    result.stats.meanPixelValue = (result.left.meanPhase + result.right.meanPhase) * 0.5;
    result.status = {};
    result.stats.status = {};
    return result;
}

} // namespace reconstruct_one_frame
