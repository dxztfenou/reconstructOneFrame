#pragma once

#include "calibration_model/CalibrationModel.h"
#include "io/JsonFile.h"

#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct CalibrationJsonReadResult {
    Status status;
    CalibrationModel model;
};

CalibrationJsonReadResult parseCalibrationJson(const JsonObject& json, const std::string& sourcePath);
bool getFirstDoubleArray(const JsonObject& json,
                         const std::vector<std::string>& keys,
                         std::vector<double>& value,
                         std::string& matchedKey);
bool getFirstIntArray(const JsonObject& json,
                      const std::vector<std::string>& keys,
                      std::vector<int>& value,
                      std::string& matchedKey);

} // namespace reconstruct_one_frame
