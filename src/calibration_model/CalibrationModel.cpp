#include "calibration_model/CalibrationModel.h"

#include "calibration_model/CalibrationJsonReader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

bool hasYamlExtension(const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".yml" || extension == ".yaml";
}

} // namespace

Status loadCalibrationResultJson(const std::string& path, CalibrationModel& model)
{
    if (path.empty()) {
        return {StatusCode::CalibrationMissing, "CalibrationModel", "calibration path is empty"};
    }
    if (hasYamlExtension(path)) {
        return {StatusCode::CalibrationParseFailed,
                "CalibrationModel",
                "YAML calibration is not supported; provide calibResult.json: " + path};
    }

    std::string text;
    Status status = readTextFile(path, text);
    if (status.code == StatusCode::ConfigMissing) {
        return {StatusCode::CalibrationMissing, "CalibrationModel", "calibration file not found: " + path};
    }
    if (!status.ok()) {
        status.module = "CalibrationModel";
        return status;
    }

    if (!looksLikeJsonObject(text)) {
        return {StatusCode::CalibrationParseFailed,
                "CalibrationModel",
                "calibration must contain a JSON object; YAML is not supported: " + path};
    }
    CalibrationJsonReadResult parsed = parseCalibrationJson(JsonObject(text), path);
    if (!parsed.status.ok()) {
        return parsed.status;
    }

    model = std::move(parsed.model);
    return {};
}

Status validateCalibrationImageSize(const CalibrationModel& model, int expectedWidth, int expectedHeight)
{
    if (model.imageWidth == 0 || model.imageHeight == 0) {
        return {};
    }
    if (model.imageWidth != expectedWidth || model.imageHeight != expectedHeight) {
        std::ostringstream message;
        message << "calibration image size " << model.imageWidth << "x" << model.imageHeight
                << " does not match config image size " << expectedWidth << "x" << expectedHeight;
        return {StatusCode::CalibrationImageSizeMismatch, "CalibrationModel", message.str()};
    }
    return {};
}

std::string summarizeCalibration(const CalibrationModel& model)
{
    std::ostringstream out;
    out << "source=" << model.sourcePath
        << ", image_size=" << model.imageWidth << "x" << model.imageHeight
        << ", leftIntrinsics=" << model.leftIntrinsics.size()
        << ", rightIntrinsics=" << model.rightIntrinsics.size()
        << ", leftDistortion=" << model.leftDistortion.size()
        << ", rightDistortion=" << model.rightDistortion.size()
        << ", R=" << model.rotation.size()
        << ", T=" << model.translation.size()
        << ", R_L=" << model.rectificationLeft.size()
        << ", R_R=" << model.rectificationRight.size()
        << ", P_L=" << model.projectionLeft.size()
        << ", P_R=" << model.projectionRight.size()
        << ", Q=" << model.qMatrix.size();
    if (!model.matchedKeys.empty()) {
        out << ", matchedKeys=[";
        for (std::size_t i = 0; i < model.matchedKeys.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << model.matchedKeys[i];
        }
        out << "]";
    }
    return out.str();
}

} // namespace reconstruct_one_frame
