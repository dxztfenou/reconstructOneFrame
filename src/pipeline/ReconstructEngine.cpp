#include "reconstruct_one_frame/reconstructInterface.h"

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "pipeline/SingleFramePipeline.h"

#include <utility>

namespace reconstruct_one_frame {

struct ReconstructEngine::Impl {
    ReconsConfig config;
    CalibrationModel calibration;
    SingleFramePipeline pipeline;
    bool initialized = false;
};

ReconstructEngine::ReconstructEngine()
    : impl_(new Impl())
{
}

ReconstructEngine::~ReconstructEngine()
{
    delete impl_;
    impl_ = nullptr;
}

ReconstructEngine::ReconstructEngine(ReconstructEngine&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
{
}

ReconstructEngine& ReconstructEngine::operator=(ReconstructEngine&& other) noexcept
{
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

Status ReconstructEngine::init(const InitOptions& options)
{
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }

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
    pipelineOptions.outputDirectory = options.outputDirectory;
    pipelineOptions.compareLegacyPlyPath = options.compareLegacyPlyPath;
    status = impl_->pipeline.initialize(impl_->config, impl_->calibration, pipelineOptions);
    impl_->initialized = status.ok();
    return status;
}

FrameResult ReconstructEngine::run(const StripeFrameGroup& frame)
{
    FrameResult result;
    if (impl_ == nullptr || !impl_->initialized) {
        result.status = {StatusCode::InternalError, "ReconstructEngine", "engine is not initialized"};
        return result;
    }
    return impl_->pipeline.run(frame);
}

void ReconstructEngine::shutdown()
{
    if (impl_ != nullptr) {
        impl_->initialized = false;
    }
}

} // namespace reconstruct_one_frame
