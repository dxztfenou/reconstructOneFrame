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
    require(config.phaseStepCounts[0] == 3, "expected low frequency 3-step contract");
    require(config.phaseStepCounts[1] == 5, "expected middle frequency 5-step contract");
    require(config.phaseStepCounts[2] == 5, "expected high frequency 5-step contract");
    require(config.stripeRequirements.size() == 3, "expected stripe requirement count");
    require(config.stripeRequirements[0].requiredPhaseSteps == 3, "expected per-frequency phase steps");
    require(config.stripeRequirements[1].firstProjectorIndex == 4, "expected projector offset after first frequency");
    require(config.stripeRequirements[2].firstProjectorIndex == 9, "expected projector offset after second frequency");
    require(!config.phaseUnwrapResidualGateEnabled, "expected residual gate flag from config");
    require(config.phaseUnwrapAbs23ResidualThreshold == 0.2, "expected abs23 residual threshold");
    require(config.phaseUnwrapFinalResidualThreshold == 0.2, "expected final residual threshold");

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
