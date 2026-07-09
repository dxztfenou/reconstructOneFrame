#include "image/ImageValidator.h"

#include <algorithm>
#include <cstdint>

namespace reconstruct_one_frame {

namespace {

int elementSizeBytes(ImageElementType type)
{
    switch (type) {
    case ImageElementType::UInt8:
        return 1;
    case ImageElementType::UInt16:
        return 2;
    case ImageElementType::Float32:
        return 4;
    case ImageElementType::Unknown:
        return 0;
    }
    return 0;
}

Status validateImageView(const ImageView& image, const char* module)
{
    if (image.data == nullptr) {
        return {StatusCode::InputEmptyImage, module, "image data pointer is null"};
    }
    if (image.width <= 0 || image.height <= 0 || image.channels <= 0) {
        return {StatusCode::InputEmptyImage, module, "image dimensions or channels are invalid"};
    }
    const int elementSize = elementSizeBytes(image.elementType);
    if (elementSize == 0) {
        return {StatusCode::InputTypeUnsupported, module, "unsupported image element type"};
    }
    const int minStride = image.width * image.channels * elementSize;
    if (image.strideBytes < minStride) {
        return {StatusCode::InputStrideInvalid, module, "stride is smaller than packed row width"};
    }
    return {};
}

bool isPixelBlack(const unsigned char* ptr, ImageElementType type)
{
    switch (type) {
    case ImageElementType::UInt8:
        return *ptr == 0;
    case ImageElementType::UInt16:
        return *reinterpret_cast<const std::uint16_t*>(ptr) == 0;
    case ImageElementType::Float32:
        return *reinterpret_cast<const float*>(ptr) == 0.0F;
    case ImageElementType::Unknown:
        return false;
    }
    return false;
}

Status checkBlackImage(const ImageView& image, StageStats& stats, const ImageValidationOptions& options)
{
    const int elementSize = elementSizeBytes(image.elementType);
    const auto* base = static_cast<const unsigned char*>(image.data);
    std::size_t black = 0;
    std::size_t checked = 0;

    for (int y = 0; y < image.height; ++y) {
        const unsigned char* row = base + static_cast<std::size_t>(y) * image.strideBytes;
        for (int x = 0; x < image.width * image.channels; ++x) {
            const unsigned char* pixel = row + static_cast<std::size_t>(x) * elementSize;
            if (isPixelBlack(pixel, image.elementType)) {
                ++black;
            }
            ++checked;
        }
    }

    stats.checkedPixels += checked;
    stats.blackPixels += black;
    if (checked > 0) {
        const double ratio = static_cast<double>(black) / static_cast<double>(checked);
        if (ratio >= options.blackPixelThresholdRatio) {
            return {StatusCode::InputBlackImage, "ImageValidator", "image is black above threshold"};
        }
    }

    return {};
}

Status validateStripeVector(const std::vector<StripeImage>& images,
                            CameraSide expectedSide,
                            int& width,
                            int& height,
                            StageStats& stats,
                            const ImageValidationOptions& options)
{
    for (const StripeImage& stripe : images) {
        if (stripe.camera != expectedSide) {
            return {StatusCode::InputInvalidValue, "ImageValidator", "stripe camera side does not match its array"};
        }

        Status status = validateImageView(stripe.image, "ImageValidator");
        if (!status.ok()) {
            ++stats.rejectedImageCount;
            return status;
        }

        if (width == 0 && height == 0) {
            width = stripe.image.width;
            height = stripe.image.height;
        } else if (stripe.image.width != width || stripe.image.height != height) {
            ++stats.rejectedImageCount;
            return {StatusCode::InputSizeMismatch, "ImageValidator", "stripe image sizes are inconsistent"};
        }

        status = checkBlackImage(stripe.image, stats, options);
        if (!status.ok()) {
            ++stats.rejectedImageCount;
            return status;
        }

        ++stats.validImageCount;
    }

    return {};
}

} // namespace

Status validateStripeFrameGroup(const StripeFrameGroup& frame,
                                StageStats& stats,
                                const ImageValidationOptions& options)
{
    stats.stageName = "image_validation";
    stats.inputImageCount = frame.leftStripes.size() + frame.rightStripes.size();

    if (frame.leftStripes.empty() || frame.rightStripes.empty()) {
        stats.status = {StatusCode::InputMissing, "ImageValidator", "left and right stripe arrays must not be empty"};
        return stats.status;
    }

    int width = 0;
    int height = 0;
    Status status = validateStripeVector(frame.leftStripes, CameraSide::Left, width, height, stats, options);
    if (!status.ok()) {
        stats.status = status;
        return status;
    }

    status = validateStripeVector(frame.rightStripes, CameraSide::Right, width, height, stats, options);
    if (!status.ok()) {
        stats.status = status;
        return status;
    }

    stats.status = {};
    return {};
}

} // namespace reconstruct_one_frame
