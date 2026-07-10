#include "pipeline/SingleFramePipeline.h"

#include "calibration_model/CalibrationModel.h"
#include "logging/LogSession.h"
#include "io/PlyIO.h"
#include "phase/PhaseUnwrapper.h"
#include "phase/WrappedPhaseComputer.h"
#include "quality/PointReliability.h"
#include "reconstruction/PointCloudReconstructor.h"

#include <chrono>
#include <filesystem>
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
    validationStats.stageName = "input_contract_validation";
    Status status = validateStripeFrameGroup(frame, config_, validationStats);
    validationStats.stageName = "input_contract_validation";
    validationStats.elapsedMs = elapsedMsSince(start);
    result.stats.push_back(validationStats);
    if (!status.ok()) {
        result.status = status;
        logWarn("pipeline dry-run failed at image validation: " + std::string(statusCodeName(status.code)) + " " + status.message);
        return result;
    }

    StageStats calibrationStats;
    calibrationStats.stageName = "calibration_contract_validation";
    auto stageStart = std::chrono::steady_clock::now();
    if (options_.dryRunNoCalib) {
        calibrationStats.skipped = true;
        calibrationStats.status = {};
    } else {
        status = validateCalibrationImageSize(calibration_, config_.imageWidth, config_.imageHeight);
        calibrationStats.status = status;
        calibrationStats.elapsedMs = elapsedMsSince(stageStart);
        if (!status.ok()) {
            result.stats.push_back(calibrationStats);
            result.status = status;
            logWarn("pipeline dry-run failed at calibration contract: " + std::string(statusCodeName(status.code)) + " " + status.message);
            return result;
        }
    }
    calibrationStats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(calibrationStats);

    const CalibrationModel* calibration = options_.dryRunNoCalib ? nullptr : &calibration_;
    stageStart = std::chrono::steady_clock::now();
    PreprocessResult preprocess = buildPreprocessDryRunPlan(frame, calibration);
    preprocess.stats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(preprocess.stats);
    if (!preprocess.status.ok()) {
        result.status = preprocess.status;
        logWarn("pipeline dry-run failed at image preprocess: " + std::string(statusCodeName(preprocess.status.code)) + " " + preprocess.status.message);
        return result;
    }

    stageStart = std::chrono::steady_clock::now();
    WrappedPhaseOptions wrappedPhaseOptions;
    wrappedPhaseOptions.materializeModulation = false;
    WrappedPhaseResult wrappedPhase = computeWrappedPhaseCuda(frame, config_, wrappedPhaseOptions);
    wrappedPhase.stats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(wrappedPhase.stats);
    if (!wrappedPhase.status.ok()) {
        result.status = wrappedPhase.status;
        logWarn("pipeline dry-run failed at wrapped phase compute: " + std::string(statusCodeName(wrappedPhase.status.code)) + " " + wrappedPhase.status.message);
        return result;
    }
    result.wrappedPhaseComputed = true;

    stageStart = std::chrono::steady_clock::now();
    UnwrappedPhaseResult unwrappedPhase = computeUnwrappedPhaseCuda(wrappedPhase, config_);
    unwrappedPhase.stats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(unwrappedPhase.stats);
    if (!unwrappedPhase.status.ok()) {
        result.status = unwrappedPhase.status;
        logWarn("pipeline dry-run failed at phase unwrap: " + std::string(statusCodeName(unwrappedPhase.status.code)) + " " + unwrappedPhase.status.message);
        return result;
    }
    result.unwrappedPhaseComputed = true;

    if (options_.dryRun || options_.dryRunNoCalib) {
        StageStats computeStats;
        computeStats.stageName = "downstream_not_computed";
        computeStats.status = {StatusCode::NotComputed, "SingleFramePipeline", "matching/reconstruction are intentionally not computed during dry-run"};
        computeStats.notComputed = true;
        result.stats.push_back(computeStats);
        result.depthComputed = false;
        result.normalComputed = false;
        result.qualityComputed = false;
        result.status = {};
        logInfo("pipeline dry-run summary: StatusCode::Ok, wrapped and unwrapped phase computed, reconstruction notComputed");
        return result;
    }

    stageStart = std::chrono::steady_clock::now();
    PointCloudOutputOptions pointCloudOutputOptions;
    pointCloudOutputOptions.materializeVertices =
        options_.writePly || !options_.compareLegacyPlyPath.empty();
    pointCloudOutputOptions.materializeQualityGrid = config_.qualityInfoEnabled;
    PointCloudReconstructionResult pointCloud =
        reconstructPointCloudCuda(unwrappedPhase, calibration_, config_, frame, pointCloudOutputOptions);
    pointCloud.stats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(pointCloud.stats);
    result.matchingSummary = pointCloud.matchingSummary;
    result.pointCloudSummary = pointCloud.pointCloudStageSummary;
    if (!pointCloud.status.ok()) {
        result.status = pointCloud.status;
        logWarn("pipeline failed at point cloud reconstruction: " + std::string(statusCodeName(pointCloud.status.code)) + " " + pointCloud.status.message);
        return result;
    }
    result.depthComputed = true;
    result.normalComputed = pointCloud.normalsComputed;
    result.pointCloudVertexCount = pointCloud.filteredGridValidPointCount;

    if (config_.qualityInfoEnabled) {
        stageStart = std::chrono::steady_clock::now();
        PointReliabilityResult quality = evaluatePointReliability(pointCloud, config_);
        quality.stats.elapsedMs = elapsedMsSince(stageStart);
        result.stats.push_back(quality.stats);
        if (!quality.status.ok()) {
            result.status = quality.status;
            logWarn("pipeline failed at quality evaluation: " + std::string(statusCodeName(quality.status.code)) + " " + quality.status.message);
            return result;
        }
        result.qualityComputed = true;
        result.qualitySummary = quality.summary;
    } else {
        StageStats qualityStats;
        qualityStats.stageName = "quality_evaluate";
        qualityStats.status = {StatusCode::NotComputed, "SingleFramePipeline", "quality evaluation disabled by config"};
        qualityStats.skipped = true;
        qualityStats.notComputed = true;
        result.stats.push_back(qualityStats);
    }

    if (options_.writePly && !options_.outputDirectory.empty()) {
        StageStats outputStats;
        outputStats.stageName = "ply_output";
        stageStart = std::chrono::steady_clock::now();
        std::filesystem::path outputDirectory(options_.outputDirectory);
        if (options_.outputPerFrameSubdirectory) {
            outputDirectory /= std::to_string(frame.frameId);
        }
        const std::filesystem::path outputPath = outputDirectory / "depth_points.ply";
        status = writeAsciiPly(outputPath.string(), pointCloud.vertices);
        outputStats.elapsedMs = elapsedMsSince(stageStart);
        outputStats.status = status;
        outputStats.validImageCount = pointCloud.vertices.size();
        if (!status.ok()) {
            result.stats.push_back(outputStats);
            result.status = status;
            logWarn("pipeline failed at PLY output: " + std::string(statusCodeName(status.code)) + " " + status.message);
            return result;
        }
        result.outputPointCloudPath = outputPath.string();
        result.stats.push_back(outputStats);
    }

    if (!options_.compareLegacyPlyPath.empty()) {
        StageStats compareStats;
        compareStats.stageName = "legacy_point_cloud_compare";
        stageStart = std::chrono::steady_clock::now();
        PlyComparisonResult comparison = comparePointCloudToLegacy(pointCloud.vertices, options_.compareLegacyPlyPath);
        compareStats.elapsedMs = elapsedMsSince(stageStart);
        compareStats.status = comparison.status;
        compareStats.validImageCount = comparison.generatedVertexCount;
        compareStats.rejectedImageCount = comparison.legacyVertexCount > comparison.matchedLegacyCount
            ? comparison.legacyVertexCount - comparison.matchedLegacyCount
            : 0;
        compareStats.meanPixelValue = comparison.nearestRmsDistanceMm;
        if (!comparison.status.ok()) {
            result.stats.push_back(compareStats);
            result.status = comparison.status;
            logWarn("pipeline failed at legacy PLY comparison: " + std::string(statusCodeName(comparison.status.code)) + " " + comparison.status.message);
            return result;
        }
        result.legacyComparisonSummary = formatPlyComparison(comparison);
        result.stats.push_back(compareStats);
    }

    result.status = {};
    logInfo("pipeline phase-6 summary: StatusCode::Ok, point cloud vertices=" + std::to_string(result.pointCloudVertexCount) +
            ", " + result.matchingSummary +
            ", " + result.pointCloudSummary +
            ", qualityComputed=" + std::string(result.qualityComputed ? "true" : "false"));
    return result;
}

} // namespace reconstruct_one_frame
