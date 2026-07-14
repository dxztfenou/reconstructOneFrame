#include "config/ReconsConfig.h"
#include "image\InputManifest.h"
#include "phase\WrappedPhaseComputer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
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

ManifestFrame loadManifestFrame(const char* path)
{
    InputManifest manifest;
    Status status = loadInputManifest(path, manifest);
    require(status.ok(), "expected manifest load");
    ManifestFrame frame;
    status = buildFrameFromManifest(manifest, frame);
    require(status.ok(), "expected frame build");
    return frame;
}

struct OwnedStripeFrame {
    StripeFrameGroup frame;
    std::vector<std::vector<std::uint8_t>> storage;
};

OwnedStripeFrame makeSpatialPhaseFrame(const ReconsConfig& config)
{
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr double pi = 3.14159265358979323846;
    OwnedStripeFrame owned;
    std::size_t imageCount = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        imageCount += static_cast<std::size_t>(requirement.requiredPhaseSteps) * 2U;
    }
    owned.storage.resize(imageCount);

    std::size_t storageIndex = 0;
    for (CameraSide camera : {CameraSide::Left, CameraSide::Right}) {
        auto& stripes = camera == CameraSide::Left ? owned.frame.leftStripes : owned.frame.rightStripes;
        for (const StripeRequirement& requirement : config.stripeRequirements) {
            for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
                auto& pixels = owned.storage[storageIndex++];
                pixels.resize(width * height);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const double spatialPhase = 0.35 * static_cast<double>(x) +
                            0.07 * static_cast<double>(requirement.frequencyIndex) +
                            (camera == CameraSide::Left ? 0.0 : 0.11);
                        const double stepAngle = 2.0 * pi * static_cast<double>(step) /
                            static_cast<double>(requirement.requiredPhaseSteps);
                        const int value = static_cast<int>(std::lround(128.0 + 70.0 * std::cos(spatialPhase + stepAngle)));
                        pixels[static_cast<std::size_t>(y * width + x)] =
                            static_cast<std::uint8_t>(std::clamp(value, 0, 255));
                    }
                }
                ImageView image;
                image.data = pixels.data();
                image.width = width;
                image.height = height;
                image.channels = 1;
                image.strideBytes = width;
                image.elementType = ImageElementType::UInt8;
                stripes.push_back({camera,
                                   requirement.frequencyIndex,
                                   step,
                                   requirement.firstProjectorIndex + step,
                                   image});
            }
        }
    }
    return owned;
}

CalibrationModel makeOnePixelShiftCalibration()
{
    CalibrationModel calibration;
    calibration.imageWidth = 4;
    calibration.imageHeight = 4;
    calibration.leftIntrinsics = {1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    calibration.rightIntrinsics = calibration.leftIntrinsics;
    calibration.leftDistortion = {0.0, 0.0, 0.0, 0.0, 0.0};
    calibration.rightDistortion = calibration.leftDistortion;
    calibration.rectificationLeft = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    calibration.rectificationRight = calibration.rectificationLeft;
    calibration.projectionLeft = {1.0, 0.0, 0.0, 0.0,
                                  0.0, 1.0, 0.0, 0.0,
                                  0.0, 0.0, 1.0, 0.0};
    calibration.projectionRight = calibration.projectionLeft;
    return calibration;
}

double phaseDistance(float first, float second)
{
    constexpr double twoPi = 6.28318530717958647692;
    return std::fabs(std::remainder(static_cast<double>(first) - static_cast<double>(second), twoPi));
}

} // namespace

int main()
{
    ReconsConfig config;
    Status status = loadReconsConfig("config/reconsAlgPara.json", config);
    require(status.ok(), "expected config load");

    ManifestFrame frame = loadManifestFrame("tests/data/phase2_valid_manifest.json");
    WrappedPhaseResult result = computeWrappedPhaseCpuReference(frame.frame, config);
    require(result.status.ok(), "expected wrapped phase to compute");
    require(result.stats.stageName == "wrapped_phase_compute", "expected wrapped phase stage");
    require(result.frequencies.size() == 6, "expected left/right results for three frequencies");
    require(result.frequencies[0].phase.size() == 16, "expected per-pixel phase output");
    require(result.frequencies[0].modulation.size() == 16, "expected per-pixel modulation output");
    require(result.stats.meanPixelValue > 1.0, "expected non-trivial mean modulation");

    ManifestFrame lowModulation = loadManifestFrame("tests/data/phase3_low_modulation_manifest.json");
    result = computeWrappedPhaseCpuReference(lowModulation.frame, config);
    require(result.status.code == StatusCode::PhaseQualityInsufficient, "expected low modulation failure");

    ReconsConfig spatialConfig = config;
    spatialConfig.imageWidth = 4;
    spatialConfig.imageHeight = 4;
    OwnedStripeFrame spatialFrame = makeSpatialPhaseFrame(spatialConfig);
    WrappedPhaseResult sensor = computeWrappedPhaseCuda(spatialFrame.frame, spatialConfig);
    require(sensor.status.ok(), "expected sensor-domain CUDA phase");
    CalibrationModel shiftCalibration = makeOnePixelShiftCalibration();
    WrappedPhaseResult rectified = computeWrappedPhaseCuda(spatialFrame.frame, spatialConfig, shiftCalibration);
    require(rectified.status.ok(), "expected rectified CUDA phase");
    require(rectified.coordinateDomain == PhaseCoordinateDomain::Rectified,
            "expected calibration-aware wrapped phase to report rectified domain");
    require(rectified.frequencies.size() == sensor.frequencies.size(), "expected matching frequency outputs");
    constexpr int width = 4;
    constexpr int probeY = 1;
    constexpr int probeX = 1;
    const std::size_t rectifiedProbe = static_cast<std::size_t>(probeY * width + probeX);
    const std::size_t shiftedSensorProbe = rectifiedProbe + 1U;
    for (std::size_t frequency = 0; frequency < rectified.frequencies.size(); ++frequency) {
        require(phaseDistance(rectified.frequencies[frequency].phase[rectifiedProbe],
                              sensor.frequencies[frequency].phase[shiftedSensorProbe]) < 1.0e-5,
                "expected rectified phase to sample the shifted sensor pixel");
        require(std::fabs(rectified.frequencies[frequency].modulation[rectifiedProbe] -
                          sensor.frequencies[frequency].modulation[shiftedSensorProbe]) < 1.0e-4F,
                "expected rectified modulation to sample the shifted sensor pixel");
        require(std::isnan(rectified.frequencies[frequency].phase[probeY * width + (width - 1)]),
                "expected an out-of-bounds rectification sample to be invalid");
    }

    return EXIT_SUCCESS;
}
