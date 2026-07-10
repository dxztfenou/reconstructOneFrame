#pragma once

#include "io/JsonFile.h"

#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct StripeRequirement {
    int frequencyIndex = -1;
    int frequencyValue = 0;
    int requiredPhaseSteps = 0;
    int firstProjectorIndex = 1;
    int phaseStepDirection = -1;
};

struct ReconsConfig {
    int imageWidth = 424;
    int imageHeight = 400;
    int frequencyCount = 3;
    int stepCount = 5;
    std::vector<int> freqSeries = {15, 21, 28};
    int freq23 = 7;
    int bMin = 1;
    int medianKernelSize = 5;
    std::vector<int> phaseStepCounts;
    int phaseStepDirection = -1;
    bool phaseUnwrapResidualGateEnabled = false;
    double phaseUnwrapAbs23ResidualThreshold = 0.2;
    double phaseUnwrapFinalResidualThreshold = 0.2;
    bool phaseFinalMedianFilterEnabled = false;
    double phaseDiffThreshold = 0.2;
    double minZ = 50.0;
    double maxZ = 160.0;
    bool disparityWindowEnabled = false;
    double disparityWindowMinZ = 85.0;
    double disparityWindowMaxZ = 150.0;
    double disparityWindowMargin = 10.0;
    bool matchingUniquenessEnabled = false;
    int matchingMaxCandidateCount = 3;
    double matchingMinSecondBestGap = 0.05;
    bool matchingLeftRightConsistencyEnabled = false;
    double matchingLeftRightTolerance = 1.5;
    bool matchingRightPhaseMonotonicEnabled = false;
    int matchingRightPhaseMonotonicRadius = 1;
    double matchingRightPhaseMinSlope = 0.0001;
    bool disparitySubpixelEnabled = false;
    bool disparityLocalConsistencyEnabled = false;
    double disparityLocalConsistencyThreshold = 6.0;
    int disparityLocalConsistencyRadius = 2;
    int disparityLocalConsistencyMinSupport = 6;
    bool pointCloudSmoothingEnabled = false;
    bool pointCloudFilterEnabled = false;
    bool colorTextureEnabled = false;
    std::vector<int> colorTextureProjectorIndices = {16, 17, 18};
    std::vector<double> colorCorrectionMatrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0
    };
    double colorGamma = 1.0;
    bool qualityInfoEnabled = true;
    double qualityInfoMinModulation = 8.0;
    bool qualityInfoReasonChannelEnabled = true;
    double qualityInfoPhaseCostThreshold = 0.2;
    int qualityInfoMaxCandidateCount = 3;
    int qualityInfoHoleEdgeRadius = 1;
    double qualityInfoHighConfidenceThreshold = 0.90;
    bool qualityInfoUseMatchCost = true;
    bool qualityInfoUseCandidateCount = true;
    bool qualityInfoUseModulation = true;
    bool aiEnabled = false;
    bool debugMapOutputEnabled = false;
    bool saveOutputs = false;
    bool saveStagePointClouds = false;
    std::string calibResultPath;
    std::vector<StripeRequirement> stripeRequirements;
    std::string rawJson;
};

Status loadReconsConfig(const std::string& path, ReconsConfig& config);
std::string summarizeConfig(const ReconsConfig& config);

} // namespace reconstruct_one_frame
