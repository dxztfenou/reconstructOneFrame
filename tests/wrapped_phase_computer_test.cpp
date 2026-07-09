#include "config/ReconsConfig.h"
#include "image\InputManifest.h"
#include "phase\WrappedPhaseComputer.h"

#include <cstdlib>
#include <iostream>

using namespace reconstruct_one_frame;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

ManifestFrame loadManifestFrame(const char* path)
{
    InputManifest manifest;
    Status status = loadInputManifest(path, manifest);
    require(status.ok(), "expected manifest load");
    ManifestFrame frame;
    status = buildFrameFromManifest(manifest, frame);
    require(status.ok(), "expected frame build");
    return frame;
}

} // namespace

int main()
{
    ReconsConfig config;
    Status status = loadReconsConfig("config/reconsAlgPara.json", config);
    require(status.ok(), "expected config load");

    ManifestFrame frame = loadManifestFrame("tests/data/phase2_valid_manifest.json");
    WrappedPhaseResult result = computeWrappedPhaseCpuReference(frame.frame, config);
    require(result.status.ok(), "expected wrapped phase to compute");
    require(result.stats.stageName == "wrapped_phase_compute", "expected wrapped phase stage");
    require(result.frequencies.size() == 6, "expected left/right results for three frequencies");
    require(result.frequencies[0].phase.size() == 16, "expected per-pixel phase output");
    require(result.frequencies[0].modulation.size() == 16, "expected per-pixel modulation output");
    require(result.stats.meanPixelValue > 1.0, "expected non-trivial mean modulation");

    ManifestFrame lowModulation = loadManifestFrame("tests/data/phase3_low_modulation_manifest.json");
    result = computeWrappedPhaseCpuReference(lowModulation.frame, config);
    require(result.status.code == StatusCode::PhaseQualityInsufficient, "expected low modulation failure");

    return EXIT_SUCCESS;
}
