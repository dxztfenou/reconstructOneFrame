#include "image\InputManifest.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

void putLe16(std::vector<unsigned char>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
}

void putLe32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xff);
}

void writeTinyBmp(const std::filesystem::path& path, unsigned char fill)
{
    constexpr int width = 2;
    constexpr int height = 2;
    constexpr int stride = 4;
    constexpr std::uint32_t dataOffset = 54;
    constexpr std::uint32_t fileSize = dataOffset + stride * height;

    std::vector<unsigned char> bytes(fileSize, 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    putLe32(bytes, 2, fileSize);
    putLe32(bytes, 10, dataOffset);
    putLe32(bytes, 14, 40);
    putLe32(bytes, 18, width);
    putLe32(bytes, 22, height);
    putLe16(bytes, 26, 1);
    putLe16(bytes, 28, 8);
    putLe32(bytes, 34, stride * height);
    for (std::size_t i = dataOffset; i < bytes.size(); ++i) {
        bytes[i] = fill;
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main()
{
    InputManifest manifest;
    Status status = loadInputManifest("tests/data/phase2_valid_manifest.json", manifest);
    require(status.ok(), "expected valid manifest to load");
    require(manifest.leftStripes.size() == 15, "expected left stripe count");
    require(manifest.rightStripes.size() == 15, "expected right stripe count");

    ManifestFrame frame;
    status = buildFrameFromManifest(manifest, frame);
    require(status.ok(), "expected frame build from manifest");
    require(frame.frame.leftStripes.size() == 15, "expected left frame stripe count");
    require(frame.frame.leftStripes[0].image.data != nullptr, "expected owned image data");

    status = loadInputManifest("tests/data/does_not_exist_manifest.json", manifest);
    require(status.code == StatusCode::InputManifestMissing, "expected InputManifestMissing");

    const auto invalidPath = std::filesystem::temp_directory_path() / "rof_invalid_manifest.json";
    {
        std::ofstream out(invalidPath);
        out << "not json";
    }
    status = loadInputManifest(invalidPath.string(), manifest);
    std::filesystem::remove(invalidPath);
    require(status.code == StatusCode::InputManifestParseFailed, "expected InputManifestParseFailed");

    const auto sourceRoot = std::filesystem::temp_directory_path() / "rof_source_img_loader_test";
    const auto sourceImg = sourceRoot / "7" / "SourceImg";
    std::filesystem::remove_all(sourceRoot);
    std::filesystem::create_directories(sourceImg);
    for (int i = 0; i < 3; ++i) {
        writeTinyBmp(sourceImg / ("L" + std::to_string(i) + ".bmp"), static_cast<unsigned char>(20 + i));
        writeTinyBmp(sourceImg / ("R" + std::to_string(i) + ".bmp"), static_cast<unsigned char>(40 + i));
    }
    writeTinyBmp(sourceImg / "L15.bmp", 70);
    writeTinyBmp(sourceImg / "L16.bmp", 80);
    writeTinyBmp(sourceImg / "L17.bmp", 90);

    ReconsConfig config;
    config.stripeRequirements.clear();
    config.stripeRequirements.push_back({0, 15, 2, 1, -1});
    config.stripeRequirements.push_back({1, 21, 1, 3, -1});
    config.colorTextureEnabled = true;
    config.colorTextureProjectorIndices = {16, 17, 18};
    status = loadSourceImgFrameDirectory(sourceRoot.string(), config, 7, frame, true);
    require(status.ok(), "expected SourceImg frame to load");
    require(frame.frame.frameId == 7, "expected SourceImg frame id");
    require(frame.frame.leftStripes.size() == 3, "expected SourceImg left stripe count");
    require(frame.frame.rightStripes.size() == 3, "expected SourceImg right stripe count");
    require(frame.frame.leftStripes[0].projectorIndex == 1, "expected zero-based SourceImg to map to projectorIndex 1");
    require(frame.frame.leftStripes[2].frequencyIndex == 1, "expected second requirement frequency");
    require(frame.frame.leftColor.data != nullptr, "expected packed left color");
    require(frame.frame.leftColor.channels == 3, "expected packed BGR color");
    const auto* color = static_cast<const unsigned char*>(frame.frame.leftColor.data);
    require(color[0] == 70 && color[1] == 80 && color[2] == 90, "expected BGR auxiliary frame order");

    std::filesystem::remove(sourceImg / "L15.bmp");
    std::filesystem::remove(sourceImg / "L16.bmp");
    std::filesystem::remove(sourceImg / "L17.bmp");
    status = loadSourceImgFrameDirectory(sourceRoot.string(), config, 7, frame, false);
    require(status.ok(), "expected SourceImg phase input to load without color files when color is excluded");
    require(frame.frame.leftStripes.size() == 3, "expected phase input when color is excluded");
    require(frame.frame.rightStripes.size() == 3, "expected right phase input when color is excluded");
    require(frame.frame.leftColor.data == nullptr, "expected leftColor to remain empty when color is excluded");
    require(frame.frame.color.data == nullptr, "expected color to remain empty when color is excluded");

    ReconsConfig invalidColorConfig = config;
    invalidColorConfig.colorTextureProjectorIndices = {16, 17};
    status = loadSourceImgFrameDirectory(sourceRoot.string(), invalidColorConfig, 7, frame, true);
    std::filesystem::remove_all(sourceRoot);
    require(status.code == StatusCode::ConfigInvalidValue,
            "expected direct loader call to reject invalid color projector indices");

    const auto singleStripeRoot =
        std::filesystem::temp_directory_path() / "rof_single_stripe_loader_test";
    const auto singleStripeLeft = singleStripeRoot / "9" / "L";
    const auto singleStripeRight = singleStripeRoot / "9" / "R";
    std::filesystem::remove_all(singleStripeRoot);
    std::filesystem::create_directories(singleStripeLeft);
    std::filesystem::create_directories(singleStripeRight);
    for (int projectorIndex = 1; projectorIndex <= 3; ++projectorIndex) {
        writeTinyBmp(singleStripeLeft / (std::to_string(projectorIndex) + ".bmp"),
                     static_cast<unsigned char>(60 + projectorIndex));
        writeTinyBmp(singleStripeRight / (std::to_string(projectorIndex) + ".bmp"),
                     static_cast<unsigned char>(90 + projectorIndex));
    }
    status = loadSingleStripeBmpDirectory(singleStripeRoot.string(), config, 9, frame, false);
    std::filesystem::remove_all(singleStripeRoot);
    require(status.ok(), "expected singleStripe phase input to load without color files when color is excluded");
    require(frame.frame.frameId == 9, "expected singleStripe frame id");
    require(frame.frame.leftStripes.size() == 3, "expected singleStripe left phase input");
    require(frame.frame.rightStripes.size() == 3, "expected singleStripe right phase input");
    require(frame.frame.leftColor.data == nullptr, "expected singleStripe leftColor to remain empty");
    require(frame.frame.color.data == nullptr, "expected singleStripe color to remain empty");

    return EXIT_SUCCESS;
}
