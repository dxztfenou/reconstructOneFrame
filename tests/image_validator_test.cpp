#include "image/ImageValidator.h"

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

StripeFrameGroup makeFrame(std::vector<unsigned char>& left,
                           std::vector<unsigned char>& right,
                           int rightWidth = 4,
                           int rightHeight = 4)
{
    StripeFrameGroup frame;
    frame.leftStripes.push_back({CameraSide::Left, 0, 0, 0, makeView(left)});
    frame.rightStripes.push_back({CameraSide::Right, 0, 0, 0, makeView(right, rightWidth, rightHeight)});
    return frame;
}

} // namespace

int main()
{
    StageStats stats;
    StripeFrameGroup empty;
    Status status = validateStripeFrameGroup(empty, stats);
    require(status.code == StatusCode::InputMissing, "expected InputMissing for empty frame");

    std::vector<unsigned char> left(16, 1);
    std::vector<unsigned char> right(16, 2);
    StripeFrameGroup nullImage = makeFrame(left, right);
    nullImage.leftStripes[0].image.data = nullptr;
    stats = StageStats {};
    status = validateStripeFrameGroup(nullImage, stats);
    require(status.code == StatusCode::InputEmptyImage, "expected InputEmptyImage for null data");

    std::vector<unsigned char> blackLeft(16, 0);
    std::vector<unsigned char> blackRight(16, 2);
    stats = StageStats {};
    status = validateStripeFrameGroup(makeFrame(blackLeft, blackRight), stats);
    require(status.code == StatusCode::InputBlackImage, "expected InputBlackImage");

    stats = StageStats {};
    status = validateStripeFrameGroup(makeFrame(left, right, 2, 8), stats);
    require(status.code == StatusCode::InputSizeMismatch, "expected InputSizeMismatch");

    stats = StageStats {};
    status = validateStripeFrameGroup(makeFrame(left, right), stats);
    require(status.ok(), "expected normal frame to pass");

    return EXIT_SUCCESS;
}
