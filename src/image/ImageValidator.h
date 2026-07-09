#pragma once

#include "image/ImageTypes.h"

namespace reconstruct_one_frame {

struct ImageValidationOptions {
    double blackPixelThresholdRatio = 0.995;
};

Status validateStripeFrameGroup(const StripeFrameGroup& frame,
                                StageStats& stats,
                                const ImageValidationOptions& options = {});

} // namespace reconstruct_one_frame
