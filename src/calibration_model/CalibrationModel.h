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
    std::vector<double> rectificationLeft;
    std::vector<double> rectificationRight;
    std::vector<double> projectionLeft;
    std::vector<double> projectionRight;
    std::vector<double> qMatrix;
    std::vector<std::string> matchedKeys;
    std::string sourcePath;
};

Status loadCalibrationResultJson(const std::string& path, CalibrationModel& model);
Status validateCalibrationImageSize(const CalibrationModel& model, int expectedWidth, int expectedHeight);
std::string summarizeCalibration(const CalibrationModel& model);

} // namespace reconstruct_one_frame
