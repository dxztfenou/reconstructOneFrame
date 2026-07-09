#pragma once

#include "config/ReconsConfig.h"
#include "image/ImageTypes.h"

namespace reconstruct_one_frame {

struct ImageValidationOptions {
    double blackPixelThresholdRatio = 0.995;
    double saturatedPixelThresholdRatio = 0.995;
    double nonFinitePixelThresholdRatio = 0.0;
    bool requireStripeCoverage = false;
};

Status validateStripeFrameGroup(const StripeFrameGroup& frame,
                                StageStats& stats,
                                const ImageValidationOptions& options = {});
Status validateStripeFrameGroup(const StripeFrameGroup& frame,
                                const ReconsConfig& config,
                                StageStats& stats,
                                const ImageValidationOptions& options = {});

} // namespace reconstruct_one_frame
