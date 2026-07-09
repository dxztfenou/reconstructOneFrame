#include "config/ReconsConfig.h"
#include "image\InputManifest.h"
#include "image\ImageValidator.h"

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

ReconsConfig makeConfig()
{
    ReconsConfig config;
    config.imageWidth = 4;
    config.imageHeight = 4;
    config.frequencyCount = 1;
    config.stepCount = 1;
    config.freqSeries = {15};
    config.stripeRequirements = {{0, 15, 1}};
    return config;
}

ImageView makeView(std::vector<unsigned char>& data, int width = 4, int height = 4)
{
    ImageView view;
    view.data = data.data();
    view.width = width;
    view.height = height;
    view.channels = 1;
    view.strideBytes = width;
    view.elementType = ImageElementType::UInt8;
    return view;
}

StripeFrameGroup makeSimpleFrame(std::vector<unsigned char>& left, std::vector<unsigned char>& right)
{
    StripeFrameGroup frame;
    frame.leftStripes.push_back({CameraSide::Left, 0, 0, 0, makeView(left)});
    frame.rightStripes.push_back({CameraSide::Right, 0, 0, 0, makeView(right)});
    return frame;
}

} // namespace

int main()
{
    const ReconsConfig config = makeConfig();
    std::vector<unsigned char> left(16, 32);
    std::vector<unsigned char> right(16, 48);
    StageStats stats;
    Status status = validateStripeFrameGroup(makeSimpleFrame(left, right), config, stats);
    require(status.ok(), "expected valid frame to pass coverage validation");
    require(stats.meanPixelValue > 0.0, "expected mean pixel value");

    StripeFrameGroup missingLeft;
    missingLeft.rightStripes.push_back({CameraSide::Right, 0, 0, 0, makeView(right)});
    stats = StageStats {};
    status = validateStripeFrameGroup(missingLeft, config, stats);
    require(status.code == StatusCode::InputMissingLeftStripes, "expected missing left status");

    StripeFrameGroup cameraSideMismatch = makeSimpleFrame(left, right);
    cameraSideMismatch.leftStripes[0].camera = CameraSide::Right;
    stats = StageStats {};
    status = validateStripeFrameGroup(cameraSideMismatch, config, stats);
    require(status.code == StatusCode::InputCameraSideMismatch, "expected camera mismatch status");

    StripeFrameGroup missingStep = makeSimpleFrame(left, right);
    missingStep.leftStripes[0].phaseStepIndex = 4;
    stats = StageStats {};
    status = validateStripeFrameGroup(missingStep, config, stats);
    require(status.code == StatusCode::InputPhaseStepMissing, "expected missing phase step status");

    std::vector<unsigned char> saturated(16, 255);
    stats = StageStats {};
    status = validateStripeFrameGroup(makeSimpleFrame(saturated, right), config, stats);
    require(status.code == StatusCode::InputSaturatedImage, "expected saturated image status");

    std::vector<float> nanLeft(16, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> nanRight(16, 1.0F);
    ImageView nanLeftView{nanLeft.data(), 4, 4, 1, 16, ImageElementType::Float32};
    ImageView nanRightView{nanRight.data(), 4, 4, 1, 16, ImageElementType::Float32};
    StripeFrameGroup nanFrame;
    nanFrame.leftStripes.push_back({CameraSide::Left, 0, 0, 0, nanLeftView});
    nanFrame.rightStripes.push_back({CameraSide::Right, 0, 0, 0, nanRightView});
    stats = StageStats {};
    status = validateStripeFrameGroup(nanFrame, config, stats);
    require(status.code == StatusCode::InputNonFinitePixel, "expected non-finite status");

    return EXIT_SUCCESS;
}
