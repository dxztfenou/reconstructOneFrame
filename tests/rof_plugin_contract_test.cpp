#include "reconstruct_one_frame/rof_c_api.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

RofStatus makeStatus()
{
    RofStatus status {};
    status.struct_size = sizeof(status);
    return status;
}

RofRuntimeInitOptions makeRuntimeInit()
{
    RofRuntimeInitOptions options {};
    options.struct_size = sizeof(options);
    return options;
}

RofConfigOptions makeConfig(const std::string& configPath, const std::string& calibrationPath)
{
    RofConfigOptions config {};
    config.struct_size = sizeof(config);
    config.config_path = configPath.data();
    config.config_path_size = configPath.size();
    config.calibration_path = calibrationPath.data();
    config.calibration_path_size = calibrationPath.size();
    config.output_mask = ROF_OUTPUT_ALL;
    return config;
}

} // namespace

int main()
{
    RofApi api {};
    api.struct_size = sizeof(api);
    require(rof_get_api(ROF_ABI_MAJOR, ROF_ABI_MINOR, sizeof(api), &api) == ROF_STATUS_OK,
            "ABI v2 negotiation must succeed");
    require(api.abi_major == ROF_ABI_MAJOR && api.abi_minor == ROF_ABI_MINOR,
            "negotiated ABI version must be v2");
    require(std::strcmp(api.plugin_id, "reconstructOneFrame") == 0,
            "plugin id must identify reconstructOneFrame");
    require(api.init && api.set_config && api.get_capture_plan && api.get_camera_model &&
                api.calc && api.copy_last_error && api.shutdown && api.destroy,
            "all v2 function pointers must be populated");
    require((api.capabilities & ROF_CAPABILITY_INIT_SETCONFIG_CALC) != 0U &&
                (api.capabilities & ROF_CAPABILITY_FRAME_FLAGS) != 0U &&
                (api.capabilities & ROF_CAPABILITY_METAL_SCAN_MODE) != 0U &&
                (api.capabilities & ROF_CAPABILITY_AI_SCAN_MODE) == 0U,
            "plugin must advertise init/setConfig/calc and metal frame flags without claiming AI support");

    RofApi incompatible {};
    incompatible.struct_size = sizeof(incompatible);
    require(rof_get_api(ROF_ABI_MAJOR + 1U, 0U, sizeof(incompatible), &incompatible) ==
                ROF_STATUS_ABI_MISMATCH,
            "unsupported ABI major must fail closed");

    RofRuntimeInitOptions runtimeInit = makeRuntimeInit();
    RofContextHandle context = nullptr;
    RofStatus status = makeStatus();
    require(api.init(&runtimeInit, &context, &status) == ROF_STATUS_OK,
            "runtime init must create a context");
    require(context != nullptr, "runtime init must publish a context");

    const std::string calibrationPath = "tests/data/phase2_valid_calibResult.json";
    const std::string missingConfigPath = "config/does_not_exist.json";
    RofConfigOptions missingConfig = makeConfig(missingConfigPath, calibrationPath);
    status = makeStatus();
    require(api.set_config(context, &missingConfig, &status) == ROF_STATUS_CONFIG_ERROR,
            "missing config must return stable config error");

    struct ErrorBufferWithCanary {
        std::uint32_t prefix = 0xA5A5A5A5U;
        char payload[8] {};
        std::uint32_t suffix = 0x5A5A5A5AU;
    } shortError;
    size_t requiredErrorSize = 0;
    require(api.copy_last_error(
                context, shortError.payload, sizeof(shortError.payload), &requiredErrorSize) ==
                ROF_STATUS_BUFFER_TOO_SMALL,
            "short error buffer must report required capacity");
    require(requiredErrorSize > sizeof(shortError.payload) &&
                shortError.payload[sizeof(shortError.payload) - 1U] == '\0',
            "error copy must be bounded and null terminated");
    require(shortError.prefix == 0xA5A5A5A5U && shortError.suffix == 0x5A5A5A5AU,
            "bounded error copy must preserve surrounding canaries");

    const std::string configPath = "config/reconsAlgPara.json";
    RofConfigOptions validConfig = makeConfig(configPath, calibrationPath);
    status = makeStatus();
    require(api.set_config(context, &validConfig, &status) == ROF_STATUS_OK,
            "valid config must initialize execution plan");

    RofCapturePlan plan {};
    plan.struct_size = sizeof(plan);
    status = makeStatus();
    require(api.get_capture_plan(context, &plan, &status) == ROF_STATUS_OK,
            "ready context must provide capture plan");
    require(plan.input_width == 424U && plan.input_height == 400U,
            "capture plan input size must match config");
    require(plan.live_image_count == 18U,
            "capture plan must include phase and color images");
    require(plan.preferred_input_element_type == ROF_ELEMENT_UINT8,
            "capture plan must prefer raw uint8 input");
    require(plan.input_coordinate_space == ROF_COORDINATE_CALIBRATION_INPUT,
            "capture plan must name the calibration input coordinate space");
    require(plan.max_in_flight_frames == 1U,
            "v2 context must declare serial processing");
    require(plan.stripe_requirement_count == 3U &&
                plan.stripe_requirements[0].required_phase_steps == 5 &&
                plan.stripe_requirements[1].required_phase_steps == 5 &&
                plan.stripe_requirements[2].required_phase_steps == 5 &&
                plan.stripe_requirements[0].first_projector_index == 1 &&
                plan.stripe_requirements[1].first_projector_index == 6 &&
                plan.stripe_requirements[2].first_projector_index == 11,
            "capture plan must expose per-frequency phase steps");
    require(plan.auxiliary_frame_count == 3U &&
                plan.auxiliary_projector_indices[0] == 16 &&
                plan.auxiliary_projector_indices[2] == 18,
            "capture plan must expose auxiliary projector indices");

    RofCameraModel camera {};
    camera.struct_size = sizeof(camera);
    status = makeStatus();
    require(api.get_camera_model(context, &camera, &status) == ROF_STATUS_OK,
            "ready context must provide camera model");
    require(camera.model_type == ROF_CAMERA_MODEL_REPROJECTION_Q_4X4 &&
                camera.rows == 4U && camera.cols == 4U,
            "camera model must explicitly identify Q");

    RofCalcInput invalidInput {};
    invalidInput.struct_size = sizeof(invalidInput);
    RofCalcOutput invalidOutput {};
    invalidOutput.struct_size = sizeof(invalidOutput);
    invalidOutput.output_mask = ROF_OUTPUT_ALL;
    RofCalcMetrics metrics {};
    metrics.struct_size = sizeof(metrics);
    status = makeStatus();
    invalidInput.flags = ROF_FRAME_FLAG_AI_SCAN;
    require(api.calc(context, &invalidInput, &invalidOutput, &metrics, &status) ==
                ROF_STATUS_INPUT_ERROR,
            "unsupported AI frame mode must fail closed");

    invalidInput.flags = 0U;
    status = makeStatus();
    require(api.calc(context, &invalidInput, &invalidOutput, &metrics, &status) ==
                ROF_STATUS_INPUT_ERROR,
            "empty calc input must be rejected without crossing the core");

    status = makeStatus();
    require(api.shutdown(context, &status) == ROF_STATUS_OK,
            "shutdown must be explicit and idempotent");
    status = makeStatus();
    require(api.get_capture_plan(context, &plan, &status) == ROF_STATUS_NOT_READY,
            "capture plan after shutdown must fail not-ready");
    api.destroy(context);

    return EXIT_SUCCESS;
}
