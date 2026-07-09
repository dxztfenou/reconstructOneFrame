#include "calibration_model/CalibrationJsonReader.h"

#include <cctype>
#include <regex>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

bool validDistortionLength(std::size_t size)
{
    return size == 4 || size == 5 || size == 8 || size == 12 || size == 14;
}

Status requireSize(const std::vector<double>& values,
                   std::size_t expected,
                   const std::string& field)
{
    if (values.size() != expected) {
        std::ostringstream message;
        message << field << " must contain " << expected << " values, got " << values.size();
        return {StatusCode::CalibrationMatrixShapeInvalid, "CalibrationModel", message.str()};
    }
    return {};
}

Status requireDistortionSize(const std::vector<double>& values, const std::string& field)
{
    if (!validDistortionLength(values.size())) {
        std::ostringstream message;
        message << field << " must contain 4, 5, 8, 12, or 14 values, got " << values.size();
        return {StatusCode::CalibrationMatrixShapeInvalid, "CalibrationModel", message.str()};
    }
    return {};
}

bool extractObjectBody(const std::string& json, const std::string& key, std::string& body)
{
    const std::string escapedKey = std::regex_replace(key, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\\$&)");
    std::smatch match;
    if (!std::regex_search(json, match, std::regex("\"" + escapedKey + R"("\s*:\s*\{)", std::regex::icase))) {
        return false;
    }

    const std::size_t objectStart = static_cast<std::size_t>(match.position(0) + match.length(0) - 1);
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = objectStart; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                body = json.substr(objectStart, i - objectStart + 1);
                return true;
            }
        }
    }
    return false;
}

bool getOpenCvMatrixData(const JsonObject& json, const std::string& key, std::vector<double>& value)
{
    std::string objectBody;
    if (!extractObjectBody(json.raw(), key, objectBody)) {
        return false;
    }
    JsonObject object(objectBody);
    std::vector<double> data;
    if (!object.getDoubleArray("data", data)) {
        return false;
    }
    int rows = 0;
    int cols = 0;
    if (object.getInt("rows", rows) && object.getInt("cols", cols) && rows > 0 && cols > 0 &&
        static_cast<std::size_t>(rows * cols) != data.size()) {
        return false;
    }
    value = std::move(data);
    return true;
}

bool getOpenCvMatrixIntData(const JsonObject& json, const std::string& key, std::vector<int>& value)
{
    std::vector<double> data;
    if (!getOpenCvMatrixData(json, key, data)) {
        return false;
    }
    value.clear();
    value.reserve(data.size());
    for (double item : data) {
        value.push_back(static_cast<int>(item));
    }
    return true;
}

} // namespace

bool getFirstDoubleArray(const JsonObject& json,
                         const std::vector<std::string>& keys,
                         std::vector<double>& value,
                         std::string& matchedKey)
{
    for (const std::string& key : keys) {
        if (json.getDoubleArray(key, value)) {
            matchedKey = key;
            return true;
        }
        if (getOpenCvMatrixData(json, key, value)) {
            matchedKey = key;
            return true;
        }
    }
    return false;
}

bool getFirstIntArray(const JsonObject& json,
                      const std::vector<std::string>& keys,
                      std::vector<int>& value,
                      std::string& matchedKey)
{
    for (const std::string& key : keys) {
        if (json.getIntArray(key, value)) {
            matchedKey = key;
            return true;
        }
        if (getOpenCvMatrixIntData(json, key, value)) {
            matchedKey = key;
            return true;
        }
    }
    return false;
}

CalibrationJsonReadResult parseCalibrationJson(const JsonObject& json, const std::string& sourcePath)
{
    CalibrationJsonReadResult result;
    result.model.sourcePath = sourcePath;

    std::vector<int> imageSize;
    std::string matchedKey;
    if (getFirstIntArray(json, {"image_size", "imageSize", "size"}, imageSize, matchedKey)) {
        if (imageSize.size() != 2) {
            result.status = {StatusCode::CalibrationMatrixShapeInvalid, "CalibrationModel", matchedKey + " must contain width and height"};
            return result;
        }
        result.model.imageWidth = imageSize[0];
        result.model.imageHeight = imageSize[1];
        result.model.matchedKeys.push_back("image_size:" + matchedKey);
    }

    if (!getFirstDoubleArray(json, {"leftIntrinsics", "cameraMatrixL", "M1", "KK_L"}, result.model.leftIntrinsics, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "left intrinsics field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("leftIntrinsics:" + matchedKey);
    Status status = requireSize(result.model.leftIntrinsics, 9, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (!getFirstDoubleArray(json, {"rightIntrinsics", "cameraMatrixR", "M2", "KK_R"}, result.model.rightIntrinsics, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "right intrinsics field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("rightIntrinsics:" + matchedKey);
    status = requireSize(result.model.rightIntrinsics, 9, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (!getFirstDoubleArray(json, {"leftDistortion", "D1", "Dist_L"}, result.model.leftDistortion, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "left distortion field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("leftDistortion:" + matchedKey);
    status = requireDistortionSize(result.model.leftDistortion, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (!getFirstDoubleArray(json, {"rightDistortion", "D2", "Dist_R"}, result.model.rightDistortion, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "right distortion field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("rightDistortion:" + matchedKey);
    status = requireDistortionSize(result.model.rightDistortion, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (!getFirstDoubleArray(json, {"R"}, result.model.rotation, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "stereo rotation field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("R:" + matchedKey);
    status = requireSize(result.model.rotation, 9, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (!getFirstDoubleArray(json, {"T"}, result.model.translation, matchedKey)) {
        result.status = {StatusCode::CalibrationFieldMissing, "CalibrationModel", "stereo translation field is missing"};
        return result;
    }
    result.model.matchedKeys.push_back("T:" + matchedKey);
    status = requireSize(result.model.translation, 3, matchedKey);
    if (!status.ok()) {
        result.status = status;
        return result;
    }

    if (getFirstDoubleArray(json, {"Q"}, result.model.qMatrix, matchedKey)) {
        result.model.matchedKeys.push_back("Q:" + matchedKey);
        status = requireSize(result.model.qMatrix, 16, matchedKey);
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }

    if (getFirstDoubleArray(json, {"R_L", "R1", "leftRectification"}, result.model.rectificationLeft, matchedKey)) {
        result.model.matchedKeys.push_back("R_L:" + matchedKey);
        status = requireSize(result.model.rectificationLeft, 9, matchedKey);
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (getFirstDoubleArray(json, {"R_R", "R2", "rightRectification"}, result.model.rectificationRight, matchedKey)) {
        result.model.matchedKeys.push_back("R_R:" + matchedKey);
        status = requireSize(result.model.rectificationRight, 9, matchedKey);
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (getFirstDoubleArray(json, {"P_L", "P1", "leftProjection"}, result.model.projectionLeft, matchedKey)) {
        result.model.matchedKeys.push_back("P_L:" + matchedKey);
        status = requireSize(result.model.projectionLeft, 12, matchedKey);
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }
    if (getFirstDoubleArray(json, {"P_R", "P2", "rightProjection"}, result.model.projectionRight, matchedKey)) {
        result.model.matchedKeys.push_back("P_R:" + matchedKey);
        status = requireSize(result.model.projectionRight, 12, matchedKey);
        if (!status.ok()) {
            result.status = status;
            return result;
        }
    }

    result.status = {};
    return result;
}

} // namespace reconstruct_one_frame
