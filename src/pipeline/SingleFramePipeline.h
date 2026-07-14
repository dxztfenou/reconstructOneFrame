#pragma once

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "image/ImagePreprocessor.h"
#include "image/ImageValidator.h"
#include "phase/PhaseUnwrapper.h"
#include "reconstruction/PointCloudReconstructor.h"

namespace reconstruct_one_frame {

struct PipelineOptions {
    bool dryRun = true;
    bool dryRunNoCalib = false;
    bool writePly = false;
    bool materializeFrameOutputs = false;
    bool outputPerFrameSubdirectory = false;
    std::string outputDirectory;
    std::string compareLegacyPlyPath;
};

class SingleFramePipeline {
public:
    Status initialize(ReconsConfig config,
                      CalibrationModel calibration,
                      PipelineOptions options);

    FrameResult run(const StripeFrameGroup& frame) const;
    void shutdown() noexcept;

private:
    ReconsConfig config_;
    CalibrationModel calibration_;
    PipelineOptions options_;
    mutable WrappedPhaseCudaWorkspace wrappedPhaseWorkspace_;
    mutable PhaseUnwrapCudaWorkspace phaseUnwrapWorkspace_;
    mutable PointCloudCudaWorkspace pointCloudWorkspace_;
    bool initialized_ = false;
};

} // namespace reconstruct_one_frame
