#include "reconstruct_one_frame/reconstructInterface.h"

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
    for (int frequency = 0; frequency < 3; ++frequency) {
        for (int step = 0; step < 5; ++step) {
            leftBuffers.emplace_back(16, static_cast<unsigned char>(17 + frequency * 10 + step));
            rightBuffers.emplace_back(16, static_cast<unsigned char>(47 + frequency * 10 + step));

            ImageView leftView;
            leftView.data = leftBuffers.back().data();
            leftView.width = 4;
            leftView.height = 4;
            leftView.channels = 1;
            leftView.strideBytes = 4;
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
