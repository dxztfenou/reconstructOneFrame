#include "calibration_model/CalibrationModel.h"

namespace reconstruct_one_frame {

Status loadCalibrationResultJson(const std::string& path, CalibrationModel& model)
{
    if (path.empty()) {
        return {StatusCode::CalibrationMissing, "CalibrationModel", "calibResult.json path is empty"};
    }

    JsonObject json("{}");
    Status status = loadJsonObjectFromFile(path, json);
    if (status.code == StatusCode::ConfigMissing) {
        return {StatusCode::CalibrationMissing, "CalibrationModel", "calibResult.json not found: " + path};
    }
    if (status.code == StatusCode::ConfigParseFailed) {
        return {StatusCode::CalibrationParseFailed, "CalibrationModel", "invalid calibResult.json: " + path};
    }
    if (!status.ok()) {
        status.module = "CalibrationModel";
        return status;
    }

    CalibrationModel parsed;
    parsed.sourcePath = path;
    std::vector<int> imageSize;
    if (json.getIntArray("image_size", imageSize) && imageSize.size() == 2) {
        parsed.imageWidth = imageSize[0];
        parsed.imageHeight = imageSize[1];
    }

    model = std::move(parsed);
    return {};
}

} // namespace reconstruct_one_frame
