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
