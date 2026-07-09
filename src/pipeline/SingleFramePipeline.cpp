#include "pipeline/SingleFramePipeline.h"

#include "logging/LogSession.h"

#include <chrono>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

double elapsedMsSince(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

} // namespace

Status SingleFramePipeline::initialize(ReconsConfig config,
                                       CalibrationModel calibration,
                                       PipelineOptions options)
{
    config_ = std::move(config);
    calibration_ = std::move(calibration);
    options_ = options;
    initialized_ = true;
    logInfo("SingleFramePipeline initialized: " + summarizeConfig(config_));
    return {};
}

FrameResult SingleFramePipeline::run(const StripeFrameGroup& frame) const
{
    FrameResult result;
    if (!initialized_) {
        result.status = {StatusCode::InternalError, "SingleFramePipeline", "pipeline is not initialized"};
        return result;
    }

    const auto start = std::chrono::steady_clock::now();
    StageStats validationStats;
    Status status = validateStripeFrameGroup(frame, validationStats);
    validationStats.elapsedMs = elapsedMsSince(start);
    result.stats.push_back(validationStats);
    if (!status.ok()) {
        result.status = status;
        logWarn("pipeline dry-run failed at image validation: " + std::string(statusCodeName(status.code)) + " " + status.message);
        return result;
    }

    StageStats computeStats;
    computeStats.stageName = "dry_run_compute";
    computeStats.status = {StatusCode::NotComputed, "SingleFramePipeline", "phase/matching/reconstruction are intentionally not computed in phase 1 dry-run"};
    computeStats.notComputed = true;
    result.stats.push_back(computeStats);

    result.depthComputed = false;
    result.normalComputed = false;
    result.qualityComputed = false;
    result.status = {};
    logInfo("pipeline dry-run summary: StatusCode::Ok, depth/normal/quality notComputed");
    return result;
}

} // namespace reconstruct_one_frame
