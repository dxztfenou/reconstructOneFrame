#include "calibration_model/CalibrationModel.h"

#include "calibration_model/CalibrationJsonReader.h"

#include <regex>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

bool extractYamlMatrix(const std::string& text, const std::string& key, std::vector<double>& values)
{
    const std::string escapedKey = std::regex_replace(key, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\\$&)");
    const std::regex matrixRegex(
        "(^|\\n)\\s*" + escapedKey
            + R"(\s*:\s*!!opencv-matrix\s*\n\s*rows\s*:\s*\d+\s*\n\s*cols\s*:\s*\d+\s*\n\s*dt\s*:\s*\w+\s*\n\s*data\s*:\s*\[([^\]]*)\])",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, matrixRegex)) {
        return false;
    }

    values.clear();
    const std::string body = match[2].str();
    const std::regex numberRegex(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), numberRegex);
         it != std::sregex_iterator();
         ++it) {
        values.push_back(std::stod((*it).str()));
    }
    return true;
}

bool extractYamlImageSize(const std::string& text, int& width, int& height)
{
    const std::regex imageSizeRegex(R"((^|\n)\s*image_size\s*:\s*\[\s*(\d+)\s*,\s*(\d+)\s*\])",
                                    std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, imageSizeRegex)) {
        return false;
    }
    width = std::stoi(match[2].str());
    height = std::stoi(match[3].str());
    return true;
}

Status requireMatrixSize(const std::vector<double>& values,
                         std::size_t expected,
                         const std::string& key)
{
    if (values.size() != expected) {
        std::ostringstream message;
        message << key << " must contain " << expected << " values, got " << values.size();
        return {StatusCode::CalibrationMatrixShapeInvalid, "CalibrationModel", message.str()};
    }
    return {};
}

Status requireDistortionSize(const std::vector<double>& values, const std::string& key)
{
    const std::size_t size = values.size();
    if (!(size == 4 || size == 5 || size == 8 || size == 12 || size == 14)) {
        std::ostringstream message;
        message << key << " must contain 4, 5, 8, 12, or 14 values, got " << size;
        return {StatusCode::CalibrationMatrixShapeInvalid, "CalibrationModel", message.str()};
    }
    return {};
}

CalibrationJsonReadResult parseCalibrationYaml(const std::string& text, const std::string& sourcePath)
{
    CalibrationJsonReadResult result;
    result.model.sourcePath = sourcePath;
    (void)extractYamlImageSize(text, result.model.imageWidth, result.model.imageHeight);
    if (result.model.imageWidth > 0 && result.model.imageHeight > 0) {
        result.model.matchedKeys.push_back("image_size:image_size");
    }

    auto readRequired = [&](const std::string& key,
                            std::vector<double>& target,
                            std::size_t expected,
                            const std::string& label) -> Status {
        if (!extractYamlMatrix(text, key, target)) {
            return {StatusCode::CalibrationFieldMissing, "CalibrationModel", label + " field is missing"};
        }
        result.model.matchedKeys.push_back(label + ":" + key);
        return requireMatrixSize(target, expected, key);
    };

    Status status = readRequired("KK_L", result.model.leftIntrinsics, 9, "leftIntrinsics");
    if (!status.ok()) {
        result.status = status;
        return result;
    }
    status = readRequired("KK_R", result.model.rightIntrinsics, 9, "rightIntrinsics");
    if (!status.ok()) {
        result.status = status;
        return result;
    }
    if (!extractYamlMatrix(text, "Dist_L", result.model.leftDistortion)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "left distortion field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("leftDistortion:Dist_L");
    status = requireDistortionSize(result.model.leftDistortion, "Dist_L");
    if (!status.ok()) {
        result.status = status;
        return result;
    }
    if (!extractYamlMatrix(text, "Dist_R", result.model.rightDistortion)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "right distortion field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("rightDistortion:Dist_R");
    status = requireDistortionSize(result.model.rightDistortion, "Dist_R");
    if (!status.ok()) {
        result.status = status;
        return result;
    }
    status = readRequired("R", result.model.rotation, 9, "R");
    if (!status.ok()) {
        result.status = status;
        return result;
    }
    status = readRequired("T", result.model.translation, 3, "T");
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (extractYamlMatrix(text, "R_L", result.model.rectificationLeft)) {
        result.model.matchedKeys.push_back("R_L:R_L");
        status = requireMatrixSize(result.model.rectificationLeft, 9, "R_L");
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (extractYamlMatrix(text, "R_R", result.model.rectificationRight)) {
        result.model.matchedKeys.push_back("R_R:R_R");
        status = requireMatrixSize(result.model.rectificationRight, 9, "R_R");
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (extractYamlMatrix(text, "P_L", result.model.projectionLeft)) {
        result.model.matchedKeys.push_back("P_L:P_L");
        status = requireMatrixSize(result.model.projectionLeft, 12, "P_L");
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (extractYamlMatrix(text, "P_R", result.model.projectionRight)) {
        result.model.matchedKeys.push_back("P_R:P_R");
        status = requireMatrixSize(result.model.projectionRight, 12, "P_R");
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (extractYamlMatrix(text, "Q", result.model.qMatrix)) {
        result.model.matchedKeys.push_back("Q:Q");
        status = requireMatrixSize(result.model.qMatrix, 16, "Q");
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }

    result.status = {};
    return result;
}

} // namespace

Status loadCalibrationResultJson(const std::string& path, CalibrationModel& model)
{
    if (path.empty()) {
        return {StatusCode::CalibrationMissing, "CalibrationModel", "calibration path is empty"};
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

    CalibrationJsonReadResult parsed;
    if (looksLikeJsonObject(text)) {
        parsed = parseCalibrationJson(JsonObject(text), path);
    } else {
        parsed = parseCalibrationYaml(text, path);
    }
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
