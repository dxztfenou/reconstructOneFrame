#include "pipeline/SingleFramePipeline.h"

#include "calibration_model/CalibrationModel.h"
#include "logging/LogSession.h"
#include "io/PlyIO.h"
#include "phase/PhaseUnwrapper.h"
#include "phase/WrappedPhaseComputer.h"
#include "quality/PointReliability.h"
#include "reconstruction/PointCloudReconstructor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

double elapsedMsSince(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}


std::uint16_t toU16Unit(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    return static_cast<std::uint16_t>(std::lround(clamped * 65535.0));
}

std::uint16_t toU16Reason(unsigned int value)
{
    return static_cast<std::uint16_t>(std::min<unsigned int>(value, 65535U));
}

void materializeFrameOutputMaps(const PointCloudReconstructionResult& pointCloud,
                                const PointReliabilityResult* quality,
                                FrameResult& result)
{
    if (pointCloud.width <= 0 || pointCloud.height <= 0) {
        return;
    }

    result.outputWidth = pointCloud.width;
    result.outputHeight = pointCloud.height;
    const std::size_t pixelCount =
        static_cast<std::size_t>(pointCloud.width) * static_cast<std::size_t>(pointCloud.height);
    result.depthXyz.assign(pixelCount * 3U, 0.0F);
    result.normalXyz.assign(pixelCount * 3U, 0.0F);
    result.colorBgr.assign(pixelCount * 3U, 0U);
    if (pointCloud.rectifiedColorBgr.size() == result.colorBgr.size()) {
        result.colorBgr = pointCloud.rectifiedColorBgr;
    }

    for (const PointCloudVertex& vertex : pointCloud.vertices) {
        if (vertex.u < 0 || vertex.v < 0 ||
            vertex.u >= pointCloud.width || vertex.v >= pointCloud.height) {
            continue;
        }
        const std::size_t base =
            (static_cast<std::size_t>(vertex.v) * static_cast<std::size_t>(pointCloud.width) +
             static_cast<std::size_t>(vertex.u)) * 3U;
        result.depthXyz[base + 0U] = vertex.x;
        result.depthXyz[base + 1U] = vertex.y;
        result.depthXyz[base + 2U] = vertex.z;
        result.normalXyz[base + 0U] = vertex.nx;
        result.normalXyz[base + 1U] = vertex.ny;
        result.normalXyz[base + 2U] = vertex.nz;
        result.colorBgr[base + 0U] = vertex.b;
        result.colorBgr[base + 1U] = vertex.g;
        result.colorBgr[base + 2U] = vertex.r;
    }

    if (quality != nullptr && quality->map.pixels.size() == pixelCount) {
        result.qualityInfoU16.assign(pixelCount * 3U, 0U);
        for (std::size_t idx = 0; idx < pixelCount; ++idx) {
            const PointReliabilityPixel& pixel = quality->map.pixels[idx];
            const std::size_t base = idx * 3U;
            result.qualityInfoU16[base + 0U] = static_cast<std::uint16_t>(
                std::clamp(pixel.semanticClass, 0.0, 65535.0));
            result.qualityInfoU16[base + 1U] = toU16Unit(pixel.score);
            result.qualityInfoU16[base + 2U] = toU16Reason(pixel.reason);
        }
    }
}

} // namespace

Status SingleFramePipeline::initialize(ReconsConfig config,
                                       CalibrationModel calibration,
                                       PipelineOptions options)
{
    shutdown();
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
    WrappedPhaseResult wrappedPhase = computeWrappedPhaseCuda(
        frame, config_, calibration_, wrappedPhaseWorkspace_, wrappedPhaseOptions);
    wrappedPhase.stats.elapsedMs = elapsedMsSince(stageStart);
    result.stats.push_back(wrappedPhase.stats);
    if (!wrappedPhase.status.ok()) {
        result.status = wrappedPhase.status;
        logWarn("pipeline dry-run failed at wrapped phase compute: " + std::string(statusCodeName(wrappedPhase.status.code)) + " " + wrappedPhase.status.message);
        return result;
    }
    result.wrappedPhaseComputed = true;

    stageStart = std::chrono::steady_clock::now();
    UnwrappedPhaseResult unwrappedPhase = computeUnwrappedPhaseCuda(
        wrappedPhase, config_, phaseUnwrapWorkspace_);
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
        options_.materializeFrameOutputs || options_.writePly || !options_.compareLegacyPlyPath.empty();
    pointCloudOutputOptions.materializeQualityGrid =
        options_.materializeFrameOutputs || config_.qualityInfoEnabled;
    PointCloudReconstructionResult pointCloud =
        reconstructPointCloudCuda(
            unwrappedPhase, calibration_, config_, frame, pointCloudWorkspace_, pointCloudOutputOptions);
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

    PointReliabilityResult quality;
    const PointReliabilityResult* qualityOutput = nullptr;
    if (config_.qualityInfoEnabled) {
        stageStart = std::chrono::steady_clock::now();
        quality = evaluatePointReliability(pointCloud, config_);
        quality.stats.elapsedMs = elapsedMsSince(stageStart);
        result.stats.push_back(quality.stats);
        if (!quality.status.ok()) {
            result.status = quality.status;
            logWarn("pipeline failed at quality evaluation: " + std::string(statusCodeName(quality.status.code)) + " " + quality.status.message);
            return result;
        }
        result.qualityComputed = true;
        result.qualitySummary = quality.summary;
        qualityOutput = &quality;
    } else {
        StageStats qualityStats;
        qualityStats.stageName = "quality_evaluate";
        qualityStats.status = {StatusCode::NotComputed, "SingleFramePipeline", "quality evaluation disabled by config"};
        qualityStats.skipped = true;
        qualityStats.notComputed = true;
        result.stats.push_back(qualityStats);
    }

    if (options_.materializeFrameOutputs) {
        materializeFrameOutputMaps(pointCloud, qualityOutput, result);
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

void SingleFramePipeline::shutdown() noexcept
{
    initialized_ = false;
    wrappedPhaseWorkspace_.reset();
    phaseUnwrapWorkspace_.reset();
    pointCloudWorkspace_.reset();
}

} // namespace reconstruct_one_frame
