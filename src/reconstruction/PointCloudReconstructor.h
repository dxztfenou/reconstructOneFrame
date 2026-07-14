#pragma once

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"
#include "phase/PhaseUnwrapper.h"

#include <cstdint>
#include <memory>
#include <string>
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
    bool semanticBackground = false;
    bool highFrequencySaturated = false;
    bool highFrequencyLowLight = false;
};

struct PointCloudOutputOptions {
    bool materializeVertices = true;
    bool materializeQualityGrid = true;
};

struct PointCloudReconstructionResult {
    Status status;
    StageStats stats;
    int width = 0;
    int height = 0;
    std::size_t rawValidPointCount = 0;
    std::size_t filteredGridValidPointCount = 0;
    std::size_t leftRightRejectedPointCount = 0;
    std::size_t rightPhaseMonotonicRejectedPointCount = 0;
    std::size_t clear255RejectedPixelCount = 0;
    bool semanticMaskApplied = false;
    std::string matchingSummary;
    std::vector<PointCloudGridPoint> gridPoints;
    std::vector<PointCloudVertex> vertices;
    // Full rectified BGR preview, independent of point-cloud validity.
    std::vector<std::uint8_t> rectifiedColorBgr;
    std::size_t smoothedGridValidPointCount = 0;
    std::string pointCloudStageSummary;
    bool normalsComputed = false;
};

class PointCloudCudaWorkspace {
public:
    struct Impl;

    PointCloudCudaWorkspace();
    ~PointCloudCudaWorkspace();
    PointCloudCudaWorkspace(PointCloudCudaWorkspace&&) noexcept;
    PointCloudCudaWorkspace& operator=(PointCloudCudaWorkspace&&) noexcept;
    PointCloudCudaWorkspace(const PointCloudCudaWorkspace&) = delete;
    PointCloudCudaWorkspace& operator=(const PointCloudCudaWorkspace&) = delete;
    void reset() noexcept;

    std::unique_ptr<Impl> impl_;
};

PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame,
                                                         const PointCloudOutputOptions& outputOptions = {});
PointCloudReconstructionResult reconstructPointCloudCuda(const UnwrappedPhaseResult& unwrappedPhase,
                                                         const CalibrationModel& calibration,
                                                         const ReconsConfig& config,
                                                         const StripeFrameGroup& frame,
                                                         PointCloudCudaWorkspace& workspace,
                                                         const PointCloudOutputOptions& outputOptions = {});

} // namespace reconstruct_one_frame
