#include "image\InputManifest.h"
#include "reconstruct_one_frame/reconstructInterface.h"

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

ManifestFrame loadFrame(const char* path)
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
    ReconstructEngine engine;
    InitOptions options;
    options.configPath = "config/reconsAlgPara.json";
    options.dryRun = true;
    options.dryRunNoCalib = true;
    Status status = engine.init(options);
    require(status.ok(), "expected dry-run-no-calib init");
    ManifestFrame frame = loadFrame("tests/data/phase2_valid_manifest.json");
    FrameResult result = engine.run(frame.frame);
    require(result.status.ok(), "expected phase2 dry-run-no-calib pipeline ok");
    require(result.stats.size() == 6, "expected six phase4 stages");
    require(result.stats[0].stageName == "input_contract_validation", "expected input validation stage");
    require(result.stats[1].skipped, "expected calibration skipped");
    require(result.stats[2].stageName == "image_preprocess_dry_run", "expected preprocess stage");
    require(result.stats[3].stageName == "wrapped_phase_compute_cuda", "expected CUDA wrapped phase stage");
    require(result.stats[3].cudaComputedPixels > 0, "expected CUDA wrapped phase pixels");
    require(result.wrappedPhaseComputed, "expected wrapped phase computed marker");
    require(result.stats[4].stageName == "phase_unwrap_cuda", "expected CUDA unwrap stage");
    require(result.stats[4].cudaComputedPixels > 0, "expected CUDA unwrap pixels");
    require(result.unwrappedPhaseComputed, "expected unwrapped phase computed marker");
    require(result.stats[5].status.code == StatusCode::NotComputed, "expected downstream not computed marker");

    ReconstructEngine calibratedEngine;
    InitOptions calibratedOptions;
    calibratedOptions.configPath = "config/reconsAlgPara.json";
    calibratedOptions.calibrationPath = "tests/data/phase2_valid_calibResult.json";
    calibratedOptions.dryRun = true;
    status = calibratedEngine.init(calibratedOptions);
    require(status.ok(), "expected calibrated init");
    result = calibratedEngine.run(frame.frame);
    require(result.status.ok(), "expected calibrated phase2 dry-run ok");
    require(!result.stats[1].skipped, "expected calibration contract checked");

    ReconstructEngine mismatchEngine;
    InitOptions mismatchOptions;
    mismatchOptions.configPath = "config/reconsAlgPara.json";
    mismatchOptions.calibrationPath = "tests/data/phase2_mismatch_calibResult.json";
    mismatchOptions.dryRun = true;
    status = mismatchEngine.init(mismatchOptions);
    require(status.ok(), "expected mismatch calibration to parse during init");
    result = mismatchEngine.run(frame.frame);
    require(result.status.code == StatusCode::CalibrationImageSizeMismatch, "expected calibration size mismatch at pipeline contract");

    return EXIT_SUCCESS;
}
