#include "reconstruct_one_frame/reconstructInterface.h"

#include <algorithm>
#include <cmath>
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

StripeFrameGroup makeFrame()
{
    static std::vector<std::vector<unsigned char>> leftBuffers;
    static std::vector<std::vector<unsigned char>> rightBuffers;
    leftBuffers.clear();
    rightBuffers.clear();
    leftBuffers.reserve(15);
    rightBuffers.reserve(15);

    StripeFrameGroup frame;
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr double pi = 3.14159265358979323846;
    auto fillStripe = [](std::vector<unsigned char>& pixels,
                         int frequency,
                         int step,
                         double cameraOffset) {
        constexpr int width = 4;
        constexpr int height = 4;
        constexpr double pi = 3.14159265358979323846;
        pixels.resize(width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double spatialPhase =
                    0.31 * static_cast<double>(x) +
                    0.07 * static_cast<double>(frequency) +
                    cameraOffset;
                const double stepAngle = 2.0 * pi * static_cast<double>(step) / 5.0;
                const int value = static_cast<int>(
                    std::lround(128.0 + 70.0 * std::cos(spatialPhase + stepAngle)));
                pixels[static_cast<std::size_t>(y * width + x)] =
                    static_cast<unsigned char>(std::clamp(value, 0, 255));
            }
        }
    };
    for (int frequency = 0; frequency < 3; ++frequency) {
        for (int step = 0; step < 5; ++step) {
            leftBuffers.emplace_back();
            rightBuffers.emplace_back();
            fillStripe(leftBuffers.back(), frequency, step, 0.0);
            fillStripe(rightBuffers.back(), frequency, step, 0.11);

            ImageView leftView;
            leftView.data = leftBuffers.back().data();
            leftView.width = width;
            leftView.height = height;
            leftView.channels = 1;
            leftView.strideBytes = width;
            leftView.elementType = ImageElementType::UInt8;

            ImageView rightView = leftView;
            rightView.data = rightBuffers.back().data();

            frame.leftStripes.push_back({CameraSide::Left, frequency, step, 0, leftView});
            frame.rightStripes.push_back({CameraSide::Right, frequency, step, 0, rightView});
        }
    }
    return frame;
}

} // namespace

int main()
{
    ReconstructEngine engine;
    InitOptions options;
    options.configPath = "config/reconsAlgPara.json";
    options.dryRun = true;
    options.dryRunNoCalib = true;

    Status status = engine.init(options);
    require(status.ok(), "expected engine init to pass with dry-run-no-calib");

    FrameResult result = engine.run(makeFrame());
    require(result.status.ok(), "expected pipeline dry-run to pass");
    require(!result.depthComputed, "depth must remain notComputed");
    require(!result.normalComputed, "normal must remain notComputed");
    require(!result.qualityComputed, "quality must remain notComputed");
    require(result.stats.size() == 6, "expected phase4 dry-run stages");
    require(result.stats[0].stageName == "input_contract_validation", "expected phase2 input contract stage");
    require(result.stats[3].stageName == "wrapped_phase_compute_cuda", "expected CUDA wrapped phase stage");
    require(result.stats[3].cudaComputedPixels > 0, "expected CUDA wrapped phase pixels");
    require(result.wrappedPhaseComputed, "expected wrapped phase computed marker");
    require(result.stats[4].stageName == "phase_unwrap_cuda", "expected CUDA unwrap stage");
    require(result.stats[4].cudaComputedPixels > 0, "expected CUDA unwrap pixels");
    require(result.unwrappedPhaseComputed, "expected unwrapped phase computed marker");
    require(result.stats[5].status.code == StatusCode::NotComputed, "expected downstream stage NotComputed marker");

    return EXIT_SUCCESS;
}
