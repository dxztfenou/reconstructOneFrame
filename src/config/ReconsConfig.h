#pragma once

#include "io/JsonFile.h"

#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct ReconsConfig {
    int imageWidth = 424;
    int imageHeight = 400;
    int frequencyCount = 3;
    int stepCount = 5;
    std::vector<int> freqSeries = {15, 21, 28};
    int phaseStepDirection = -1;
    double minZ = 50.0;
    double maxZ = 160.0;
    bool aiEnabled = false;
    bool debugMapOutputEnabled = false;
    bool qualityInfoEnabled = false;
    bool saveOutputs = false;
    std::string calibResultPath;
    std::string rawJson;
};

Status loadReconsConfig(const std::string& path, ReconsConfig& config);
std::string summarizeConfig(const ReconsConfig& config);

} // namespace reconstruct_one_frame
