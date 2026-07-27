#include "reconstruct_one_frame/reconstructInterface.h"

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "pipeline/SingleFramePipeline.h"

#include <algorithm>
#include <memory>

namespace reconstruct_one_frame {

struct ReconstructEngine::Impl {
    ReconsConfig config;
    CalibrationModel calibration;
    SingleFramePipeline pipeline;
    bool runtimeInitialized = false;
    bool configured = false;
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

Status ReconstructEngine::init()
{
    impl_->pipeline.shutdown();
    impl_->runtimeInitialized = true;
    impl_->configured = false;
    return {};
}

Status ReconstructEngine::setConfig(const InitOptions& options)
{
    if (!impl_->runtimeInitialized) {
        return {StatusCode::InternalError, "ReconstructEngine", "engine runtime is not initialized"};
    }

    Status status = loadReconsConfigWithBase(options.configBasePath, options.configPath, impl_->config);
    if (!status.ok()) {
        impl_->configured = false;
        return status;
    }

    const std::string calibrationPath = !options.calibrationPath.empty()
        ? options.calibrationPath
        : impl_->config.calibResultPath;
    if (!options.dryRunNoCalib) {
        status = loadCalibrationResultJson(calibrationPath, impl_->calibration);
        if (!status.ok()) {
            impl_->configured = false;
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
    impl_->configured = status.ok();
    return status;
}

Status ReconstructEngine::describe(EngineDescriptor& descriptor) const
{
    if (!impl_->runtimeInitialized || !impl_->configured) {
        return {StatusCode::InternalError, "ReconstructEngine", "engine config is not ready"};
    }

    EngineDescriptor value;
    value.imageWidth = impl_->config.imageWidth;
    value.imageHeight = impl_->config.imageHeight;

    int maxProjectorIndex = 0;
    value.stripeRequirements.reserve(impl_->config.stripeRequirements.size());
    for (const StripeRequirement& requirement : impl_->config.stripeRequirements) {
        value.stripeRequirements.push_back({
            requirement.frequencyIndex,
            requirement.frequencyValue,
            requirement.requiredPhaseSteps,
            requirement.firstProjectorIndex,
            requirement.phaseStepDirection
        });
        maxProjectorIndex = std::max(
            maxProjectorIndex,
            requirement.firstProjectorIndex + requirement.requiredPhaseSteps - 1);
    }

    if (impl_->config.colorTextureEnabled || impl_->config.clear255) {
        value.colorProjectorIndices = impl_->config.colorTextureProjectorIndices;
        for (int projectorIndex : value.colorProjectorIndices) {
            maxProjectorIndex = std::max(maxProjectorIndex, projectorIndex);
        }
    }
    value.liveImageCount = static_cast<std::uint32_t>(std::max(maxProjectorIndex, 0));

    if (impl_->calibration.qMatrix.size() == value.cameraModelValues.size()) {
        value.cameraModelRows = 4;
        value.cameraModelCols = 4;
        std::copy(impl_->calibration.qMatrix.begin(),
                  impl_->calibration.qMatrix.end(),
                  value.cameraModelValues.begin());
    } else if (impl_->calibration.leftIntrinsics.size() == 9U) {
        value.cameraModelRows = 3;
        value.cameraModelCols = 3;
        std::copy(impl_->calibration.leftIntrinsics.begin(),
                  impl_->calibration.leftIntrinsics.end(),
                  value.cameraModelValues.begin());
    } else {
        return {StatusCode::CalibrationInvalid,
                "ReconstructEngine",
                "calibration does not contain Q or left intrinsics"};
    }

    descriptor = std::move(value);
    return {};
}

FrameResult ReconstructEngine::calc(const StripeFrameGroup& frame)
{
    FrameResult result;
    if (!impl_->runtimeInitialized || !impl_->configured) {
        result.status = {StatusCode::InternalError, "ReconstructEngine", "engine config is not ready"};
        return result;
    }
    return impl_->pipeline.calc(frame);
}

void ReconstructEngine::shutdown()
{
    impl_->pipeline.shutdown();
    impl_->configured = false;
}

} // namespace reconstruct_one_frame
