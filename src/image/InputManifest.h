#pragma once

#include "image/ImageTypes.h"
#include "io/JsonFile.h"
#include "config/ReconsConfig.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct ManifestStripe {
    int frequencyIndex = -1;
    int phaseStepIndex = -1;
    int projectorIndex = 0;
    double fill = 0.0;
    std::string path;
};

struct InputManifest {
    std::uint64_t frameId = 0;
    int width = 0;
    int height = 0;
    int channels = 1;
    ImageElementType elementType = ImageElementType::UInt8;
    std::vector<ManifestStripe> leftStripes;
    std::vector<ManifestStripe> rightStripes;
};

struct ManifestFrame {
    StripeFrameGroup frame;
    std::vector<std::vector<unsigned char>> buffers;
};

Status loadInputManifest(const std::string& path, InputManifest& manifest);
Status buildFrameFromManifest(const InputManifest& manifest, ManifestFrame& output);
Status loadSingleStripeBmpDirectory(const std::string& root,
                                    const ReconsConfig& config,
                                    int groupIndex,
                                    ManifestFrame& output,
                                    bool includeColor);
Status loadSourceImgFrameDirectory(const std::string& root,
                                   const ReconsConfig& config,
                                   int frameIndex,
                                   ManifestFrame& output,
                                   bool includeColor);
std::string summarizeManifest(const InputManifest& manifest);

} // namespace reconstruct_one_frame
