#pragma once

#include "calibration_model/CalibrationModel.h"
#include "image/ImageTypes.h"

namespace reconstruct_one_frame {

struct PreprocessPlan {
    int width = 0;
    int height = 0;
    int channels = 0;
    ImageElementType elementType = ImageElementType::Unknown;
    bool rectificationRequired = false;
    bool gpuUploadRequired = false;
    bool normalizedFloatRequired = true;
    std::size_t stripeCount = 0;
};

struct PreprocessResult {
    Status status;
    StageStats stats;
    PreprocessPlan plan;
};

PreprocessResult buildPreprocessDryRunPlan(const StripeFrameGroup& frame,
                                           const CalibrationModel* calibration);

} // namespace reconstruct_one_frame
