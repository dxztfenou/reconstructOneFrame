#include "config/ReconsConfig.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace reconstruct_one_frame;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    ReconsConfig config;
    Status status = loadReconsConfig("config/reconsAlgPara.json", config);
    require(status.ok(), "expected valid config to load");
    require(config.imageWidth == 424, "expected image width from config");
    require(config.imageHeight == 400, "expected image height from config");
    require(config.frequencyCount == 3, "expected FREQ from config");
    require(config.stepCount == 5, "expected STEP from config");
    require(config.freqSeries.size() == 3, "expected freqSeries size");
    require(config.freq23 == 7, "expected freq23 from config");
    require(config.bMin == 1, "expected Bmin from config");
    require(config.medianKernelSize == 5, "expected winSizeMedian from config");
    require(config.phaseStepCounts.size() == 3, "expected phaseStepCounts size");
    require(config.phaseStepCounts[0] == 5, "expected low frequency 5-step production contract");
    require(config.phaseStepCounts[1] == 5, "expected middle frequency 5-step contract");
    require(config.phaseStepCounts[2] == 5, "expected high frequency 5-step contract");
    require(config.stripeRequirements.size() == 3, "expected stripe requirement count");
    require(config.stripeRequirements[0].requiredPhaseSteps == 5, "expected per-frequency phase steps");
    require(config.stripeRequirements[1].firstProjectorIndex == 6, "expected projector offset after first frequency");
    require(config.stripeRequirements[2].firstProjectorIndex == 11, "expected projector offset after second frequency");
    require(!config.phaseUnwrapResidualGateEnabled, "expected residual gate flag from config");
    require(config.phaseUnwrapAbs23ResidualThreshold == 0.2, "expected abs23 residual threshold");
    require(config.phaseUnwrapFinalResidualThreshold == 0.2, "expected final residual threshold");
    require(config.disparityWindowEnabled, "expected physical disparity window enabled");
    require(config.matchingLeftRightConsistencyEnabled, "expected left-right consistency enabled");
    require(config.matchingRightPhaseMonotonicEnabled, "expected right-phase monotonicity enabled");
    require(config.matchingRightPhaseMonotonicRadius == 1, "expected right-phase monotonic radius");
    require(config.matchingRightPhaseMinSlope == 0.0001, "expected right-phase minimum slope");
    require(config.disparitySubpixelEnabled, "expected disparity subpixel interpolation enabled");
    require(!config.pointCloudSmoothingEnabled, "expected production smoothing to remain disabled until real-data validation");
    require(config.pointCloudFilterEnabled, "expected production point-cloud filter enabled");
    require(config.colorTextureEnabled, "expected production color texture enabled");
    require(config.colorTextureProjectorIndices == std::vector<int>({16, 17, 18}), "expected BGR auxiliary projector indices");
    require(config.colorCorrectionMatrix.size() == 12, "expected 3x4 color correction matrix");
    require(config.colorGamma == 0.5, "expected legacy-compatible color gamma");

    const auto legacyCompatiblePath = std::filesystem::temp_directory_path() / "rof_legacy_compatible_config.json";
    {
        std::ofstream out(legacyCompatiblePath);
        out << "{}";
    }
    ReconsConfig legacyCompatible;
    status = loadReconsConfig(legacyCompatiblePath.string(), legacyCompatible);
    std::filesystem::remove(legacyCompatiblePath);
    require(status.ok(), "expected missing matching keys to use backward-compatible defaults");
    require(!legacyCompatible.disparityWindowEnabled, "expected missing disparity window key to remain disabled");
    require(!legacyCompatible.matchingLeftRightConsistencyEnabled, "expected missing left-right key to remain disabled");
    require(!legacyCompatible.matchingRightPhaseMonotonicEnabled, "expected missing monotonic key to remain disabled");
    require(!legacyCompatible.disparitySubpixelEnabled, "expected missing subpixel key to remain disabled");
    require(!legacyCompatible.pointCloudSmoothingEnabled, "expected missing smoothing key to remain disabled");
    require(!legacyCompatible.pointCloudFilterEnabled, "expected missing point-cloud filter key to remain disabled");
    require(!legacyCompatible.colorTextureEnabled, "expected missing color texture key to remain disabled");

    const auto invalidColorPath = std::filesystem::temp_directory_path() / "rof_invalid_color_config.json";
    {
        std::ofstream out(invalidColorPath);
        out << R"({"colorTextureEnabled":true,"colorTextureProjectorIndices":[16,17],"colorCorrectionMatrix":[1,0,0,0,0,1,0,0,0,0,1,0],"colorGamma":0.5})";
    }
    ReconsConfig invalidColor;
    status = loadReconsConfig(invalidColorPath.string(), invalidColor);
    std::filesystem::remove(invalidColorPath);
    require(status.code == StatusCode::ConfigInvalidValue, "expected invalid color projector indices to fail");

    ReconsConfig missing;
    status = loadReconsConfig("config/does_not_exist.json", missing);
    require(status.code == StatusCode::ConfigMissing, "expected ConfigMissing");

    const auto invalidPath = std::filesystem::temp_directory_path() / "rof_invalid_config.json";
    {
        std::ofstream out(invalidPath);
        out << "not json";
    }
    ReconsConfig invalid;
    status = loadReconsConfig(invalidPath.string(), invalid);
    std::filesystem::remove(invalidPath);
    require(status.code == StatusCode::ConfigParseFailed, "expected ConfigParseFailed");

    return EXIT_SUCCESS;
}
