#include "image/InputManifest.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>

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

bool parseElementType(const std::string& text, ImageElementType& type)
{
    if (text == "uint8") {
        type = ImageElementType::UInt8;
        return true;
    }
    if (text == "uint16") {
        type = ImageElementType::UInt16;
        return true;
    }
    if (text == "float32") {
        type = ImageElementType::Float32;
        return true;
    }
    return false;
}

Status parseStripes(const JsonObject& json, const std::string& key, std::vector<ManifestStripe>& stripes)
{
    std::vector<JsonObject> objects;
    if (!json.getObjectArray(key, objects)) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", key + " array is missing"};
    }

    stripes.clear();
    for (const JsonObject& object : objects) {
        ManifestStripe stripe;
        if (!object.getInt("frequencyIndex", stripe.frequencyIndex) ||
            !object.getInt("phaseStepIndex", stripe.phaseStepIndex)) {
            return {StatusCode::InputManifestParseFailed, "InputManifest", key + " item must contain frequencyIndex and phaseStepIndex"};
        }
        (void)object.getInt("projectorIndex", stripe.projectorIndex);
        (void)object.getDouble("fill", stripe.fill);
        (void)object.getString("path", stripe.path);
        stripes.push_back(stripe);
    }
    return {};
}

void fillBuffer(std::vector<unsigned char>& buffer, ImageElementType type, double fill)
{
    if (type == ImageElementType::UInt8) {
        std::fill(buffer.begin(), buffer.end(), static_cast<unsigned char>(std::clamp(fill, 0.0, 255.0)));
        return;
    }
    if (type == ImageElementType::UInt16) {
        const auto value = static_cast<std::uint16_t>(std::clamp(fill, 0.0, 65535.0));
        for (std::size_t i = 0; i + sizeof(value) <= buffer.size(); i += sizeof(value)) {
            std::memcpy(buffer.data() + i, &value, sizeof(value));
        }
        return;
    }
    if (type == ImageElementType::Float32) {
        const auto value = static_cast<float>(fill);
        for (std::size_t i = 0; i + sizeof(value) <= buffer.size(); i += sizeof(value)) {
            std::memcpy(buffer.data() + i, &value, sizeof(value));
        }
    }
}

std::uint16_t readLe16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
}

std::uint32_t readLe32(const unsigned char* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

Status loadBmp8(const std::filesystem::path& path,
                std::vector<unsigned char>& pixels,
                int& width,
                int& height)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {StatusCode::InputManifestMissing, "InputManifest", "BMP file not found: " + path.string()};
    }
    std::array<unsigned char, 54> header = {};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size()) ||
        header[0] != 'B' || header[1] != 'M') {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "invalid BMP header: " + path.string()};
    }
    const std::uint32_t dataOffset = readLe32(header.data() + 10);
    const std::uint32_t dibSize = readLe32(header.data() + 14);
    if (dibSize < 40) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "unsupported BMP DIB header: " + path.string()};
    }
    const auto signedWidth = static_cast<std::int32_t>(readLe32(header.data() + 18));
    const auto signedHeight = static_cast<std::int32_t>(readLe32(header.data() + 22));
    const std::uint16_t planes = readLe16(header.data() + 26);
    const std::uint16_t bitsPerPixel = readLe16(header.data() + 28);
    const std::uint32_t compression = readLe32(header.data() + 30);
    if (signedWidth <= 0 || signedHeight == 0 || planes != 1 || bitsPerPixel != 8 || compression != 0) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "only uncompressed 8-bit BMP is supported: " + path.string()};
    }

    width = signedWidth;
    height = signedHeight < 0 ? -signedHeight : signedHeight;
    const bool topDown = signedHeight < 0;
    const int stride = ((width * bitsPerPixel + 31) / 32) * 4;
    const std::size_t needed = static_cast<std::size_t>(dataOffset) + static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) < needed) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "BMP pixel data is truncated: " + path.string()};
    }

    const std::size_t packedBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t pixelBlockBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    pixels.resize(packedBytes);
    input.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
    if (topDown && stride == width) {
        input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(packedBytes));
        if (input.gcount() != static_cast<std::streamsize>(packedBytes)) {
            return {StatusCode::InputManifestParseFailed, "InputManifest", "BMP pixel data read failed: " + path.string()};
        }
        return {};
    }

    thread_local std::vector<unsigned char> pixelBlock;
    pixelBlock.resize(pixelBlockBytes);
    input.read(reinterpret_cast<char*>(pixelBlock.data()), static_cast<std::streamsize>(pixelBlockBytes));
    if (input.gcount() != static_cast<std::streamsize>(pixelBlockBytes)) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "BMP pixel data read failed: " + path.string()};
    }
    for (int y = 0; y < height; ++y) {
        const int sourceY = topDown ? y : (height - 1 - y);
        const unsigned char* source = pixelBlock.data() + static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(stride);
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width),
                    source,
                    static_cast<std::size_t>(width));
    }
    return {};
}

Status appendStripe(const ManifestStripe& manifestStripe,
                    CameraSide side,
                    const InputManifest& manifest,
                    ManifestFrame& output,
                    std::vector<StripeImage>& target)
{
    const int elementSize = elementSizeBytes(manifest.elementType);
    if (elementSize <= 0 || manifest.width <= 0 || manifest.height <= 0 || manifest.channels <= 0) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "manifest image shape or elementType is invalid"};
    }

    const int stride = manifest.width * manifest.channels * elementSize;
    const std::size_t bytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(manifest.height);
    output.buffers.emplace_back(bytes);
    if (manifestStripe.path.empty()) {
        fillBuffer(output.buffers.back(), manifest.elementType, manifestStripe.fill);
    }

    ImageView view;
    view.data = output.buffers.back().data();
    view.width = manifest.width;
    view.height = manifest.height;
    view.channels = manifest.channels;
    view.strideBytes = stride;
    view.elementType = manifest.elementType;

    target.push_back({side, manifestStripe.frequencyIndex, manifestStripe.phaseStepIndex, manifestStripe.projectorIndex, view});
    return {};
}

Status appendBmpStripe(const std::filesystem::path& path,
                       CameraSide side,
                       int frequencyIndex,
                       int phaseStepIndex,
                       int projectorIndex,
                       ManifestFrame& output,
                       std::vector<StripeImage>& target,
                       int& expectedWidth,
                       int& expectedHeight)
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    Status status = loadBmp8(path, pixels, width, height);
    if (!status.ok()) {
        return status;
    }
    if (expectedWidth == 0 && expectedHeight == 0) {
        expectedWidth = width;
        expectedHeight = height;
    } else if (width != expectedWidth || height != expectedHeight) {
        return {StatusCode::InputSizeMismatch, "InputManifest", "BMP size mismatch: " + path.string()};
    }

    output.buffers.push_back(std::move(pixels));
    ImageView view;
    view.data = output.buffers.back().data();
    view.width = width;
    view.height = height;
    view.channels = 1;
    view.strideBytes = width;
    view.elementType = ImageElementType::UInt8;
    target.push_back({side, frequencyIndex, phaseStepIndex, projectorIndex, view});
    return {};
}

Status appendBmpColor(const std::array<std::filesystem::path, 3>& paths,
                      ManifestFrame& output,
                      int& expectedWidth,
                      int& expectedHeight)
{
    std::array<std::vector<unsigned char>, 3> channels;
    int width = 0;
    int height = 0;
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        int channelWidth = 0;
        int channelHeight = 0;
        Status status = loadBmp8(paths[channel], channels[channel], channelWidth, channelHeight);
        if (!status.ok()) {
            return status;
        }
        if (channel == 0) {
            width = channelWidth;
            height = channelHeight;
        } else if (channelWidth != width || channelHeight != height) {
            return {StatusCode::InputSizeMismatch, "InputManifest", "color BMP size mismatch: " + paths[channel].string()};
        }
    }
    if (expectedWidth == 0 && expectedHeight == 0) {
        expectedWidth = width;
        expectedHeight = height;
    } else if (width != expectedWidth || height != expectedHeight) {
        return {StatusCode::InputSizeMismatch, "InputManifest", "color BMP size does not match phase input"};
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    output.buffers.emplace_back(pixelCount * 3);
    std::vector<unsigned char>& packedBgr = output.buffers.back();
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        packedBgr[pixel * 3 + 0] = channels[0][pixel];
        packedBgr[pixel * 3 + 1] = channels[1][pixel];
        packedBgr[pixel * 3 + 2] = channels[2][pixel];
    }

    ImageView view;
    view.data = packedBgr.data();
    view.width = width;
    view.height = height;
    view.channels = 3;
    view.strideBytes = width * 3;
    view.elementType = ImageElementType::UInt8;
    output.frame.leftColor = view;
    output.frame.color = view;
    return {};
}

Status validateColorProjectorIndices(const ReconsConfig& config)
{
    if (config.colorTextureProjectorIndices.size() != 3) {
        return {StatusCode::ConfigInvalidValue,
                "InputManifest",
                "colorTextureProjectorIndices must contain B, G, and R projector indices"};
    }
    for (int projectorIndex : config.colorTextureProjectorIndices) {
        if (projectorIndex <= 0) {
            return {StatusCode::ConfigInvalidValue,
                    "InputManifest",
                    "colorTextureProjectorIndices must be positive"};
        }
    }
    return {};
}

} // namespace

Status loadInputManifest(const std::string& path, InputManifest& manifest)
{
    JsonObject json("{}");
    Status status = loadJsonObjectFromFile(path, json);
    if (status.code == StatusCode::ConfigMissing) {
        return {StatusCode::InputManifestMissing, "InputManifest", "input manifest not found: " + path};
    }
    if (status.code == StatusCode::ConfigParseFailed) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "invalid input manifest: " + path};
    }
    if (!status.ok()) {
        status.module = "InputManifest";
        return status;
    }

    InputManifest parsed;
    int frameId = 0;
    (void)json.getInt("frameId", frameId);
    parsed.frameId = static_cast<std::uint64_t>(std::max(frameId, 0));
    if (!json.getInt("width", parsed.width) ||
        !json.getInt("height", parsed.height) ||
        !json.getInt("channels", parsed.channels)) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "manifest must contain width, height, and channels"};
    }
    std::string elementType;
    if (!json.getString("elementType", elementType) || !parseElementType(elementType, parsed.elementType)) {
        return {StatusCode::InputManifestParseFailed, "InputManifest", "manifest elementType must be uint8, uint16, or float32"};
    }

    status = parseStripes(json, "leftStripes", parsed.leftStripes);
    if (!status.ok()) {
        return status;
    }
    status = parseStripes(json, "rightStripes", parsed.rightStripes);
    if (!status.ok()) {
        return status;
    }

    manifest = std::move(parsed);
    return {};
}

Status buildFrameFromManifest(const InputManifest& manifest, ManifestFrame& output)
{
    output = ManifestFrame {};
    output.frame.frameId = manifest.frameId;
    output.buffers.reserve(manifest.leftStripes.size() + manifest.rightStripes.size());

    for (const ManifestStripe& stripe : manifest.leftStripes) {
        Status status = appendStripe(stripe, CameraSide::Left, manifest, output, output.frame.leftStripes);
        if (!status.ok()) {
            return status;
        }
    }
    for (const ManifestStripe& stripe : manifest.rightStripes) {
        Status status = appendStripe(stripe, CameraSide::Right, manifest, output, output.frame.rightStripes);
        if (!status.ok()) {
            return status;
        }
    }
    return {};
}

Status loadSingleStripeBmpDirectory(const std::string& root,
                                    const ReconsConfig& config,
                                    int groupIndex,
                                    ManifestFrame& output,
                                    bool includeColor)
{
    output = ManifestFrame {};
    output.frame.frameId = static_cast<std::uint64_t>(std::max(groupIndex, 0));
    if (config.colorTextureEnabled && includeColor) {
        Status status = validateColorProjectorIndices(config);
        if (!status.ok()) {
            return status;
        }
    }
    std::size_t expectedImageCount = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        expectedImageCount += static_cast<std::size_t>(requirement.requiredPhaseSteps) * 2;
    }
    if (config.colorTextureEnabled && includeColor) {
        ++expectedImageCount;
    }
    output.buffers.reserve(expectedImageCount);

    const std::filesystem::path groupRoot = std::filesystem::path(root) / std::to_string(groupIndex);
    int expectedWidth = 0;
    int expectedHeight = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
            const int projectorIndex = requirement.firstProjectorIndex + step;
            const std::filesystem::path left = groupRoot / "L" / (std::to_string(projectorIndex) + ".bmp");
            const std::filesystem::path right = groupRoot / "R" / (std::to_string(projectorIndex) + ".bmp");
            Status status = appendBmpStripe(left,
                                            CameraSide::Left,
                                            requirement.frequencyIndex,
                                            step,
                                            projectorIndex,
                                            output,
                                            output.frame.leftStripes,
                                            expectedWidth,
                                            expectedHeight);
            if (!status.ok()) {
                return status;
            }
            status = appendBmpStripe(right,
                                     CameraSide::Right,
                                     requirement.frequencyIndex,
                                     step,
                                     projectorIndex,
                                     output,
                                     output.frame.rightStripes,
                                     expectedWidth,
                                     expectedHeight);
            if (!status.ok()) {
                return status;
            }
        }
    }
    if (config.colorTextureEnabled && includeColor) {
        std::array<std::filesystem::path, 3> colorPaths;
        for (std::size_t channel = 0; channel < colorPaths.size(); ++channel) {
            colorPaths[channel] =
                groupRoot / "L" / (std::to_string(config.colorTextureProjectorIndices[channel]) + ".bmp");
        }
        return appendBmpColor(colorPaths, output, expectedWidth, expectedHeight);
    }
    return {};
}

Status loadSourceImgFrameDirectory(const std::string& root,
                                   const ReconsConfig& config,
                                   int frameIndex,
                                   ManifestFrame& output,
                                   bool includeColor)
{
    output = ManifestFrame {};
    output.frame.frameId = static_cast<std::uint64_t>(std::max(frameIndex, 0));
    if (config.colorTextureEnabled && includeColor) {
        Status status = validateColorProjectorIndices(config);
        if (!status.ok()) {
            return status;
        }
    }
    std::size_t expectedImageCount = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        expectedImageCount += static_cast<std::size_t>(requirement.requiredPhaseSteps) * 2;
    }
    if (config.colorTextureEnabled && includeColor) {
        ++expectedImageCount;
    }
    output.buffers.reserve(expectedImageCount);

    const std::filesystem::path rootPath(root);
    std::filesystem::path sourceImgRoot = rootPath / std::to_string(frameIndex) / "SourceImg";
    if (!std::filesystem::exists(sourceImgRoot)) {
        const std::filesystem::path directSourceImgRoot = rootPath / "SourceImg";
        if (std::filesystem::exists(directSourceImgRoot)) {
            sourceImgRoot = directSourceImgRoot;
        }
    }
    if (!std::filesystem::exists(sourceImgRoot)) {
        return {StatusCode::InputManifestMissing,
                "InputManifest",
                "SourceImg directory not found: " + sourceImgRoot.string()};
    }

    int expectedWidth = 0;
    int expectedHeight = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
            const int projectorIndex = requirement.firstProjectorIndex + step;
            const int sourceImgIndex = projectorIndex - 1;
            if (sourceImgIndex < 0) {
                return {StatusCode::InputManifestParseFailed,
                        "InputManifest",
                        "SourceImg zero-based index is negative for projectorIndex=" + std::to_string(projectorIndex)};
            }

            const std::filesystem::path left = sourceImgRoot / ("L" + std::to_string(sourceImgIndex) + ".bmp");
            const std::filesystem::path right = sourceImgRoot / ("R" + std::to_string(sourceImgIndex) + ".bmp");
            Status status = appendBmpStripe(left,
                                            CameraSide::Left,
                                            requirement.frequencyIndex,
                                            step,
                                            projectorIndex,
                                            output,
                                            output.frame.leftStripes,
                                            expectedWidth,
                                            expectedHeight);
            if (!status.ok()) {
                return status;
            }
            status = appendBmpStripe(right,
                                     CameraSide::Right,
                                     requirement.frequencyIndex,
                                     step,
                                     projectorIndex,
                                     output,
                                     output.frame.rightStripes,
                                     expectedWidth,
                                     expectedHeight);
            if (!status.ok()) {
                return status;
            }
        }
    }
    if (config.colorTextureEnabled && includeColor) {
        std::array<std::filesystem::path, 3> colorPaths;
        for (std::size_t channel = 0; channel < colorPaths.size(); ++channel) {
            const int sourceImgIndex = config.colorTextureProjectorIndices[channel] - 1;
            colorPaths[channel] =
                sourceImgRoot / ("L" + std::to_string(sourceImgIndex) + ".bmp");
        }
        return appendBmpColor(colorPaths, output, expectedWidth, expectedHeight);
    }
    return {};
}

std::string summarizeManifest(const InputManifest& manifest)
{
    std::ostringstream out;
    out << "frameId=" << manifest.frameId
        << ", image_size=" << manifest.width << "x" << manifest.height
        << ", channels=" << manifest.channels
        << ", leftStripes=" << manifest.leftStripes.size()
        << ", rightStripes=" << manifest.rightStripes.size();
    return out.str();
}

} // namespace reconstruct_one_frame
