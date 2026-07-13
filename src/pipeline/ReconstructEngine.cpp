#include "reconstruct_one_frame/reconstructInterface.h"

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "pipeline/SingleFramePipeline.h"

#include <memory>

namespace reconstruct_one_frame {

struct ReconstructEngine::Impl {
    ReconsConfig config;
    CalibrationModel calibration;
    SingleFramePipeline pipeline;
    bool initialized = false;
};

ReconstructEngine::ReconstructEngine()
    : impl_(std::make_unique<Impl>())
{
}

ReconstructEngine::~ReconstructEngine() = default;

ReconstructEngine::ReconstructEngine(ReconstructEngine&& other) noexcept
    = default;

ReconstructEngine& ReconstructEngine::operator=(ReconstructEngine&& other) noexcept
    = default;

Status ReconstructEngine::init(const InitOptions& options)
{
    Status status = loadReconsConfig(options.configPath, impl_->config);
    if (!status.ok()) {
        impl_->initialized = false;
        return status;
    }

    const std::string calibrationPath = !options.calibrationPath.empty()
        ? options.calibrationPath
        : impl_->config.calibResultPath;
    if (!options.dryRunNoCalib) {
        status = loadCalibrationResultJson(calibrationPath, impl_->calibration);
        if (!status.ok()) {
            impl_->initialized = false;
            return status;
        }
    }

    PipelineOptions pipelineOptions;
    pipelineOptions.dryRun = options.dryRun;
    pipelineOptions.dryRunNoCalib = options.dryRunNoCalib;
    pipelineOptions.writePly = options.writePly;
    pipelineOptions.materializeFrameOutputs = options.materializeFrameOutputs;
    pipelineOptions.outputPerFrameSubdirectory = options.outputPerFrameSubdirectory;
    pipelineOptions.outputDirectory = options.outputDirectory;
    pipelineOptions.compareLegacyPlyPath = options.compareLegacyPlyPath;
    status = impl_->pipeline.initialize(impl_->config, impl_->calibration, pipelineOptions);
    impl_->initialized = status.ok();
    return status;
}

FrameResult ReconstructEngine::run(const StripeFrameGroup& frame)
{
    FrameResult result;
    if (!impl_->initialized) {
        result.status = {StatusCode::InternalError, "ReconstructEngine", "engine is not initialized"};
        return result;
    }
    return impl_->pipeline.run(frame);
}

void ReconstructEngine::shutdown()
{
    impl_->initialized = false;
}

} // namespace reconstruct_one_frame
