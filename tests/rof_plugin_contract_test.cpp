#include "reconstruct_one_frame/rof_c_api.h"

#include <cstddef>
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

RofStatusV1 makeStatus()
{
    RofStatusV1 status {};
    status.struct_size = sizeof(status);
    return status;
}

RofSessionConfigV1 makeConfig(const std::string& configPath, const std::string& calibrationPath)
{
    RofSessionConfigV1 config {};
    config.struct_size = sizeof(config);
    config.config_path = configPath.data();
    config.config_path_size = configPath.size();
    config.calibration_path = calibrationPath.data();
    config.calibration_path_size = calibrationPath.size();
    config.output_mask = ROF_OUTPUT_ALL_V1;
    return config;
}

} // namespace

int main()
{
    RofApiV1 api {};
    api.struct_size = sizeof(api);
    require(rof_get_api(ROF_ABI_MAJOR_V1, ROF_ABI_MINOR_V1, sizeof(api), &api) == ROF_STATUS_OK_V1,
            "ABI v1 negotiation must succeed");
    require(api.abi_major == ROF_ABI_MAJOR_V1 && api.abi_minor == ROF_ABI_MINOR_V1,
            "negotiated ABI version must be v1");
    require(std::strcmp(api.plugin_id, "reconstructOneFrame") == 0,
            "plugin id must identify reconstructOneFrame");
    require(api.create_session && api.get_capture_plan && api.get_camera_model &&
            api.process_frame && api.copy_last_error && api.drain && api.destroy_session,
            "all v1 function pointers must be populated");
    require((api.capabilities & ROF_CAPABILITY_FRAME_FLAGS_V1) != 0U &&
                (api.capabilities & ROF_CAPABILITY_METAL_SCAN_MODE_V1) != 0U &&
                (api.capabilities & ROF_CAPABILITY_AI_SCAN_MODE_V1) == 0U,
            "plugin must advertise metal frame flags without claiming AI support");

    RofApiV1 incompatible {};
    incompatible.struct_size = sizeof(incompatible);
    require(rof_get_api(2U, 0U, sizeof(incompatible), &incompatible) == ROF_STATUS_ABI_MISMATCH_V1,
            "unsupported ABI major must fail closed");

    const std::string calibrationPath = "tests/data/phase2_valid_calibResult.json";
    const std::string missingConfigPath = "config/does_not_exist.json";
    RofSessionConfigV1 missingConfig = makeConfig(missingConfigPath, calibrationPath);
    RofSessionHandle session = nullptr;
    RofStatusV1 status = makeStatus();
    require(api.create_session(&missingConfig, &session, &status) == ROF_STATUS_CONFIG_ERROR_V1,
            "missing config must return stable config error");
    require(session == nullptr, "failed create must not publish a session");

    struct ErrorBufferWithCanary {
        std::uint32_t prefix = 0xA5A5A5A5U;
        char payload[8] {};
        std::uint32_t suffix = 0x5A5A5A5AU;
    } shortError;
    size_t requiredErrorSize = 0;
    require(api.copy_last_error(
                nullptr, shortError.payload, sizeof(shortError.payload), &requiredErrorSize) ==
                ROF_STATUS_BUFFER_TOO_SMALL_V1,
            "short error buffer must report required capacity");
    require(requiredErrorSize > sizeof(shortError.payload) &&
                shortError.payload[sizeof(shortError.payload) - 1U] == '\0',
            "error copy must be bounded and null terminated");
    require(shortError.prefix == 0xA5A5A5A5U && shortError.suffix == 0x5A5A5A5AU,
            "bounded error copy must preserve surrounding canaries");

    const std::string configPath = "config/reconsAlgPara.json";
    RofSessionConfigV1 validConfig = makeConfig(configPath, calibrationPath);
    status = makeStatus();
    require(api.create_session(&validConfig, &session, &status) == ROF_STATUS_OK_V1,
            "valid session config must initialize");
    require(session != nullptr, "successful create must publish a session");

    RofCapturePlanV1 plan {};
    plan.struct_size = sizeof(plan);
    status = makeStatus();
    require(api.get_capture_plan(session, &plan, &status) == ROF_STATUS_OK_V1,
            "ready session must provide capture plan");
    require(plan.input_width == 424U && plan.input_height == 400U,
            "capture plan input size must match config");
    require(plan.live_image_count == 18U,
            "capture plan must include phase and color images");
    require(plan.preferred_input_element_type == ROF_ELEMENT_UINT8_V1,
            "capture plan must prefer raw uint8 input");
    require(plan.input_coordinate_space == ROF_COORDINATE_CALIBRATION_INPUT_V1,
            "capture plan must name the calibration input coordinate space");
    require(plan.max_in_flight_frames == 1U,
            "ABI v1 sessions must declare serial processing");
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

    RofCapturePlanV1 legacyPlan {};
    legacyPlan.struct_size = static_cast<std::uint32_t>(
        offsetof(RofCapturePlanV1, preferred_input_element_type));
    status = makeStatus();
    require(api.get_capture_plan(session, &legacyPlan, &status) == ROF_STATUS_OK_V1 &&
                legacyPlan.live_image_count == 18U,
            "ABI v1.0 capture plan prefix must remain supported");

    RofCameraModelV1 camera {};
    camera.struct_size = sizeof(camera);
    status = makeStatus();
    require(api.get_camera_model(session, &camera, &status) == ROF_STATUS_OK_V1,
            "ready session must provide camera model");
    require(camera.model_type == ROF_CAMERA_MODEL_REPROJECTION_Q_4X4_V1 &&
                camera.rows == 4U && camera.cols == 4U,
            "camera model must explicitly identify Q");

    RofFrameInputV1 invalidInput {};
    invalidInput.struct_size = sizeof(invalidInput);
    RofFrameOutputV1 invalidOutput {};
    invalidOutput.struct_size = sizeof(invalidOutput);
    RofFrameMetricsV1 metrics {};
    metrics.struct_size = sizeof(metrics);
    status = makeStatus();
    invalidInput.struct_size = ROF_FRAME_INPUT_V11_SIZE_V1;
    require(api.process_frame(session, &invalidInput, &invalidOutput, &metrics, &status) ==
                ROF_STATUS_INPUT_ERROR_V1,
            "ABI v1.1 frame input prefix must remain accepted");

    invalidInput.struct_size = sizeof(invalidInput);
    invalidInput.flags = ROF_FRAME_FLAG_AI_SCAN_V1;
    status = makeStatus();
    require(api.process_frame(session, &invalidInput, &invalidOutput, &metrics, &status) ==
                ROF_STATUS_INPUT_ERROR_V1,
            "unsupported AI frame mode must fail closed");

    invalidInput.flags = 0U;
    status = makeStatus();
    require(api.process_frame(session, &invalidInput, &invalidOutput, &metrics, &status) ==
                ROF_STATUS_INPUT_ERROR_V1,
            "empty frame input must be rejected without crossing the ABI");

    status = makeStatus();
    require(api.drain(session, &status) == ROF_STATUS_OK_V1,
            "drain must be explicit and idempotent");
    api.destroy_session(session);

    return EXIT_SUCCESS;
}
