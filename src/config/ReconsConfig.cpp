#include "config/ReconsConfig.h"

#include <sstream>

namespace reconstruct_one_frame {

namespace {

Status requirePositive(const char* key, int value)
{
    if (value <= 0) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", std::string(key) + " must be positive"};
    }
    return {};
}

} // namespace

Status loadReconsConfig(const std::string& path, ReconsConfig& config)
{
    JsonObject json("{}");
    Status status = loadJsonObjectFromFile(path, json);
    if (status.code == StatusCode::ConfigMissing) {
        status.module = "ReconsConfig";
        return status;
    }
    if (!status.ok()) {
        status.module = "ReconsConfig";
        return status;
    }

    ReconsConfig parsed;
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
    (void)json.getInt("phaseStepDirection", parsed.phaseStepDirection);
    (void)json.getDouble("minZ", parsed.minZ);
    (void)json.getDouble("maxZ", parsed.maxZ);
    (void)json.getBool("aiEnabled", parsed.aiEnabled);
    (void)json.getBool("debugMapOutputEnabled", parsed.debugMapOutputEnabled);
    (void)json.getBool("qualityInfoEnabled", parsed.qualityInfoEnabled);
    (void)json.getBool("saveOutputs", parsed.saveOutputs);
    (void)json.getString("calibResultPath", parsed.calibResultPath);

    status = requirePositive("image_width", parsed.imageWidth);
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
    if (parsed.minZ >= parsed.maxZ) {
        return {StatusCode::ConfigInvalidValue, "ReconsConfig", "minZ must be smaller than maxZ"};
    }

    config = std::move(parsed);
    return {};
}

std::string summarizeConfig(const ReconsConfig& config)
{
    std::ostringstream out;
    out << "image_size=" << config.imageWidth << "x" << config.imageHeight
        << ", FREQ=" << config.frequencyCount
        << ", STEP=" << config.stepCount
        << ", freqSeries=[";
    for (std::size_t i = 0; i < config.freqSeries.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << config.freqSeries[i];
    }
    out << "], phaseStepDirection=" << config.phaseStepDirection
        << ", minZ=" << config.minZ
        << ", maxZ=" << config.maxZ
        << ", aiEnabled=" << (config.aiEnabled ? "true" : "false")
        << ", debugMapOutputEnabled=" << (config.debugMapOutputEnabled ? "true" : "false")
        << ", qualityInfoEnabled=" << (config.qualityInfoEnabled ? "true" : "false")
        << ", saveOutputs=" << (config.saveOutputs ? "true" : "false");
    if (!config.calibResultPath.empty()) {
        out << ", calibResultPath=" << config.calibResultPath;
    }
    return out.str();
}

} // namespace reconstruct_one_frame
