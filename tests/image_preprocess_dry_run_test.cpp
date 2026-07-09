#include "image\ImagePreprocessor.h"
#include "image\InputManifest.h"

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

} // namespace

int main()
{
    InputManifest manifest;
    Status status = loadInputManifest("tests/data/phase2_valid_manifest.json", manifest);
    require(status.ok(), "expected manifest load");
    ManifestFrame frame;
    status = buildFrameFromManifest(manifest, frame);
    require(status.ok(), "expected frame build");

    CalibrationModel calibration;
    calibration.sourcePath = "tests/data/phase2_valid_calibResult.json";
    PreprocessResult result = buildPreprocessDryRunPlan(frame.frame, &calibration);
    require(result.status.ok(), "expected preprocess dry-run status ok");
    require(result.stats.stageName == "image_preprocess_dry_run", "expected preprocess stage name");
    require(result.plan.normalizedFloatRequired, "expected normalized float plan");
    require(result.plan.rectificationRequired, "expected rectification required when calibration exists");
    require(result.plan.stripeCount == 30, "expected stripe count");

    return EXIT_SUCCESS;
}
