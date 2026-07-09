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
    static std::vector<unsigned char> left(16, 17);
    static std::vector<unsigned char> right(16, 19);

    ImageView leftView;
    leftView.data = left.data();
    leftView.width = 4;
    leftView.height = 4;
    leftView.channels = 1;
    leftView.strideBytes = 4;
    leftView.elementType = ImageElementType::UInt8;

    ImageView rightView = leftView;
    rightView.data = right.data();

    StripeFrameGroup frame;
    frame.leftStripes.push_back({CameraSide::Left, 0, 0, 0, leftView});
    frame.rightStripes.push_back({CameraSide::Right, 0, 0, 0, rightView});
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
    require(result.stats.size() == 2, "expected validation and dry_run_compute stats");
    require(result.stats[1].status.code == StatusCode::NotComputed, "expected compute stage NotComputed marker");

    return EXIT_SUCCESS;
}
