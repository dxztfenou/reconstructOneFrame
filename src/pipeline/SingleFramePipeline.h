#pragma once

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "image/ImageValidator.h"

namespace reconstruct_one_frame {

struct PipelineOptions {
    bool dryRun = true;
    bool dryRunNoCalib = false;
};

class SingleFramePipeline {
public:
    Status initialize(ReconsConfig config,
                      CalibrationModel calibration,
                      PipelineOptions options);

    FrameResult run(const StripeFrameGroup& frame) const;

private:
    ReconsConfig config_;
    CalibrationModel calibration_;
    PipelineOptions options_;
    bool initialized_ = false;
};

} // namespace reconstruct_one_frame
