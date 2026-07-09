#include "image/ImageValidator.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <set>

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

double pixelValue(const unsigned char* ptr, ImageElementType type, bool& finite)
{
    finite = true;
    switch (type) {
    case ImageElementType::UInt8:
        return static_cast<double>(*ptr);
    case ImageElementType::UInt16:
        return static_cast<double>(*reinterpret_cast<const std::uint16_t*>(ptr));
    case ImageElementType::Float32: {
        const float value = *reinterpret_cast<const float*>(ptr);
        finite = std::isfinite(value);
        return static_cast<double>(value);
    }
    case ImageElementType::Unknown:
        finite = false;
        return 0.0;
    }
    finite = false;
    return 0.0;
}

double saturatedValue(ImageElementType type)
{
    switch (type) {
    case ImageElementType::UInt8:
        return 255.0;
    case ImageElementType::UInt16:
        return 65535.0;
    case ImageElementType::Float32:
        return 255.0;
    case ImageElementType::Unknown:
        return std::numeric_limits<double>::infinity();
    }
    return std::numeric_limits<double>::infinity();
}

Status collectPixelStats(const ImageView& image, StageStats& stats, const ImageValidationOptions& options)
{
    const int elementSize = elementSizeBytes(image.elementType);
    const auto* base = static_cast<const unsigned char*>(image.data);
    std::size_t black = 0;
    std::size_t saturated = 0;
    std::size_t nonFinite = 0;
    std::size_t checked = 0;
    double sum = 0.0;
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    const double saturation = saturatedValue(image.elementType);

    for (int y = 0; y < image.height; ++y) {
        const unsigned char* row = base + static_cast<std::size_t>(y) * image.strideBytes;
        for (int x = 0; x < image.width * image.channels; ++x) {
            const unsigned char* pixel = row + static_cast<std::size_t>(x) * elementSize;
            bool finite = true;
            const double value = pixelValue(pixel, image.elementType, finite);
            if (!finite) {
                ++nonFinite;
                ++checked;
                continue;
            }
            if (value == 0.0) {
                ++black;
            }
            if (value >= saturation) {
                ++saturated;
            }
            sum += value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
            ++checked;
        }
    }

    stats.checkedPixels += checked;
    stats.blackPixels += black;
    stats.saturatedPixels += saturated;
    stats.nonFinitePixels += nonFinite;
    if (checked > 0) {
        stats.blackPixelRatio = static_cast<double>(stats.blackPixels) / static_cast<double>(stats.checkedPixels);
        stats.saturatedPixelRatio = static_cast<double>(stats.saturatedPixels) / static_cast<double>(stats.checkedPixels);
        const double nonFiniteRatio = static_cast<double>(stats.nonFinitePixels) / static_cast<double>(stats.checkedPixels);
        if (std::isfinite(minValue)) {
            stats.minPixelValue = stats.validImageCount == 0 ? minValue : std::min(stats.minPixelValue, minValue);
            stats.maxPixelValue = stats.validImageCount == 0 ? maxValue : std::max(stats.maxPixelValue, maxValue);
        }
        if (stats.checkedPixels > stats.nonFinitePixels) {
            const double priorCount = static_cast<double>(stats.checkedPixels - checked);
            const double currentFiniteCount = static_cast<double>(checked - nonFinite);
            const double priorSum = stats.meanPixelValue * priorCount;
            stats.meanPixelValue = (priorSum + sum) / (priorCount + currentFiniteCount);
        }
        if (nonFiniteRatio > options.nonFinitePixelThresholdRatio) {
            return {StatusCode::InputNonFinitePixel, "ImageValidator", "image contains NaN/Inf pixels above threshold"};
        }
        if (stats.blackPixelRatio >= options.blackPixelThresholdRatio) {
            return {StatusCode::InputBlackImage, "ImageValidator", "image is black above threshold"};
        }
        if (stats.saturatedPixelRatio >= options.saturatedPixelThresholdRatio) {
            return {StatusCode::InputSaturatedImage, "ImageValidator", "image is saturated above threshold"};
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
            return {StatusCode::InputCameraSideMismatch, "ImageValidator", "stripe camera side does not match its array"};
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

        status = collectPixelStats(stripe.image, stats, options);
        if (!status.ok()) {
            ++stats.rejectedImageCount;
            return status;
        }

        ++stats.validImageCount;
    }

    return {};
}

Status validateCoverage(const std::vector<StripeImage>& images,
                        const std::vector<StripeRequirement>& requirements,
                        CameraSide side)
{
    std::set<std::pair<int, int>> present;
    for (const StripeImage& stripe : images) {
        present.insert({stripe.frequencyIndex, stripe.phaseStepIndex});
    }

    for (const StripeRequirement& requirement : requirements) {
        bool anyFrequency = false;
        for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
            const auto key = std::make_pair(requirement.frequencyIndex, step);
            if (present.find(key) == present.end()) {
                if (!anyFrequency) {
                    for (const auto& existing : present) {
                        if (existing.first == requirement.frequencyIndex) {
                            anyFrequency = true;
                            break;
                        }
                    }
                }
                const std::string sideName = side == CameraSide::Left ? "left" : "right";
                return {StatusCode::InputPhaseStepMissing, "ImageValidator", sideName + " stripe phase step is missing"};
            }
            anyFrequency = true;
        }
        if (!anyFrequency) {
            const std::string sideName = side == CameraSide::Left ? "left" : "right";
            return {StatusCode::InputFrequencyPlanMismatch, "ImageValidator", sideName + " stripe frequency is missing"};
        }
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
        stats.status = frame.leftStripes.empty()
            ? Status{StatusCode::InputMissingLeftStripes, "ImageValidator", "left stripe array must not be empty"}
            : Status{StatusCode::InputMissingRightStripes, "ImageValidator", "right stripe array must not be empty"};
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

Status validateStripeFrameGroup(const StripeFrameGroup& frame,
                                const ReconsConfig& config,
                                StageStats& stats,
                                const ImageValidationOptions& options)
{
    ImageValidationOptions effectiveOptions = options;
    effectiveOptions.requireStripeCoverage = true;
    Status status = validateStripeFrameGroup(frame, stats, effectiveOptions);
    if (!status.ok()) {
        return status;
    }

    status = validateCoverage(frame.leftStripes, config.stripeRequirements, CameraSide::Left);
    if (!status.ok()) {
        stats.status = status;
        return status;
    }
    status = validateCoverage(frame.rightStripes, config.stripeRequirements, CameraSide::Right);
    if (!status.ok()) {
        stats.status = status;
        return status;
    }
    stats.status = {};
    return {};
}

} // namespace reconstruct_one_frame
