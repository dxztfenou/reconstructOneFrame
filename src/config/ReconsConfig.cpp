#include "config/ReconsConfig.h"

#include <sstream>
#include <utility>

namespace reconstruct_one_frame {

namespace {

Status requirePositive(const char* key, int value)
{
    if (value <= 0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", std::string(key) + " must be positive"};
    }
    return {};
}

Status loadConfigJsonObject(const std::string& path, JsonObject& json)
{
    Status status = loadJsonObjectFromFile(path, json);
    if (status.code == StatusCode::ConfigMissing) {
        status.module = "ReconsConfig";
        return status;
    }
    if (!status.ok()) {
        status.module = "ReconsConfig";
        return status;
    }
    return {};
}

Status parseReconsConfigObject(const JsonObject& json, ReconsConfig parsed, ReconsConfig& config)
{
    parsed.rawJson = json.raw();

    std::vector<int> imageSize;
    if (json.getIntArray("image_size", imageSize)) {
        if (imageSize.size() != 2) {
            return {StatusCode::ConfigInvalidValue, "ReconsConfig", "image_size must contain width and height"};
        }
        parsed.imageWidth = imageSize[0];
        parsed.imageHeight = imageSize[1];
    }

    (void)json.getInt("FREQ", parsed.frequencyCount);
    (void)json.getInt("STEP", parsed.stepCount);
    (void)json.getIntArray("freqSeries", parsed.freqSeries);
    (void)json.getInt("freq23", parsed.freq23);
    (void)json.getInt("Bmin", parsed.bMin);
    (void)json.getInt("winSizeMedian", parsed.medianKernelSize);
    (void)json.getIntArray("phaseStepCounts", parsed.phaseStepCounts);
    (void)json.getInt("phaseStepDirection", parsed.phaseStepDirection);
    (void)json.getBool("phaseUnwrapResidualGateEnabled", parsed.phaseUnwrapResidualGateEnabled);
    (void)json.getDouble("phaseUnwrapAbs23ResidualThreshold", parsed.phaseUnwrapAbs23ResidualThreshold);
    (void)json.getDouble("phaseUnwrapFinalResidualThreshold", parsed.phaseUnwrapFinalResidualThreshold);
    (void)json.getBool("phaseFinalMedianFilterEnabled", parsed.phaseFinalMedianFilterEnabled);
    (void)json.getDouble("phaseDiffThreshold", parsed.phaseDiffThreshold);
    (void)json.getDouble("minZ", parsed.minZ);
    (void)json.getDouble("maxZ", parsed.maxZ);
    (void)json.getBool("disparityWindowEnabled", parsed.disparityWindowEnabled);
    (void)json.getDouble("disparityWindowMinZ", parsed.disparityWindowMinZ);
    (void)json.getDouble("disparityWindowMaxZ", parsed.disparityWindowMaxZ);
    (void)json.getDouble("disparityWindowMargin", parsed.disparityWindowMargin);
    (void)json.getBool("matchingUniquenessEnabled", parsed.matchingUniquenessEnabled);
    (void)json.getInt("matchingMaxCandidateCount", parsed.matchingMaxCandidateCount);
    (void)json.getDouble("matchingMinSecondBestGap", parsed.matchingMinSecondBestGap);
    (void)json.getBool("matchingLeftRightConsistencyEnabled", parsed.matchingLeftRightConsistencyEnabled);
    (void)json.getDouble("matchingLeftRightTolerance", parsed.matchingLeftRightTolerance);
    (void)json.getBool("matchingRightPhaseMonotonicEnabled", parsed.matchingRightPhaseMonotonicEnabled);
    (void)json.getInt("matchingRightPhaseMonotonicRadius", parsed.matchingRightPhaseMonotonicRadius);
    (void)json.getDouble("matchingRightPhaseMinSlope", parsed.matchingRightPhaseMinSlope);
    (void)json.getBool("matchingRejectOnSubpixelFailure", parsed.matchingRejectOnSubpixelFailure);
    (void)json.getBool("matchingCandidateQualityFilterEnabled", parsed.matchingCandidateQualityFilterEnabled);
    (void)json.getDouble("matchingCandidateMinModulation", parsed.matchingCandidateMinModulation);
    (void)json.getBool("matchingCandidateRejectSaturation", parsed.matchingCandidateRejectSaturation);
    (void)json.getBool("matchingCandidateRejectLowLight", parsed.matchingCandidateRejectLowLight);
    (void)json.getBool("disparitySubpixelEnabled", parsed.disparitySubpixelEnabled);
    (void)json.getBool("disparityLocalConsistencyEnabled", parsed.disparityLocalConsistencyEnabled);
    (void)json.getDouble("disparityLocalConsistencyThreshold", parsed.disparityLocalConsistencyThreshold);
    (void)json.getInt("disparityLocalConsistencyRadius", parsed.disparityLocalConsistencyRadius);
    (void)json.getInt("disparityLocalConsistencyMinSupport", parsed.disparityLocalConsistencyMinSupport);
    (void)json.getBool("pointCloudSmoothingEnabled", parsed.pointCloudSmoothingEnabled);
    (void)json.getBool("pointCloudFilterEnabled", parsed.pointCloudFilterEnabled);
    (void)json.getBool("colorTextureEnabled", parsed.colorTextureEnabled);
    (void)json.getIntArray("colorTextureProjectorIndices", parsed.colorTextureProjectorIndices);
    (void)json.getDoubleArray("colorCorrectionMatrix", parsed.colorCorrectionMatrix);
    (void)json.getDouble("colorGamma", parsed.colorGamma);
    (void)json.getBool("clear255", parsed.clear255);
    (void)json.getInt("clear255DilateRadius", parsed.clear255DilateRadius);
    (void)json.getBool("colorHighlightCompressionEnabled", parsed.colorHighlightCompressionEnabled);
    (void)json.getBool("qualityInfoEnabled", parsed.qualityInfoEnabled);
    (void)json.getBool("qualityMapEnabled", parsed.qualityInfoEnabled);
    (void)json.getDouble("qualityInfoMinModulation", parsed.qualityInfoMinModulation);
    (void)json.getDouble("qualityMapMinModulation", parsed.qualityInfoMinModulation);
    (void)json.getBool("qualityInfoReasonChannelEnabled", parsed.qualityInfoReasonChannelEnabled);
    (void)json.getBool("qualityMapReasonChannelEnabled", parsed.qualityInfoReasonChannelEnabled);
    (void)json.getDouble("qualityInfoPhaseCostThreshold", parsed.qualityInfoPhaseCostThreshold);
    (void)json.getInt("qualityInfoMaxCandidateCount", parsed.qualityInfoMaxCandidateCount);
    (void)json.getInt("qualityInfoHoleEdgeRadius", parsed.qualityInfoHoleEdgeRadius);
    (void)json.getDouble("qualityInfoHighConfidenceThreshold", parsed.qualityInfoHighConfidenceThreshold);
    (void)json.getBool("qualityInfoUseMatchCost", parsed.qualityInfoUseMatchCost);
    (void)json.getBool("qualityInfoUseCandidateCount", parsed.qualityInfoUseCandidateCount);
    (void)json.getBool("qualityInfoUseModulation", parsed.qualityInfoUseModulation);
    (void)json.getBool("aiEnabled", parsed.aiEnabled);
    (void)json.getBool("debugMapOutputEnabled", parsed.debugMapOutputEnabled);
    (void)json.getBool("saveOutputs", parsed.saveOutputs);
    (void)json.getBool("phaseDiagnosticsEnabled", parsed.phaseDiagnosticsEnabled);
    (void)json.getBool("matchingDiagnosticsEnabled", parsed.matchingDiagnosticsEnabled);
    (void)json.getBool("pointCloudStageDiagnosticsEnabled", parsed.pointCloudStageDiagnosticsEnabled);
    (void)json.getBool("saveStagePointClouds", parsed.saveStagePointClouds);
    (void)json.getString("calibResultPath", parsed.calibResultPath);

    Status status = requirePositive("image_width", parsed.imageWidth);
    if (!status.ok()) {
        return status;
    }
    status = requirePositive("image_height", parsed.imageHeight);
    if (!status.ok()) {
        return status;
    }
    status = requirePositive("FREQ", parsed.frequencyCount);
    if (!status.ok()) {
        return status;
    }
    status = requirePositive("STEP", parsed.stepCount);
    if (!status.ok()) {
        return status;
    }
    if (parsed.freqSeries.empty()) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "freqSeries must not be empty"};
    }
    if (parsed.freqSeries.size() != static_cast<std::size_t>(parsed.frequencyCount)) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "freqSeries size must match FREQ"};
    }
    status = requirePositive("freq23", parsed.freq23);
    if (!status.ok()) {
        return status;
    }
    if (parsed.bMin < -1) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "Bmin must be -1 or non-negative"};
    }
    if (parsed.medianKernelSize != -1 && parsed.medianKernelSize <= 0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "winSizeMedian must be -1 or positive"};
    }
    if (parsed.phaseStepCounts.empty()) {
        parsed.phaseStepCounts.assign(static_cast<std::size_t>(parsed.frequencyCount), parsed.stepCount);
    }
    if (parsed.phaseStepCounts.size() != static_cast<std::size_t>(parsed.frequencyCount)) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "phaseStepCounts size must match FREQ"};
    }
    for (int steps : parsed.phaseStepCounts) {
        status = requirePositive("phaseStepCounts", steps);
        if (!status.ok()) {
            return status;
        }
    }
    if (parsed.minZ >= parsed.maxZ) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "minZ must be smaller than maxZ"};
    }
    if (parsed.phaseDiffThreshold <= 0.0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "phaseDiffThreshold must be positive"};
    }
    if (parsed.disparityWindowMinZ <= 0.0 || parsed.disparityWindowMaxZ <= 0.0 ||
        parsed.disparityWindowMinZ >= parsed.disparityWindowMaxZ) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "disparityWindowMinZ must be smaller than disparityWindowMaxZ"};
    }
    if (parsed.matchingMaxCandidateCount <= 0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "matchingMaxCandidateCount must be positive"};
    }
    if (parsed.matchingLeftRightTolerance < 0.0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "matchingLeftRightTolerance must be non-negative"};
    }
    if (parsed.matchingRightPhaseMonotonicRadius < 1 || parsed.matchingRightPhaseMonotonicRadius > 5 ||
        parsed.matchingRightPhaseMinSlope < 0.0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "right phase monotonic radius/slope are invalid"};
    }
    if (parsed.matchingCandidateMinModulation < 0.0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "matchingCandidateMinModulation must be non-negative"};
    }
    if (parsed.disparityLocalConsistencyRadius < 0 || parsed.disparityLocalConsistencyMinSupport < 0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "disparity local consistency radius/support must be non-negative"};
    }
    if (parsed.colorTextureEnabled) {
        if (parsed.colorTextureProjectorIndices.size() != 3) {
            return {StatusCode::ConfigInvalidValue, "ReconsConfig", "colorTextureProjectorIndices must contain B, G, and R projector indices"};
        }
        for (int projectorIndex : parsed.colorTextureProjectorIndices) {
            if (projectorIndex <= 0) {
                return {StatusCode::ConfigInvalidValue, "ReconsConfig", "colorTextureProjectorIndices must be positive"};
            }
        }
        if (parsed.colorCorrectionMatrix.size() != 12) {
            return {StatusCode::ConfigInvalidValue, "ReconsConfig", "colorCorrectionMatrix must contain 12 values for a 3x4 BGR matrix"};
        }
        if (parsed.colorGamma <= 0.0) {
            return {StatusCode::ConfigInvalidValue, "ReconsConfig", "colorGamma must be positive"};
        }
    }
    if (parsed.clear255DilateRadius < 0 || parsed.clear255DilateRadius > 32) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "clear255DilateRadius must be in [0, 32]"};
    }
    if (parsed.clear255 && parsed.colorTextureProjectorIndices.size() != 3) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "clear255 requires three auxiliary projector indices"};
    }
    if (parsed.qualityInfoMinModulation < 0.0 ||
        parsed.qualityInfoPhaseCostThreshold <= 0.0 ||
        parsed.qualityInfoMaxCandidateCount <= 0 ||
        parsed.qualityInfoHoleEdgeRadius < 0 ||
        parsed.qualityInfoHighConfidenceThreshold < 0.0 ||
        parsed.qualityInfoHighConfidenceThreshold > 1.0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "qualityInfo thresholds/counts are invalid"};
    }

    parsed.stripeRequirements.clear();
    int nextProjectorIndex = 1;
    for (int i = 0; i < parsed.frequencyCount; ++i) {
        StripeRequirement requirement;
        requirement.frequencyIndex = i;
        requirement.frequencyValue = parsed.freqSeries[static_cast<std::size_t>(i)];
        requirement.requiredPhaseSteps = parsed.phaseStepCounts[static_cast<std::size_t>(i)];
        requirement.firstProjectorIndex = nextProjectorIndex;
        requirement.phaseStepDirection = parsed.phaseStepDirection;
        parsed.stripeRequirements.push_back(requirement);
        nextProjectorIndex += requirement.requiredPhaseSteps;
    }

    config = std::move(parsed);
    return {};
}

} // namespace

Status loadReconsConfig(const std::string& path, ReconsConfig& config)
{
    JsonObject json("{}");
    Status status = loadConfigJsonObject(path, json);
    if (!status.ok()) {
        return status;
    }

    return parseReconsConfigObject(json, ReconsConfig {}, config);
}

Status loadReconsConfigWithBase(const std::string& basePath,
                                const std::string& overridePath,
                                ReconsConfig& config)
{
    if (basePath.empty()) {
        return loadReconsConfig(overridePath, config);
    }

    ReconsConfig seed;
    Status status = loadReconsConfig(basePath, seed);
    if (!status.ok()) {
        status.message = "base config: " + status.message;
        return status;
    }

    JsonObject overrideJson("{}");
    status = loadConfigJsonObject(overridePath, overrideJson);
    if (!status.ok()) {
        status.message = "override config: " + status.message;
        return status;
    }

    // 数据集 JSON 经常只描述标定/实验参数。显式 base 让生产默认值可继承，
    // 同时保留 loadReconsConfig(path) 的旧配置兼容语义。
    return parseReconsConfigObject(overrideJson, std::move(seed), config);
}

std::string summarizeConfig(const ReconsConfig& config)
{
    std::ostringstream out;
    out << "image_size=" << config.imageWidth << "x" << config.imageHeight
        << ", FREQ=" << config.frequencyCount
        << ", STEP=" << config.stepCount
        << ", freq23=" << config.freq23
        << ", Bmin=" << config.bMin
        << ", winSizeMedian=" << config.medianKernelSize
        << ", freqSeries=[";
    for (std::size_t i = 0; i < config.freqSeries.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << config.freqSeries[i];
    }
    out << "], phaseStepDirection=" << config.phaseStepDirection
        << ", phaseStepCounts=[";
    for (std::size_t i = 0; i < config.phaseStepCounts.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << config.phaseStepCounts[i];
    }
    out << "]"
        << ", phaseUnwrapResidualGateEnabled=" << (config.phaseUnwrapResidualGateEnabled ? "true" : "false")
        << ", phaseUnwrapAbs23ResidualThreshold=" << config.phaseUnwrapAbs23ResidualThreshold
        << ", phaseUnwrapFinalResidualThreshold=" << config.phaseUnwrapFinalResidualThreshold
        << ", phaseFinalMedianFilterEnabled=" << (config.phaseFinalMedianFilterEnabled ? "true" : "false")
        << ", stripeRequirements=" << config.stripeRequirements.size()
        << ", phaseDiffThreshold=" << config.phaseDiffThreshold
        << ", minZ=" << config.minZ
        << ", maxZ=" << config.maxZ
        << ", disparityWindowEnabled=" << (config.disparityWindowEnabled ? "true" : "false")
        << ", disparityWindow=[" << config.disparityWindowMinZ << "," << config.disparityWindowMaxZ
        << "]+/-" << config.disparityWindowMargin
        << ", matchingUniquenessEnabled=" << (config.matchingUniquenessEnabled ? "true" : "false")
        << ", matchingLeftRightConsistencyEnabled=" << (config.matchingLeftRightConsistencyEnabled ? "true" : "false")
        << ", matchingRightPhaseMonotonicEnabled=" << (config.matchingRightPhaseMonotonicEnabled ? "true" : "false")
        << ", matchingRightPhaseMonotonicRadius=" << config.matchingRightPhaseMonotonicRadius
        << ", matchingRightPhaseMinSlope=" << config.matchingRightPhaseMinSlope
        << ", matchingRejectOnSubpixelFailure=" << (config.matchingRejectOnSubpixelFailure ? "true" : "false")
        << ", matchingCandidateQualityFilterEnabled="
        << (config.matchingCandidateQualityFilterEnabled ? "true" : "false")
        << ", matchingCandidateMinModulation=" << config.matchingCandidateMinModulation
        << ", disparitySubpixelEnabled=" << (config.disparitySubpixelEnabled ? "true" : "false")
        << ", disparityLocalConsistencyEnabled=" << (config.disparityLocalConsistencyEnabled ? "true" : "false")
        << ", pointCloudSmoothingEnabled=" << (config.pointCloudSmoothingEnabled ? "true" : "false")
        << ", pointCloudFilterEnabled=" << (config.pointCloudFilterEnabled ? "true" : "false")
        << ", colorTextureEnabled=" << (config.colorTextureEnabled ? "true" : "false")
        << ", colorTextureProjectorIndices=[";
    for (std::size_t i = 0; i < config.colorTextureProjectorIndices.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << config.colorTextureProjectorIndices[i];
    }
    out << "]"
        << ", colorGamma=" << config.colorGamma
        << ", clear255=" << (config.clear255 ? "true" : "false")
        << ", clear255DilateRadius=" << config.clear255DilateRadius
        << ", colorHighlightCompressionEnabled="
        << (config.colorHighlightCompressionEnabled ? "true" : "false")
        << ", qualityInfoEnabled=" << (config.qualityInfoEnabled ? "true" : "false")
        << ", qualityInfoMinModulation=" << config.qualityInfoMinModulation
        << ", qualityInfoPhaseCostThreshold=" << config.qualityInfoPhaseCostThreshold
        << ", qualityInfoMaxCandidateCount=" << config.qualityInfoMaxCandidateCount
        << ", aiEnabled=" << (config.aiEnabled ? "true" : "false")
        << ", debugMapOutputEnabled=" << (config.debugMapOutputEnabled ? "true" : "false")
        << ", saveOutputs=" << (config.saveOutputs ? "true" : "false")
        << ", phaseDiagnosticsEnabled=" << (config.phaseDiagnosticsEnabled ? "true" : "false")
        << ", matchingDiagnosticsEnabled=" << (config.matchingDiagnosticsEnabled ? "true" : "false")
        << ", pointCloudStageDiagnosticsEnabled="
        << (config.pointCloudStageDiagnosticsEnabled ? "true" : "false")
        << ", saveStagePointClouds=" << (config.saveStagePointClouds ? "true" : "false");
    if (!config.calibResultPath.empty()) {
        out << ", calibResultPath=" << config.calibResultPath;
    }
    return out.str();
}

} // namespace reconstruct_one_frame
