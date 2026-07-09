#pragma once

#include "config/ReconsConfig.h"
#include "image/ImageTypes.h"

#include <vector>

namespace reconstruct_one_frame {

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
    std::vector<WrappedPhaseFrequencyResult> frequencies;
};

struct WrappedPhaseOptions {
    double minMeanModulation = 1.0;
    bool preferCuda = true;
};

WrappedPhaseResult computeWrappedPhaseCpuReference(const StripeFrameGroup& frame,
                                                   const ReconsConfig& config,
                                                   const WrappedPhaseOptions& options = {});
WrappedPhaseResult computeWrappedPhaseCuda(const StripeFrameGroup& frame,
                                           const ReconsConfig& config,
                                           const WrappedPhaseOptions& options = {});

} // namespace reconstruct_one_frame
