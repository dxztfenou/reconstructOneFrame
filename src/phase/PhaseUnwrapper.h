#pragma once

#include "phase/WrappedPhaseComputer.h"

#include <memory>
#include <vector>

namespace reconstruct_one_frame {

struct UnwrappedPhaseCameraResult {
    CameraSide camera = CameraSide::Unknown;
    int width = 0;
    int height = 0;
    std::vector<float> absolutePhase;
    std::size_t validPixelCount = 0;
    std::size_t invalidPixelCount = 0;
    double minPhase = 0.0;
    double maxPhase = 0.0;
    double meanPhase = 0.0;
};

struct UnwrappedPhaseResult {
    Status status;
    StageStats stats;
    PhaseCoordinateDomain coordinateDomain = PhaseCoordinateDomain::Sensor;
    UnwrappedPhaseCameraResult left;
    UnwrappedPhaseCameraResult right;
};

struct PhaseUnwrapOptions {
    bool preferCuda = true;
};

class PhaseUnwrapCudaWorkspace {
public:
    struct Impl;

    PhaseUnwrapCudaWorkspace();
    ~PhaseUnwrapCudaWorkspace();
    PhaseUnwrapCudaWorkspace(PhaseUnwrapCudaWorkspace&&) noexcept;
    PhaseUnwrapCudaWorkspace& operator=(PhaseUnwrapCudaWorkspace&&) noexcept;
    PhaseUnwrapCudaWorkspace(const PhaseUnwrapCudaWorkspace&) = delete;
    PhaseUnwrapCudaWorkspace& operator=(const PhaseUnwrapCudaWorkspace&) = delete;
    void reset() noexcept;

    std::unique_ptr<Impl> impl_;
};

UnwrappedPhaseResult computeUnwrappedPhaseCuda(const WrappedPhaseResult& wrappedPhase,
                                               const ReconsConfig& config,
                                               const PhaseUnwrapOptions& options = {});
UnwrappedPhaseResult computeUnwrappedPhaseCuda(const WrappedPhaseResult& wrappedPhase,
                                               const ReconsConfig& config,
                                               PhaseUnwrapCudaWorkspace& workspace,
                                               const PhaseUnwrapOptions& options = {});

} // namespace reconstruct_one_frame
