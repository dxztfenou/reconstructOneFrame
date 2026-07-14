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

Status ReconstructEngine::describe(EngineDescriptor& descriptor) const
{
    if (!impl_->initialized) {
        return {StatusCode::InternalError, "ReconstructEngine", "engine is not initialized"};
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
    impl_->pipeline.shutdown();
    impl_->initialized = false;
}

} // namespace reconstruct_one_frame
