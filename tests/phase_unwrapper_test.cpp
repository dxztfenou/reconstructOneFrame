#include "config/ReconsConfig.h"
#include "image\InputManifest.h"
#include "phase\PhaseUnwrapper.h"
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
    WrappedPhaseResult wrapped = computeWrappedPhaseCuda(frame.frame, config);
    require(wrapped.status.ok(), "expected CUDA wrapped phase to compute");
    wrapped.coordinateDomain = PhaseCoordinateDomain::Rectified;

    UnwrappedPhaseResult unwrapped = computeUnwrappedPhaseCuda(wrapped, config);
    require(unwrapped.status.ok(), "expected CUDA unwrap to compute");
    require(unwrapped.coordinateDomain == PhaseCoordinateDomain::Rectified,
            "expected unwrap to preserve the wrapped-phase coordinate domain");
    require(unwrapped.stats.stageName == "phase_unwrap_cuda", "expected CUDA unwrap stage");
    require(unwrapped.left.absolutePhase.size() == 16, "expected left absolute phase pixels");
    require(unwrapped.right.absolutePhase.size() == 16, "expected right absolute phase pixels");
    require(unwrapped.left.validPixelCount > 0, "expected left valid absolute phase pixels");
    require(unwrapped.right.validPixelCount > 0, "expected right valid absolute phase pixels");
    require(unwrapped.stats.cudaComputedPixels == 32, "expected left+right CUDA unwrap pixels");

    return EXIT_SUCCESS;
}
