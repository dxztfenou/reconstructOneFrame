#include "reconstruction/PointCloudReconstructor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace reconstruct_one_frame;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

UnwrappedPhaseResult makePhase(bool flatRightPhase)
{
    constexpr int width = 16;
    constexpr int height = 8;
    UnwrappedPhaseResult phase;
    phase.status = {};
    phase.left.camera = CameraSide::Left;
    phase.left.width = width;
    phase.left.height = height;
    phase.left.absolutePhase.assign(width * height, 0.0F);
    phase.right.camera = CameraSide::Right;
    phase.right.width = width;
    phase.right.height = height;
    phase.right.absolutePhase.assign(width * height, 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            phase.left.absolutePhase[y * width + x] = static_cast<float>(x) * 0.1F + 0.05F;
            phase.right.absolutePhase[y * width + x] =
                flatRightPhase ? 0.4F : static_cast<float>(x + 2) * 0.1F;
        }
    }
    return phase;
}

UnwrappedPhaseResult makeFlatEqualPhase()
{
    UnwrappedPhaseResult phase = makePhase(false);
    for (int y = 0; y < phase.left.height; ++y) {
        for (int x = 0; x < phase.left.width; ++x) {
            phase.left.absolutePhase[y * phase.left.width + x] = static_cast<float>(x) * 0.1F;
            phase.right.absolutePhase[y * phase.right.width + x] = 0.4F;
        }
    }
    return phase;
}

CalibrationModel makeCalibration()
{
    CalibrationModel calibration;
    calibration.imageWidth = 16;
    calibration.imageHeight = 8;
    calibration.qMatrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1000.0,
        0.0, 0.0, 5.0, 0.0
    };
    return calibration;
}

StripeFrameGroup makeFrame()
{
    static std::vector<unsigned char> color(16 * 8, 180);
    StripeFrameGroup frame;
    ImageView image;
    image.data = color.data();
    image.width = 16;
    image.height = 8;
    image.channels = 1;
    image.strideBytes = 16;
    image.elementType = ImageElementType::UInt8;
    frame.leftStripes.push_back({CameraSide::Left, 2, 4, 13, image});
    return frame;
}

ReconsConfig makeConfig()
{
    ReconsConfig config;
    config.imageWidth = 16;
    config.imageHeight = 8;
    config.phaseDiffThreshold = 0.051;
    config.minZ = 40.0;
    config.maxZ = 160.0;
    config.disparityWindowEnabled = false;
    config.matchingUniquenessEnabled = false;
    config.matchingLeftRightConsistencyEnabled = true;
    config.matchingLeftRightTolerance = 0.6;
    config.matchingRightPhaseMonotonicEnabled = true;
    config.matchingRightPhaseMonotonicRadius = 1;
    config.matchingRightPhaseMinSlope = 0.0001;
    config.disparitySubpixelEnabled = true;
    config.disparityLocalConsistencyEnabled = false;
    config.pointCloudSmoothingEnabled = false;
    config.pointCloudFilterEnabled = false;
    return config;
}

UnwrappedPhaseResult makeDensePhaseWithCenterHole()
{
    constexpr int width = 32;
    constexpr int height = 32;
    UnwrappedPhaseResult phase;
    phase.status = {};
    phase.left.camera = CameraSide::Left;
    phase.left.width = width;
    phase.left.height = height;
    phase.left.absolutePhase.assign(width * height, 0.0F);
    phase.right.camera = CameraSide::Right;
    phase.right.width = width;
    phase.right.height = height;
    phase.right.absolutePhase.assign(width * height, 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            phase.left.absolutePhase[y * width + x] = static_cast<float>(x) * 0.1F + 0.05F;
            phase.right.absolutePhase[y * width + x] = static_cast<float>(x + 2) * 0.1F;
        }
    }
    phase.left.absolutePhase[(height / 2) * width + width / 2] =
        std::numeric_limits<float>::quiet_NaN();
    return phase;
}

UnwrappedPhaseResult makeDenseCheckerboardPhase()
{
    UnwrappedPhaseResult phase = makeDensePhaseWithCenterHole();
    for (int y = 0; y < phase.left.height; ++y) {
        for (int x = 0; x < phase.left.width; ++x) {
            if (((x + y) & 1) != 0) {
                phase.left.absolutePhase[y * phase.left.width + x] =
                    std::numeric_limits<float>::quiet_NaN();
            } else {
                phase.left.absolutePhase[y * phase.left.width + x] =
                    static_cast<float>(x) * 0.1F + 0.05F;
            }
        }
    }
    return phase;
}

CalibrationModel makeDenseCalibration()
{
    CalibrationModel calibration;
    calibration.imageWidth = 32;
    calibration.imageHeight = 32;
    calibration.qMatrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1000.0,
        0.0, 0.0, 10.0, 0.0
    };
    return calibration;
}

CalibrationModel makeDenseCheckerboardCalibration()
{
    CalibrationModel calibration = makeDenseCalibration();
    calibration.qMatrix[14] = 15.0;
    return calibration;
}

StripeFrameGroup makeDenseFrame()
{
    static std::vector<unsigned char> color(32 * 32, 180);
    StripeFrameGroup frame;
    ImageView image;
    image.data = color.data();
    image.width = 32;
    image.height = 32;
    image.channels = 1;
    image.strideBytes = 32;
    image.elementType = ImageElementType::UInt8;
    frame.leftStripes.push_back({CameraSide::Left, 2, 4, 13, image});
    return frame;
}

ReconsConfig makeDenseConfig()
{
    ReconsConfig config = makeConfig();
    config.imageWidth = 32;
    config.imageHeight = 32;
    config.matchingLeftRightConsistencyEnabled = false;
    config.matchingRightPhaseMonotonicEnabled = false;
    config.disparitySubpixelEnabled = true;
    return config;
}

int reflectCoordinate(int coordinate, int size)
{
    if (coordinate < 0) {
        coordinate = -coordinate;
    }
    if (coordinate >= size) {
        coordinate = 2 * size - coordinate - 1;
    }
    return coordinate;
}

float expectedLegacySmoothedZ(const std::vector<PointCloudGridPoint>& points,
                              int width,
                              int height,
                              int x,
                              int y)
{
    constexpr float weights[9] = {
        1.96519161e-05F, 2.39409349e-04F, 1.07295826e-03F,
        1.76900911e-03F, 1.07295826e-03F, 2.39409349e-04F,
        1.96519161e-05F, 2.39409349e-04F, 2.91660295e-03F
    };
    float weightedZ = 0.0F;
    float weightSum = 0.0F;
    int weightIndex = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx, ++weightIndex) {
            const int sampleX = reflectCoordinate(x + dx, width);
            const int sampleY = reflectCoordinate(y + dy, height);
            const float weight = weights[weightIndex];
            weightedZ += points[sampleY * width + sampleX].z * weight;
            weightSum += weight;
        }
    }
    return weightedZ / weightSum;
}

} // namespace

int main()
{
    const CalibrationModel calibration = makeCalibration();
    const StripeFrameGroup frame = makeFrame();

    ReconsConfig config = makeConfig();
    PointCloudReconstructionResult crossing =
        reconstructPointCloudCuda(makePhase(false), calibration, config, frame);
    require(crossing.status.ok(), "expected monotonic phase crossing to reconstruct");
    require(!crossing.vertices.empty(), "expected reconstructed vertices for monotonic crossing");
    require(crossing.rawValidPointCount > 0, "expected raw valid points");
    require(crossing.filteredGridValidPointCount == crossing.vertices.size(), "expected filtered grid count to match vertices");
    require(crossing.leftRightRejectedPointCount == 0, "expected subpixel left-right consistency to retain crossing");
    require(crossing.rightPhaseMonotonicRejectedPointCount == 8,
            "expected only one unsupported right-edge candidate per row");
    require(!crossing.matchingSummary.empty(), "expected matching diagnostic summary");

    ReconsConfig tightLeftRight = config;
    tightLeftRight.matchingLeftRightTolerance = 0.4;
    PointCloudReconstructionResult tightLeftRightResult =
        reconstructPointCloudCuda(makePhase(false), calibration, tightLeftRight, frame);
    require(tightLeftRightResult.status.code == StatusCode::ReconstructionInsufficient,
            "expected tighter-than-half-pixel left-right tolerance to reject synthetic matches");
    require(tightLeftRightResult.leftRightRejectedPointCount > 0,
            "expected left-right rejection counter");

    ReconsConfig flatWithoutMonotonic = config;
    flatWithoutMonotonic.matchingLeftRightConsistencyEnabled = false;
    flatWithoutMonotonic.matchingRightPhaseMonotonicEnabled = false;
    PointCloudReconstructionResult flatAccepted =
        reconstructPointCloudCuda(makePhase(true), calibration, flatWithoutMonotonic, frame);
    require(flatAccepted.status.ok(), "expected flat phase candidates to pass without monotonic constraint");
    require(!flatAccepted.vertices.empty(), "expected flat phase candidates before monotonic rejection");

    ReconsConfig flatWithMonotonic = flatWithoutMonotonic;
    flatWithMonotonic.matchingRightPhaseMonotonicEnabled = true;
    PointCloudReconstructionResult flatRejected =
        reconstructPointCloudCuda(makePhase(true), calibration, flatWithMonotonic, frame);
    require(flatRejected.status.code == StatusCode::ReconstructionInsufficient,
            "expected flat phase candidates to be rejected by monotonic constraint");
    require(flatRejected.rightPhaseMonotonicRejectedPointCount > 0,
            "expected right-phase monotonic rejection counter");

    ReconsConfig flatEqualWithoutMonotonic = config;
    flatEqualWithoutMonotonic.phaseDiffThreshold = 0.001;
    flatEqualWithoutMonotonic.matchingLeftRightConsistencyEnabled = false;
    flatEqualWithoutMonotonic.matchingRightPhaseMonotonicEnabled = false;
    PointCloudReconstructionResult flatEqualAccepted =
        reconstructPointCloudCuda(makeFlatEqualPhase(), calibration, flatEqualWithoutMonotonic, frame);
    require(flatEqualAccepted.status.ok(), "expected exact flat candidate before monotonic rejection");

    ReconsConfig flatEqualWithMonotonic = flatEqualWithoutMonotonic;
    flatEqualWithMonotonic.matchingRightPhaseMonotonicEnabled = true;
    PointCloudReconstructionResult flatEqualRejected =
        reconstructPointCloudCuda(makeFlatEqualPhase(), calibration, flatEqualWithMonotonic, frame);
    require(flatEqualRejected.status.code == StatusCode::ReconstructionInsufficient,
            "expected exact flat candidate to be rejected without local slope");
    require(flatEqualRejected.rightPhaseMonotonicRejectedPointCount > 0,
            "expected exact flat monotonic rejection counter");

    UnwrappedPhaseResult shortPhase = makePhase(false);
    shortPhase.left.absolutePhase.pop_back();
    shortPhase.right.absolutePhase.pop_back();
    PointCloudReconstructionResult shortResult =
        reconstructPointCloudCuda(shortPhase, calibration, config, frame);
    require(shortResult.status.code == StatusCode::MatchingFailed,
            "expected short absolute phase arrays to fail before CUDA copy");

    constexpr int denseWidth = 32;
    constexpr int denseHeight = 32;
    constexpr int holeX = denseWidth / 2;
    constexpr int holeY = denseHeight / 2;
    constexpr int probeX = holeX + 1;
    constexpr int probeY = holeY;
    const int holeIndex = holeY * denseWidth + holeX;
    const int probeIndex = probeY * denseWidth + probeX;
    const UnwrappedPhaseResult densePhase = makeDensePhaseWithCenterHole();
    const CalibrationModel denseCalibration = makeDenseCalibration();
    const StripeFrameGroup denseFrame = makeDenseFrame();

    ReconsConfig denseRawConfig = makeDenseConfig();
    PointCloudReconstructionResult denseRaw =
        reconstructPointCloudCuda(densePhase, denseCalibration, denseRawConfig, denseFrame);
    require(denseRaw.status.ok(), "expected dense synthetic surface to reconstruct");
    require(denseRaw.gridPoints[holeIndex].x == 0.0F &&
                denseRaw.gridPoints[holeIndex].y == 0.0F &&
                denseRaw.gridPoints[holeIndex].z == 0.0F,
            "expected center hole before smoothing");
    require(denseRaw.gridPoints[probeIndex].z > 0.0F,
            "expected valid point next to center hole");

    ReconsConfig denseSmoothConfig = denseRawConfig;
    denseSmoothConfig.pointCloudSmoothingEnabled = true;
    PointCloudReconstructionResult denseSmoothed =
        reconstructPointCloudCuda(densePhase, denseCalibration, denseSmoothConfig, denseFrame);
    require(denseSmoothed.status.ok(), "expected smoothed dense surface to reconstruct");
    require(std::fabs(denseSmoothed.gridPoints[probeIndex].x - denseRaw.gridPoints[probeIndex].x) < 1.0e-6F,
            "expected Legacy smoothing to preserve center X");
    require(std::fabs(denseSmoothed.gridPoints[probeIndex].y - denseRaw.gridPoints[probeIndex].y) < 1.0e-6F,
            "expected Legacy smoothing to preserve center Y");
    const float expectedProbeZ =
        expectedLegacySmoothedZ(denseRaw.gridPoints, denseWidth, denseHeight, probeX, probeY);
    require(std::fabs(denseSmoothed.gridPoints[probeIndex].z - expectedProbeZ) < 1.0e-4F,
            "expected Legacy smoothing to use the historical 3x3 indexing of the 7x7 kernel");
    require(denseSmoothed.gridPoints[holeIndex].x == 0.0F &&
                denseSmoothed.gridPoints[holeIndex].y == 0.0F &&
                denseSmoothed.gridPoints[holeIndex].z > 0.0F,
            "expected Legacy smoothing to create temporary (0,0,z) at a hole");

    ReconsConfig denseFilteredConfig = denseSmoothConfig;
    denseFilteredConfig.pointCloudFilterEnabled = true;
    PointCloudReconstructionResult denseFiltered =
        reconstructPointCloudCuda(densePhase, denseCalibration, denseFilteredConfig, denseFrame);
    require(denseFiltered.status.ok(), "expected dense surface to survive point-cloud filtering");
    require(denseFiltered.gridPoints[holeIndex].x == 0.0F &&
                denseFiltered.gridPoints[holeIndex].y == 0.0F &&
                denseFiltered.gridPoints[holeIndex].z == 0.0F,
            "expected point-cloud filter to remove temporary (0,0,z) hole point");

    PointCloudReconstructionResult checkerboardFiltered =
        reconstructPointCloudCuda(makeDenseCheckerboardPhase(),
                                  makeDenseCheckerboardCalibration(),
                                  denseFilteredConfig,
                                  denseFrame);
    require(checkerboardFiltered.status.ok(),
            "expected Legacy filter to evaluate nonzero smoothed points without reapplying the Z gate");
    require(checkerboardFiltered.gridPoints[holeIndex].z > 0.0F &&
                checkerboardFiltered.gridPoints[holeIndex].z < denseFilteredConfig.minZ,
            "expected locally supported smoothed point below minZ to survive Legacy-compatible filter");
    return EXIT_SUCCESS;
}
