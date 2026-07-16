#include "config/ReconsConfig.h"
#include "image\InputManifest.h"
#include "phase\PhaseUnwrapper.h"
#include "phase\WrappedPhaseComputer.h"

#include <cmath>
#include <cstdlib>
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

WrappedPhaseFrequencyResult makeFrequency(CameraSide camera,
                                          int frequencyIndex,
                                          int frequencyValue,
                                          const std::vector<float>& phase)
{
    WrappedPhaseFrequencyResult result;
    result.camera = camera;
    result.frequencyIndex = frequencyIndex;
    result.frequencyValue = frequencyValue;
    result.width = 5;
    result.height = 5;
    result.phase = phase;
    result.validPixelCount = phase.size();
    return result;
}

WrappedPhaseResult makeWrappedPhase(const std::vector<float>& phi0,
                                    const std::vector<float>& phi1,
                                    const std::vector<float>& phi2)
{
    WrappedPhaseResult wrapped;
    wrapped.coordinateDomain = PhaseCoordinateDomain::Rectified;
    for (CameraSide camera : {CameraSide::Left, CameraSide::Right}) {
        wrapped.frequencies.push_back(makeFrequency(camera, 0, 15, phi0));
        wrapped.frequencies.push_back(makeFrequency(camera, 1, 21, phi1));
        wrapped.frequencies.push_back(makeFrequency(camera, 2, 28, phi2));
    }
    return wrapped;
}

WrappedPhaseResult makeAbs23OutlierWrappedPhase()
{
    std::vector<float> phi0(25, 0.0F);
    std::vector<float> phi1(25, 1.0F);
    std::vector<float> phi2(25, 2.0F);

    // Isolate an intermediate abs23 phase-order outlier. Final median remains
    // disabled, so only the Legacy-equivalent abs23 median should fix it.
    phi1[12] = 0.1F;
    return makeWrappedPhase(phi0, phi1, phi2);
}

WrappedPhaseResult makeFinalPhaseOutlierWrappedPhase()
{
    std::vector<float> phi0(25, 0.0F);
    std::vector<float> phi1(25, 1.0F);
    std::vector<float> phi2(25, 2.0F);

    // Keep center abs23 aligned with its neighborhood and isolate a final
    // absolute-phase outlier. This proves the final median switch is separate.
    phi0[12] = 3.0F;
    phi1[12] = 4.0F;
    phi2[12] = 5.0F;
    return makeWrappedPhase(phi0, phi1, phi2);
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

    ReconsConfig noAbs23MedianConfig = config;
    noAbs23MedianConfig.medianKernelSize = -1;
    noAbs23MedianConfig.phaseFinalMedianFilterEnabled = false;
    ReconsConfig abs23MedianConfig = config;
    abs23MedianConfig.medianKernelSize = 5;
    abs23MedianConfig.phaseFinalMedianFilterEnabled = false;

    WrappedPhaseResult abs23Outlier = makeAbs23OutlierWrappedPhase();
    UnwrappedPhaseResult withoutAbs23Median = computeUnwrappedPhaseCuda(abs23Outlier, noAbs23MedianConfig);
    require(withoutAbs23Median.status.ok(), "expected synthetic unwrap without abs23 median");
    UnwrappedPhaseResult withAbs23Median = computeUnwrappedPhaseCuda(abs23Outlier, abs23MedianConfig);
    require(withAbs23Median.status.ok(), "expected synthetic unwrap with abs23 median");
    require(withoutAbs23Median.left.absolutePhase[12] > 50.0F,
            "expected center phase-order outlier without abs23 median");
    require(std::fabs(withAbs23Median.left.absolutePhase[12] - withAbs23Median.left.absolutePhase[0]) < 0.001F,
            "expected abs23 median to remove isolated intermediate unwrap outlier");

    ReconsConfig finalMedianOffConfig = config;
    finalMedianOffConfig.medianKernelSize = 5;
    finalMedianOffConfig.phaseFinalMedianFilterEnabled = false;
    ReconsConfig finalMedianOnConfig = finalMedianOffConfig;
    finalMedianOnConfig.phaseFinalMedianFilterEnabled = true;

    WrappedPhaseResult finalOutlier = makeFinalPhaseOutlierWrappedPhase();
    UnwrappedPhaseResult finalMedianOff = computeUnwrappedPhaseCuda(finalOutlier, finalMedianOffConfig);
    require(finalMedianOff.status.ok(), "expected synthetic unwrap with final median disabled");
    UnwrappedPhaseResult finalMedianOn = computeUnwrappedPhaseCuda(finalOutlier, finalMedianOnConfig);
    require(finalMedianOn.status.ok(), "expected synthetic unwrap with final median enabled");
    require(finalMedianOff.left.absolutePhase[12] > 4.0F,
            "expected final phase outlier to remain when final median is disabled");
    require(std::fabs(finalMedianOn.left.absolutePhase[12] - finalMedianOn.left.absolutePhase[0]) < 0.001F,
            "expected final median to remove isolated final phase outlier only when enabled");

    return EXIT_SUCCESS;
}
