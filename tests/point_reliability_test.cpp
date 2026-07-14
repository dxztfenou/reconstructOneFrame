#include "quality/PointReliability.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace reconstruct_one_frame;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

PointCloudReconstructionResult makePointCloud()
{
    PointCloudReconstructionResult result;
    result.status = {};
    result.width = 4;
    result.height = 4;
    result.gridPoints.assign(16, {});
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * result.width + x;
            PointCloudGridPoint point;
            point.x = static_cast<float>(x);
            point.y = static_cast<float>(y);
            point.z = 100.0F;
            point.matchCost = 0.01F;
            point.candidateCount = 1;
            result.gridPoints[idx] = point;
        }
    }

    result.gridPoints[5].matchCost = 0.5F;
    result.gridPoints[6].candidateCount = 5;
    result.gridPoints[10].z = 0.0F;
    result.gridPoints[10].x = 0.0F;
    result.gridPoints[10].y = 0.0F;
    return result;
}

void testQualityReasonsAndSentinel()
{
    ReconsConfig config;
    config.imageWidth = 4;
    config.imageHeight = 4;
    config.minZ = 50.0;
    config.maxZ = 160.0;
    config.qualityInfoEnabled = true;
    config.qualityInfoReasonChannelEnabled = true;
    config.qualityInfoUseModulation = false;
    config.qualityInfoPhaseCostThreshold = 0.2;
    config.qualityInfoMaxCandidateCount = 3;
    config.qualityInfoHoleEdgeRadius = 1;

    PointReliabilityResult result = evaluatePointReliability(makePointCloud(), config);
    check(result.status.ok(), "quality evaluation succeeds");
    check(result.map.width == 4 && result.map.height == 4, "quality map keeps input size");
    check(result.map.pixels.size() == 16, "quality map has one pixel per input pixel");

    check((result.map.pixels[5].reason & ReasonHighMatchCost) != 0U, "high match cost reason is set");
    check(result.map.pixels[5].score == 0.0, "high match cost zeros quality");
    check((result.map.pixels[6].reason & ReasonCandidateAmbiguous) != 0U, "candidate ambiguity reason is set");
    check(result.map.pixels[6].score <= 0.6, "candidate ambiguity lowers quality");
    check((result.map.pixels[10].reason & ReasonDepthRange) != 0U, "invalid depth reason is set");
    check((result.map.pixels[0].reason & ReasonDepthRange) != 0U, "sentinel stores aggregate reason bits");
    check(result.map.pixels[0].score == result.frameStats.frameQualityScore, "sentinel stores frame score");
    check(result.frameStats.validPoints > 0, "summary counts valid points");
    check(result.frameStats.lowQualityReasonBits != ReasonNone, "summary accumulates low quality reasons");
    check(result.summary.find("frameQualityScore=") != std::string::npos, "summary formats frame score");

    PointCloudReconstructionResult maskedPointCloud = makePointCloud();
    maskedPointCloud.semanticMaskApplied = true;
    maskedPointCloud.gridPoints[5].semanticBackground = true;
    PointReliabilityResult masked = evaluatePointReliability(maskedPointCloud, config);
    check(masked.map.pixels[1].semanticClass == 1.0,
          "active clear255 mask marks valid pixels as semantic foreground");
    check(masked.map.pixels[5].semanticClass == 0.0 &&
              (masked.map.pixels[5].reason & ReasonSemanticBackground) != 0U,
          "active clear255 mask preserves semantic background rejection");

    config.qualityInfoReasonChannelEnabled = false;
    PointReliabilityResult reasonsDisabled = evaluatePointReliability(maskedPointCloud, config);
    check(reasonsDisabled.map.pixels[5].reason == ReasonNone,
          "disabled reason channel clears per-pixel reasons");
    check((reasonsDisabled.map.pixels[0].reason &
              (ReasonHighMatchCost | ReasonCandidateAmbiguous | ReasonSemanticBackground)) == 0U,
          "disabled reason channel keeps pixel reasons out of the frame sentinel");
}

} // namespace

int main()
{
    testQualityReasonsAndSentinel();
    return EXIT_SUCCESS;
}
