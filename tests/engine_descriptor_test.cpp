#include "reconstruct_one_frame/reconstructInterface.h"

#include <cstdlib>
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
    ReconstructEngine engine;
    EngineDescriptor descriptor;

    Status status = engine.describe(descriptor);
    require(status.code == StatusCode::InternalError,
            "describe before init must return InternalError");

    InitOptions options;
    options.configPath = "config/reconsAlgPara.json";
    options.calibrationPath = "tests/data/phase2_valid_calibResult.json";
    status = engine.init();
    require(status.ok(), "engine runtime init must succeed");
    status = engine.setConfig(options);
    require(status.ok(), "engine setConfig must succeed");

    status = engine.describe(descriptor);
    require(status.ok(), "initialized engine must provide a descriptor");
    require(descriptor.imageWidth == 424 && descriptor.imageHeight == 400,
            "descriptor image size must match config");
    require(descriptor.liveImageCount == 18U,
            "descriptor must include phase and color projector images");
    require(descriptor.stripeRequirements.size() == 3U,
            "descriptor must expose all frequency requirements");
    require(descriptor.stripeRequirements[0].requiredPhaseSteps == 5,
            "descriptor must preserve low-frequency phase steps");
    require(descriptor.colorProjectorIndices.size() == 3U,
            "descriptor must expose BGR projector indices");
    require(descriptor.cameraModelRows == 4 && descriptor.cameraModelCols == 4,
            "Q calibration must be exposed explicitly");
    require(descriptor.cameraModelValues[0] == 1.0 && descriptor.cameraModelValues[15] == 1.0,
            "descriptor must copy Q values");

    return EXIT_SUCCESS;
}
