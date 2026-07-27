#include "pipeline/SingleFramePipeline.h"

#include "calibration_model/CalibrationModel.h"
#include "logging/LogSession.h"
#include "io/PlyIO.h"
#include "phase/PhaseUnwrapper.h"
#include "phase/WrappedPhaseComputer.h"
#include "quality/PointReliability.h"
#include "reconstruction/PointCloudReconstructor.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

Status writeStagePly(const std::filesystem::path& outputDirectory,
                     const char* fileName,
                     const std::vector<PointCloudVertex>& vertices,
                     std::size_t& writtenVertices)
{
    Status status = writeAsciiPly((outputDirectory / fileName).string(), vertices);
    if (status.ok()) {
        writtenVertices += vertices.size();
    }
    return status;
}

const char* cameraSideName(CameraSide camera)
{
    switch (camera) {
    case CameraSide::Left:
        return "left";
    case CameraSide::Right:
        return "right";
    default:
        return "unknown";
    }
}

Status writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    try {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path);
        if (!out) {
            return {StatusCode::OutputWriteFailed,
                    "SingleFramePipeline",
                    "failed to open diagnostic output: " + path.string()};
        }
        out << content;
        return {};
    } catch (const std::exception& ex) {
        return {StatusCode::OutputWriteFailed, "SingleFramePipeline", ex.what()};
    }
}

Status writePhaseDiagnosticsCsv(const std::filesystem::path& outputDirectory,
                                const WrappedPhaseResult& wrappedPhase)
{
    std::ostringstream out;
    out << "camera,frequencyIndex,frequencyValue,width,height,validPixelCount,"
           "meanModulation,minModulation,maxModulation,modulationMaterialized\n";
    out << std::fixed << std::setprecision(6);
    for (const WrappedPhaseFrequencyResult& frequency : wrappedPhase.frequencies) {
        out << cameraSideName(frequency.camera) << ','
            << frequency.frequencyIndex << ','
            << frequency.frequencyValue << ','
            << frequency.width << ','
            << frequency.height << ','
            << frequency.validPixelCount << ','
            << frequency.meanModulation << ','
            << frequency.minModulation << ','
            << frequency.maxModulation << ','
            << (!frequency.modulation.empty() ? "true" : "false") << '\n';
    }
    return writeTextFile(outputDirectory / "phase-diagnostics.csv", out.str());
}

cv::Mat makeFloatMat(int width, int height, const std::vector<float>& values)
{
    cv::Mat mat(height, width, CV_32FC1, cv::Scalar(std::numeric_limits<float>::quiet_NaN()));
    const std::size_t expected =
        static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    if (width > 0 && height > 0 && values.size() == expected) {
        std::memcpy(mat.ptr<float>(), values.data(), sizeof(float) * expected);
    }
    return mat;
}

cv::Mat makeIntMat(int width, int height, const std::vector<int>& values)
{
    cv::Mat mat(height, width, CV_32SC1, cv::Scalar(0));
    const std::size_t expected =
        static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    if (width > 0 && height > 0 && values.size() == expected) {
        std::memcpy(mat.ptr<int>(), values.data(), sizeof(int) * expected);
    }
    return mat;
}

std::string phaseMapPrefix(CameraSide camera)
{
    return camera == CameraSide::Left ? "left" : "right";
}

Status writePhaseDebugMaps(const std::filesystem::path& outputDirectory,
                           const WrappedPhaseResult& wrappedPhase,
                           const UnwrappedPhaseResult& unwrappedPhase)
{
    try {
        std::filesystem::create_directories(outputDirectory);
        cv::FileStorage maps((outputDirectory / "phase-debug-maps.yml.gz").string(),
                             cv::FileStorage::WRITE);
        if (!maps.isOpened()) {
            return {StatusCode::OutputWriteFailed,
                    "SingleFramePipeline",
                    "failed to open phase debug map output: " +
                        (outputDirectory / "phase-debug-maps.yml.gz").string()};
        }

        maps << "phase_left" << makeFloatMat(unwrappedPhase.left.width,
                                              unwrappedPhase.left.height,
                                              unwrappedPhase.left.absolutePhase);
        maps << "phase_right" << makeFloatMat(unwrappedPhase.right.width,
                                               unwrappedPhase.right.height,
                                               unwrappedPhase.right.absolutePhase);
        for (const WrappedPhaseFrequencyResult& frequency : wrappedPhase.frequencies) {
            const std::string prefix = phaseMapPrefix(frequency.camera);
            maps << ("wrapped_phase_" + prefix + "_f" + std::to_string(frequency.frequencyIndex))
                 << makeFloatMat(frequency.width, frequency.height, frequency.phase);
            if (!frequency.modulation.empty()) {
                maps << ("modulation_" + prefix + "_f" + std::to_string(frequency.frequencyIndex))
                     << makeFloatMat(frequency.width, frequency.height, frequency.modulation);
            }
        }
        return {};
    } catch (const std::exception& ex) {
        return {StatusCode::OutputWriteFailed, "SingleFramePipeline", ex.what()};
    }
}

Status writePointCloudDebugMaps(const std::filesystem::path& outputDirectory,
                                const PointCloudReconstructionResult& pointCloud)
{
    try {
        std::filesystem::create_directories(outputDirectory);
        cv::FileStorage maps((outputDirectory / "pointcloud-debug-maps.yml.gz").string(),
                             cv::FileStorage::WRITE);
        if (!maps.isOpened()) {
            return {StatusCode::OutputWriteFailed,
                    "SingleFramePipeline",
                    "failed to open point-cloud debug map output: " +
                        (outputDirectory / "pointcloud-debug-maps.yml.gz").string()};
        }

        maps << "disparity" << makeFloatMat(pointCloud.width, pointCloud.height, pointCloud.debugDisparity);
        maps << "scores" << makeFloatMat(pointCloud.width, pointCloud.height, pointCloud.debugMatchScores);
        maps << "candidate_count" << makeIntMat(pointCloud.width, pointCloud.height, pointCloud.debugCandidateCounts);
        maps << "depth_map" << makeFloatMat(pointCloud.width, pointCloud.height, pointCloud.debugDepthMap);
        return {};
    } catch (const std::exception& ex) {
        return {StatusCode::OutputWriteFailed, "SingleFramePipeline", ex.what()};
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

FrameResult SingleFramePipeline::calc(const StripeFrameGroup& frame) const
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
    // phase 诊断只在明确打开时保留整幅 modulation；默认只留下轻量统计，避免热路径多占内存。
    wrappedPhaseOptions.materializeModulation =
        options_.writePly && !options_.outputDirectory.empty() &&
        config_.saveOutputs && config_.phaseDiagnosticsEnabled;
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
    const bool pointCloudStageDiagnosticsEnabled =
        config_.pointCloudStageDiagnosticsEnabled || config_.saveStagePointClouds;
    pointCloudOutputOptions.materializeVertices =
        options_.materializeFrameOutputs || options_.writePly || !options_.compareLegacyPlyPath.empty();
    pointCloudOutputOptions.materializeQualityGrid =
        options_.materializeFrameOutputs || config_.qualityInfoEnabled;
    pointCloudOutputOptions.materializeMatchingDiagnostics =
        options_.writePly && !options_.outputDirectory.empty() &&
        config_.saveOutputs && config_.matchingDiagnosticsEnabled;
    pointCloudOutputOptions.materializeStageVertices =
        options_.writePly && !options_.outputDirectory.empty() &&
        config_.saveOutputs && pointCloudStageDiagnosticsEnabled;
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

        if (config_.saveOutputs && config_.phaseDiagnosticsEnabled) {
            StageStats phaseDiagnosticsStats;
            phaseDiagnosticsStats.stageName = "phase_diagnostics_output";
            stageStart = std::chrono::steady_clock::now();
            status = writePhaseDiagnosticsCsv(outputDirectory, wrappedPhase);
            if (status.ok()) {
                status = writePhaseDebugMaps(outputDirectory, wrappedPhase, unwrappedPhase);
            }
            phaseDiagnosticsStats.elapsedMs = elapsedMsSince(stageStart);
            phaseDiagnosticsStats.status = status;
            phaseDiagnosticsStats.validImageCount = wrappedPhase.frequencies.size();
            if (!status.ok()) {
                result.stats.push_back(phaseDiagnosticsStats);
                result.status = status;
                logWarn("pipeline failed at phase diagnostics output: " +
                        std::string(statusCodeName(status.code)) + " " + status.message);
                return result;
            }
            result.stats.push_back(phaseDiagnosticsStats);
        }

        if (config_.saveOutputs && config_.matchingDiagnosticsEnabled) {
            StageStats matchingDiagnosticsStats;
            matchingDiagnosticsStats.stageName = "matching_diagnostics_output";
            stageStart = std::chrono::steady_clock::now();
            status = writeTextFile(outputDirectory / "matching-diagnostics.csv",
                                   pointCloud.matchingDiagnosticsCsv);
            matchingDiagnosticsStats.elapsedMs = elapsedMsSince(stageStart);
            matchingDiagnosticsStats.status = status;
            matchingDiagnosticsStats.validImageCount =
                pointCloud.matchingDiagnostics.acceptedMatchPixelCount;
            if (!status.ok()) {
                result.stats.push_back(matchingDiagnosticsStats);
                result.status = status;
                logWarn("pipeline failed at matching diagnostics output: " +
                        std::string(statusCodeName(status.code)) + " " + status.message);
                return result;
            }
            result.stats.push_back(matchingDiagnosticsStats);
        }

        if (config_.saveOutputs && pointCloudStageDiagnosticsEnabled) {
            StageStats stageOutputStats;
            stageOutputStats.stageName = "stage_ply_output";
            stageStart = std::chrono::steady_clock::now();
            std::size_t writtenVertices = 0U;

            // 这些诊断 PLY 对齐 Legacy 的 stage 思路：raw 是重投影结果，
            // filter-input 是真正喂给三维连通性 filter 的点，deleted 是
            // filter 输入里被 final filter 删除的点。默认关闭，避免热路径
            // 为了调试额外回传整帧点云。
            status = writeStagePly(outputDirectory, "raw-points.ply", pointCloud.rawStageVertices, writtenVertices);
            if (status.ok()) {
                status = writeStagePly(outputDirectory, "filter-input-points.ply", pointCloud.filterInputStageVertices, writtenVertices);
            }
            if (status.ok()) {
                status = writeStagePly(outputDirectory, "filter-deleted-points.ply", pointCloud.filterDeletedStageVertices, writtenVertices);
            }
            if (status.ok()) {
                status = writePointCloudDebugMaps(outputDirectory, pointCloud);
            }
            stageOutputStats.elapsedMs = elapsedMsSince(stageStart);
            stageOutputStats.status = status;
            stageOutputStats.validImageCount = writtenVertices;
            if (!status.ok()) {
                result.stats.push_back(stageOutputStats);
                result.status = status;
                logWarn("pipeline failed at stage PLY output: " + std::string(statusCodeName(status.code)) + " " + status.message);
                return result;
            }
            result.stats.push_back(stageOutputStats);
        }
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
