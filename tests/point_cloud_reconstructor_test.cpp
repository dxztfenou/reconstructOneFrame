#include "reconstruction/PointCloudReconstructor.h"

#include <cstdlib>
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

UnwrappedPhaseResult makePhase()
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
            phase.left.absolutePhase[y * width + x] = static_cast<float>(x) * 0.1F;
            phase.right.absolutePhase[y * width + x] = static_cast<float>(x + 2) * 0.1F;
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

} // namespace

int main()
{
    ReconsConfig config;
    config.imageWidth = 16;
    config.imageHeight = 8;
    config.phaseDiffThreshold = 0.01;
    config.minZ = 50.0;
    config.maxZ = 150.0;
    config.disparityWindowEnabled = false;
    config.pointCloudSmoothingEnabled = false;
    config.pointCloudFilterEnabled = false;

    PointCloudReconstructionResult result = reconstructPointCloudCuda(makePhase(), makeCalibration(), config, makeFrame());
    require(result.status.ok(), "expected CUDA point cloud reconstruction to pass");
    require(!result.vertices.empty(), "expected reconstructed vertices");
    require(result.rawValidPointCount > 0, "expected raw valid points");
    require(result.filteredGridValidPointCount == result.vertices.size(), "expected filtered grid count to match vertices");
    return EXIT_SUCCESS;
}
