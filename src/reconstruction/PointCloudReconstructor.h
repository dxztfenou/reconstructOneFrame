#pragma once

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "phase/PhaseUnwrapper.h"

#include <cstdint>
#include <vector>

namespace reconstruct_one_frame {

struct PointCloudVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 0.0F;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    int u = 0;
    int v = 0;
};

struct PointCloudGridPoint {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 0.0F;
    float matchCost = 0.0F;
    float modulation = 0.0F;
    int candidateCount = 0;
};

struct PointCloudReconstructionResult {
    Status status;
    StageStats stats;
    int width = 0;
    int height = 0;
    std::size_t rawValidPointCount = 0;
    std::size_t filteredGridValidPointCount = 0;
    std::vector<PointCloudGridPoint> gridPoints;
    std::vector<PointCloudVertex> vertices;
};

PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame);

} // namespace reconstruct_one_frame
