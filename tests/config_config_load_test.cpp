#include "config/ReconsConfig.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    ReconsConfig config;
    Status status = loadReconsConfig("config/reconsAlgPara.json", config);
    require(status.ok(), "expected valid config to load");
    require(config.imageWidth == 424, "expected image width from config");
    require(config.imageHeight == 400, "expected image height from config");
    require(config.frequencyCount == 3, "expected FREQ from config");
    require(config.stepCount == 5, "expected STEP from config");
    require(config.freqSeries.size() == 3, "expected freqSeries size");
    require(config.freq23 == 7, "expected freq23 from config");
    require(config.bMin == 1, "expected Bmin from config");
    require(config.phaseMinModulation == 0.1, "expected phase modulation threshold from config");
    require(config.phaseMinMeanModulation == 0.0, "expected disabled mean phase modulation gate from config");
    require(config.medianKernelSize == 5, "expected winSizeMedian from config");
    require(config.phaseStepCounts.size() == 3, "expected phaseStepCounts size");
    require(config.phaseStepCounts[0] == 5, "expected low frequency 5-step production contract");
    require(config.phaseStepCounts[1] == 5, "expected middle frequency 5-step contract");
    require(config.phaseStepCounts[2] == 5, "expected high frequency 5-step contract");
    require(config.stripeRequirements.size() == 3, "expected stripe requirement count");
    require(config.stripeRequirements[0].requiredPhaseSteps == 5, "expected per-frequency phase steps");
    require(config.stripeRequirements[1].firstProjectorIndex == 6, "expected projector offset after first frequency");
    require(config.stripeRequirements[2].firstProjectorIndex == 11, "expected projector offset after second frequency");
    require(!config.phaseUnwrapResidualGateEnabled, "expected residual gate flag from config");
    require(config.phaseUnwrapAbs23ResidualThreshold == 0.2, "expected abs23 residual threshold");
    require(config.phaseUnwrapFinalResidualThreshold == 0.2, "expected final residual threshold");
    require(config.disparityWindowEnabled, "expected physical disparity window enabled");
    require(config.matchingLeftRightConsistencyEnabled, "expected left-right consistency enabled");
    require(config.matchingRightPhaseMonotonicEnabled, "expected right-phase monotonicity enabled");
    require(config.matchingRightPhaseMonotonicRadius == 1, "expected right-phase monotonic radius");
    require(config.matchingRightPhaseMinSlope == 0.0001, "expected right-phase minimum slope");
    require(!config.matchingRejectOnSubpixelFailure, "expected subpixel-failure reject disabled by default");
    require(!config.matchingCandidateQualityFilterEnabled,
            "expected matching candidate quality filter disabled by default");
    require(config.matchingCandidateMinModulation == 0.1,
            "expected matching candidate modulation threshold from config");
    require(config.matchingCandidateRejectSaturation,
            "expected matching candidate saturation rejection flag from config");
    require(config.matchingCandidateRejectLowLight,
            "expected matching candidate low-light rejection flag from config");
    require(config.disparitySubpixelEnabled, "expected disparity subpixel interpolation enabled");
    require(config.pointCloudSmoothingEnabled, "expected production smoothing to mirror Legacy final point-cloud blur");
    require(config.pointCloudFilterEnabled, "expected production point-cloud filter enabled");
    require(config.colorTextureEnabled, "expected production color texture enabled");
    require(config.colorTextureProjectorIndices == std::vector<int>({16, 17, 18}), "expected BGR auxiliary projector indices");
    require(config.colorCorrectionMatrix.size() == 12, "expected 3x4 color correction matrix");
    require(config.colorGamma == 0.5, "expected legacy-compatible color gamma");
    require(config.clear255, "expected production clear255 enabled");
    require(config.clear255DilateRadius == 3, "expected production clear255 radius");
    require(config.colorHighlightCompressionEnabled,
            "expected production non-metal highlight compression enabled");
    require(config.qualityInfoMinModulation == 0.1,
            "expected quality info modulation threshold from config");
    require(!config.phaseDiagnosticsEnabled, "expected phase diagnostics disabled by default");
    require(!config.matchingDiagnosticsEnabled, "expected matching diagnostics disabled by default");
    require(!config.pointCloudStageDiagnosticsEnabled,
            "expected point-cloud stage diagnostics disabled by default");
    require(!config.saveStagePointClouds, "expected legacy stage point-cloud alias disabled by default");

    const auto legacyCompatiblePath = std::filesystem::temp_directory_path() / "rof_legacy_compatible_config.json";
    {
        std::ofstream out(legacyCompatiblePath);
        out << "{}";
    }
    ReconsConfig legacyCompatible;
    status = loadReconsConfig(legacyCompatiblePath.string(), legacyCompatible);
    ReconsConfig noBaseCompatible;
    Status noBaseStatus = loadReconsConfigWithBase("", legacyCompatiblePath.string(), noBaseCompatible);
    std::filesystem::remove(legacyCompatiblePath);
    require(status.ok(), "expected missing matching keys to use backward-compatible defaults");
    require(noBaseStatus.ok(), "expected empty config base to keep single-file load compatibility");
    require(!legacyCompatible.disparityWindowEnabled, "expected missing disparity window key to remain disabled");
    require(!legacyCompatible.matchingLeftRightConsistencyEnabled, "expected missing left-right key to remain disabled");
    require(!legacyCompatible.matchingRightPhaseMonotonicEnabled, "expected missing monotonic key to remain disabled");
    require(!legacyCompatible.matchingRejectOnSubpixelFailure,
            "expected missing subpixel-failure reject key to remain disabled");
    require(!legacyCompatible.matchingCandidateQualityFilterEnabled,
            "expected missing candidate-quality key to remain disabled");
    require(legacyCompatible.phaseMinModulation == 0.1,
            "expected compatible phase modulation default");
    require(legacyCompatible.matchingCandidateMinModulation == 0.1,
            "expected compatible matching candidate modulation default");
    require(legacyCompatible.qualityInfoMinModulation == 0.1,
            "expected compatible quality info modulation default");
    require(!legacyCompatible.disparitySubpixelEnabled, "expected missing subpixel key to remain disabled");
    require(!legacyCompatible.pointCloudSmoothingEnabled, "expected missing smoothing key to remain disabled");
    require(!legacyCompatible.pointCloudFilterEnabled, "expected missing point-cloud filter key to remain disabled");
    require(!legacyCompatible.colorTextureEnabled, "expected missing color texture key to remain disabled");
    require(!legacyCompatible.clear255, "expected missing clear255 key to remain disabled");
    require(legacyCompatible.clear255DilateRadius == 3, "expected compatible clear255 radius default");
    require(!legacyCompatible.colorHighlightCompressionEnabled,
            "expected missing highlight compression key to remain disabled");
    require(!legacyCompatible.phaseDiagnosticsEnabled,
            "expected missing phase diagnostics key to remain disabled");
    require(!legacyCompatible.matchingDiagnosticsEnabled,
            "expected missing matching diagnostics key to remain disabled");
    require(!legacyCompatible.pointCloudStageDiagnosticsEnabled,
            "expected missing point-cloud diagnostics key to remain disabled");
    require(!noBaseCompatible.pointCloudFilterEnabled, "expected no-base overlay to preserve missing filter default");
    require(!noBaseCompatible.colorTextureEnabled, "expected no-base overlay to preserve missing color default");

    const auto overridePath = std::filesystem::temp_directory_path() / "rof_dataset_override_config.json";
    {
        std::ofstream out(overridePath);
        out << R"({"minZ":60,"qualityInfoEnabled":true})";
    }
    ReconsConfig overlaid;
    status = loadReconsConfigWithBase("config/reconsAlgPara.json", overridePath.string(), overlaid);
    std::filesystem::remove(overridePath);
    require(status.ok(), "expected dataset override to merge with production base config");
    require(overlaid.minZ == 60.0, "expected override config to replace base scalar");
    require(overlaid.pointCloudFilterEnabled, "expected base point-cloud filter to survive missing override key");
    require(overlaid.colorTextureEnabled, "expected base color texture to survive missing override key");
    require(overlaid.qualityInfoEnabled, "expected explicit override key to replace base quality flag");
    require(overlaid.disparitySubpixelEnabled, "expected base subpixel flag to survive missing override key");

    const auto diagnosticsPath = std::filesystem::temp_directory_path() / "rof_diagnostics_config.json";
    {
        std::ofstream out(diagnosticsPath);
        out << R"({"phaseDiagnosticsEnabled":true,"matchingDiagnosticsEnabled":true,"pointCloudStageDiagnosticsEnabled":true,"saveStagePointClouds":true})";
    }
    ReconsConfig diagnosticsConfig;
    status = loadReconsConfig(diagnosticsPath.string(), diagnosticsConfig);
    std::filesystem::remove(diagnosticsPath);
    require(status.ok(), "expected diagnostics config to load");
    require(diagnosticsConfig.phaseDiagnosticsEnabled, "expected phase diagnostics parse true");
    require(diagnosticsConfig.matchingDiagnosticsEnabled, "expected matching diagnostics parse true");
    require(diagnosticsConfig.pointCloudStageDiagnosticsEnabled,
            "expected point-cloud stage diagnostics parse true");
    require(diagnosticsConfig.saveStagePointClouds,
            "expected legacy saveStagePointClouds alias to keep parsing true");

    const auto matchingConstraintPath =
        std::filesystem::temp_directory_path() / "rof_matching_constraints_config.json";
    {
        std::ofstream out(matchingConstraintPath);
        out << R"({"phaseMinModulation":-1,"matchingRejectOnSubpixelFailure":true,"matchingCandidateQualityFilterEnabled":true,"matchingCandidateMinModulation":0.25,"matchingCandidateRejectSaturation":false,"matchingCandidateRejectLowLight":false})";
    }
    ReconsConfig matchingConstraintConfig;
    status = loadReconsConfig(matchingConstraintPath.string(), matchingConstraintConfig);
    std::filesystem::remove(matchingConstraintPath);
    require(status.ok(), "expected matching constraint config to load");
    require(matchingConstraintConfig.matchingRejectOnSubpixelFailure,
            "expected subpixel-failure reject parse true");
    require(matchingConstraintConfig.matchingCandidateQualityFilterEnabled,
            "expected matching candidate quality parse true");
    require(matchingConstraintConfig.phaseMinModulation == -1.0,
            "expected phase modulation gate disable override");
    require(matchingConstraintConfig.matchingCandidateMinModulation == 0.25,
            "expected matching candidate modulation override");
    require(!matchingConstraintConfig.matchingCandidateRejectSaturation,
            "expected matching candidate saturation override false");
    require(!matchingConstraintConfig.matchingCandidateRejectLowLight,
            "expected matching candidate low-light override false");

    ReconsConfig missingBase;
    status = loadReconsConfigWithBase("config/does_not_exist.json", "config/reconsAlgPara.json", missingBase);
    require(status.code == StatusCode::ConfigMissing, "expected missing base config to fail");
    require(status.message.find("base config:") != std::string::npos,
            "expected missing base config error to identify the base layer");

    const auto invalidModulationPath =
        std::filesystem::temp_directory_path() / "rof_invalid_modulation_config.json";
    {
        std::ofstream out(invalidModulationPath);
        out << R"({"matchingCandidateMinModulation":8.0})";
    }
    ReconsConfig invalidModulation;
    status = loadReconsConfig(invalidModulationPath.string(), invalidModulation);
    std::filesystem::remove(invalidModulationPath);
    require(status.code == StatusCode::ConfigInvalidValue,
            "expected old grayscale-amplitude modulation threshold to fail");

    const auto invalidColorPath = std::filesystem::temp_directory_path() / "rof_invalid_color_config.json";
    {
        std::ofstream out(invalidColorPath);
        out << R"({"colorTextureEnabled":true,"colorTextureProjectorIndices":[16,17],"colorCorrectionMatrix":[1,0,0,0,0,1,0,0,0,0,1,0],"colorGamma":0.5})";
    }
    ReconsConfig invalidColor;
    status = loadReconsConfig(invalidColorPath.string(), invalidColor);
    std::filesystem::remove(invalidColorPath);
    require(status.code == StatusCode::ConfigInvalidValue, "expected invalid color projector indices to fail");

    const auto invalidClearRadiusPath =
        std::filesystem::temp_directory_path() / "rof_invalid_clear255_radius_config.json";
    {
        std::ofstream out(invalidClearRadiusPath);
        out << R"({"clear255DilateRadius":33})";
    }
    ReconsConfig invalidClearRadius;
    status = loadReconsConfig(invalidClearRadiusPath.string(), invalidClearRadius);
    std::filesystem::remove(invalidClearRadiusPath);
    require(status.code == StatusCode::ConfigInvalidValue,
            "expected excessive clear255 dilation radius to fail");

    ReconsConfig missing;
    status = loadReconsConfig("config/does_not_exist.json", missing);
    require(status.code == StatusCode::ConfigMissing, "expected ConfigMissing");

    const auto invalidPath = std::filesystem::temp_directory_path() / "rof_invalid_config.json";
    {
        std::ofstream out(invalidPath);
        out << "not json";
    }
    ReconsConfig invalid;
    status = loadReconsConfig(invalidPath.string(), invalid);
    std::filesystem::remove(invalidPath);
    require(status.code == StatusCode::ConfigParseFailed, "expected ConfigParseFailed");

    return EXIT_SUCCESS;
}
