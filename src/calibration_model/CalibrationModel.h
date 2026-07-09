#pragma once

#include "io/JsonFile.h"

#include <array>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct CalibrationModel {
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<double> leftIntrinsics;
    std::vector<double> rightIntrinsics;
    std::vector<double> leftDistortion;
    std::vector<double> rightDistortion;
    std::vector<double> rotation;
    std::vector<double> translation;
    std::vector<double> qMatrix;
    std::string sourcePath;
};

Status loadCalibrationResultJson(const std::string& path, CalibrationModel& model);

} // namespace reconstruct_one_frame
