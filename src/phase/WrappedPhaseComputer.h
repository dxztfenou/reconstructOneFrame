#pragma once

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "image/ImageTypes.h"

#include <memory>
#include <vector>

namespace reconstruct_one_frame {

// Phase values are only meaningful together with the pixel domain in which
// their source intensities were sampled. This prevents accidental double remap.
enum class PhaseCoordinateDomain {
    Sensor,
    Rectified
};

struct WrappedPhaseFrequencyResult {
    CameraSide camera = CameraSide::Unknown;
    int frequencyIndex = -1;
    int frequencyValue = 0;
    int width = 0;
    int height = 0;
    std::vector<float> phase;
    std::vector<float> modulation;
    std::size_t validPixelCount = 0;
    double meanModulation = 0.0;
    double minModulation = 0.0;
    double maxModulation = 0.0;
};

struct WrappedPhaseResult {
    Status status;
    StageStats stats;
    PhaseCoordinateDomain coordinateDomain = PhaseCoordinateDomain::Sensor;
    std::vector<WrappedPhaseFrequencyResult> frequencies;
};

struct WrappedPhaseOptions {
    double minMeanModulation = 1.0;
    bool preferCuda = true;
    bool materializeModulation = true;
};

class WrappedPhaseCudaWorkspace {
public:
    struct Impl;

    WrappedPhaseCudaWorkspace();
    ~WrappedPhaseCudaWorkspace();
    WrappedPhaseCudaWorkspace(WrappedPhaseCudaWorkspace&&) noexcept;
    WrappedPhaseCudaWorkspace& operator=(WrappedPhaseCudaWorkspace&&) noexcept;
    WrappedPhaseCudaWorkspace(const WrappedPhaseCudaWorkspace&) = delete;
    WrappedPhaseCudaWorkspace& operator=(const WrappedPhaseCudaWorkspace&) = delete;
    void reset() noexcept;

    std::unique_ptr<Impl> impl_;
};

WrappedPhaseResult computeWrappedPhaseCpuReference(const StripeFrameGroup& frame,
                                                   const ReconsConfig& config,
                                                   const WrappedPhaseOptions& options = {});
WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const WrappedPhaseOptions& options = {});
WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           WrappedPhaseCudaWorkspace& workspace,
                                           const WrappedPhaseOptions& options = {});
WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const CalibrationModel& calibration,
                                           const WrappedPhaseOptions& options = {});
WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const CalibrationModel& calibration,
                                           WrappedPhaseCudaWorkspace& workspace,
                                           const WrappedPhaseOptions& options = {});

} // namespace reconstruct_one_frame
