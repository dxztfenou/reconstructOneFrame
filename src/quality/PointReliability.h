#pragma once

#include "config/ReconsConfig.h"
#include "reconstruction/PointCloudReconstructor.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

enum PointReliabilityReason : unsigned int {
    ReasonNone = 0U,
    ReasonSemanticBackground = 1U << 0U,
    ReasonLowModulation = 1U << 1U,
    ReasonSaturation = 1U << 2U,
    ReasonPhaseFitResidual = 1U << 3U,
    ReasonUnwrapInconsistent = 1U << 4U,
    ReasonHighMatchCost = 1U << 5U,
    ReasonLeftRightInconsistent = 1U << 6U,
    ReasonCandidateAmbiguous = 1U << 7U,
    ReasonDepthRange = 1U << 8U,
    ReasonLocalDiscontinuity = 1U << 9U,
    ReasonHoleEdge = 1U << 10U,
    ReasonSurfaceNormal = 1U << 11U
};

constexpr double kDefaultFrameHighConfidenceThreshold = 0.90;
constexpr double kDefaultFrameCoverageTargetRatio = 0.55;
constexpr double kDefaultFrameCenterCoverageTargetRatio = 0.65;
constexpr int kPointReliabilityReasonBitCount = 32;

struct PointReliabilityPixel {
    double semanticClass = 0.0;
    double score = 0.0;
    unsigned int reason = ReasonNone;
};

struct SingleFrameQualityStats {
    int totalPixels = 0;
    int centerPixels = 0;
    int validPoints = 0;
    int centerValidPoints = 0;
    int highConfidencePoints = 0;
    int largestConnectedComponentPoints = 0;
    int boundaryValidPoints = 0;
    double validCoverageRatio = 0.0;
    double centerCoverageRatio = 0.0;
    double largestConnectedComponentRatio = 0.0;
    double boundaryRatio = 0.0;
    double highConfidenceRatio = 0.0;
    double meanScore = 0.0;
    double p50Score = 0.0;
    double p90Score = 0.0;
    double p95Score = 0.0;
    double coverageScore = 0.0;
    double centerCoverageScore = 0.0;
    double connectedSurfaceScore = 0.0;
    double pointQualityScore = 0.0;
    double frameQualityScore = 0.0;
    unsigned int lowQualityReasonBits = ReasonNone;
    std::array<int, kPointReliabilityReasonBitCount> reasonHistogram{};
};

struct PointReliabilityMap {
    int width = 0;
    int height = 0;
    std::vector<PointReliabilityPixel> pixels;
};

struct PointReliabilityResult {
    Status status;
    StageStats stats;
    PointReliabilityMap map;
    SingleFrameQualityStats frameStats;
    std::string summary;
};

PointReliabilityResult evaluatePointReliability(const PointCloudReconstructionResult& pointCloud,
                                                const ReconsConfig& config);

std::string formatFrameQualitySummary(const SingleFrameQualityStats& stats);

} // namespace reconstruct_one_frame
