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

    ReconsConfig config;
    config.stripeRequirements.clear();
    config.stripeRequirements.push_back({0, 15, 2, 1, -1});
    config.stripeRequirements.push_back({1, 21, 1, 3, -1});
    status = loadSourceImgFrameDirectory(sourceRoot.string(), config, 7, frame);
    std::filesystem::remove_all(sourceRoot);
    require(status.ok(), "expected SourceImg frame to load");
    require(frame.frame.frameId == 7, "expected SourceImg frame id");
    require(frame.frame.leftStripes.size() == 3, "expected SourceImg left stripe count");
    require(frame.frame.rightStripes.size() == 3, "expected SourceImg right stripe count");
    require(frame.frame.leftStripes[0].projectorIndex == 1, "expected zero-based SourceImg to map to projectorIndex 1");
    require(frame.frame.leftStripes[2].frequencyIndex == 1, "expected second requirement frequency");

    return EXIT_SUCCESS;
}
