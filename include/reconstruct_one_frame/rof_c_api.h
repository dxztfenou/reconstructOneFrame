#ifndef RECONSTRUCT_ONE_FRAME_ROF_C_API_H
#define RECONSTRUCT_ONE_FRAME_ROF_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(ROF_C_API_BUILDING_LIBRARY)
#define ROF_C_API __declspec(dllexport)
#else
#define ROF_C_API __declspec(dllimport)
#endif
#else
#define ROF_C_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ROF_ABI_MAJOR 2U
#define ROF_ABI_MINOR 0U

#define ROF_STATUS_OK 0
#define ROF_STATUS_INVALID_ARGUMENT 1
#define ROF_STATUS_ABI_MISMATCH 2
#define ROF_STATUS_CONFIG_ERROR 10
#define ROF_STATUS_CALIBRATION_ERROR 11
#define ROF_STATUS_NOT_READY 20
#define ROF_STATUS_INPUT_ERROR 30
#define ROF_STATUS_PROCESSING_ERROR 40
#define ROF_STATUS_BUFFER_TOO_SMALL 50
#define ROF_STATUS_INTERNAL_ERROR 100

#define ROF_SEVERITY_INFO 0U
#define ROF_SEVERITY_WARNING 1U
#define ROF_SEVERITY_ERROR 2U

#define ROF_STATUS_FLAG_RETRIABLE (1U << 0U)

#define ROF_ELEMENT_UINT8 1U
#define ROF_ELEMENT_UINT16 2U
#define ROF_ELEMENT_FLOAT32 3U

#define ROF_MEMORY_HOST 1U

#define ROF_CAMERA_MODEL_INTRINSICS_3X3 1U
#define ROF_CAMERA_MODEL_REPROJECTION_Q_4X4 2U

#define ROF_COORDINATE_SENSOR_INPUT 1U
#define ROF_COORDINATE_LEFT_CAMERA_MM 2U
#define ROF_COORDINATE_RECTIFIED_LEFT_IMAGE 3U
#define ROF_COORDINATE_CALIBRATION_INPUT 4U

#define ROF_CAPTURE_PLAN_MAX_STRIPE_REQUIREMENTS 8U
#define ROF_CAPTURE_PLAN_MAX_AUXILIARY_FRAMES 4U

#define ROF_OUTPUT_DEPTH (1U << 0U)
#define ROF_OUTPUT_NORMAL (1U << 1U)
#define ROF_OUTPUT_COLOR (1U << 2U)
#define ROF_OUTPUT_QUALITY (1U << 3U)
#define ROF_OUTPUT_ALL (ROF_OUTPUT_DEPTH | ROF_OUTPUT_NORMAL | ROF_OUTPUT_COLOR | ROF_OUTPUT_QUALITY)

#define ROF_CAPABILITY_CALLER_OWNED_OUTPUT (1ULL << 0U)
#define ROF_CAPABILITY_FLOAT32_INPUT (1ULL << 1U)
#define ROF_CAPABILITY_UINT8_INPUT (1ULL << 2U)
#define ROF_CAPABILITY_CAPTURE_PLAN (1ULL << 3U)
#define ROF_CAPABILITY_FRAME_FLAGS (1ULL << 4U)
#define ROF_CAPABILITY_METAL_SCAN_MODE (1ULL << 5U)
#define ROF_CAPABILITY_AI_SCAN_MODE (1ULL << 6U)
#define ROF_CAPABILITY_INIT_SETCONFIG_CALC (1ULL << 7U)

#define ROF_FRAME_FLAG_AI_SCAN (1U << 0U)
#define ROF_FRAME_FLAG_METAL_SCAN (1U << 1U)
#define ROF_FRAME_FLAG_ALL (ROF_FRAME_FLAG_AI_SCAN | ROF_FRAME_FLAG_METAL_SCAN)

#define ROF_CONFIG_FLAG_DRY_RUN (1U << 0U)
#define ROF_CONFIG_FLAG_DRY_RUN_NO_CALIB (1U << 1U)
#define ROF_CONFIG_FLAG_WRITE_PLY (1U << 2U)
#define ROF_CONFIG_FLAG_OUTPUT_PER_FRAME_SUBDIRECTORY (1U << 3U)

typedef void* RofContextHandle;

typedef struct RofStatus {
    uint32_t struct_size;
    int32_t code;
    uint32_t severity;
    uint32_t flags;
} RofStatus;

typedef struct RofRuntimeInitOptions {
    uint32_t struct_size;
    uint32_t runtime_flags;
    const char* log_directory;
    size_t log_directory_size;
} RofRuntimeInitOptions;

typedef struct RofConfigOptions {
    uint32_t struct_size;
    const char* config_path;
    size_t config_path_size;
    const char* calibration_path;
    size_t calibration_path_size;
    const char* config_base_path;
    size_t config_base_path_size;
    const char* output_directory;
    size_t output_directory_size;
    const char* compare_legacy_ply_path;
    size_t compare_legacy_ply_path_size;
    uint32_t output_mask;
    uint32_t flags;
} RofConfigOptions;

typedef struct RofStripeRequirement {
    int32_t frequency_index;
    int32_t required_phase_steps;
    int32_t first_projector_index;
    int32_t reserved;
} RofStripeRequirement;

typedef struct RofCapturePlan {
    uint32_t struct_size;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t live_image_count;
    uint32_t required_stripe_count;
    uint32_t supported_output_mask;
    uint32_t preferred_input_element_type;
    uint32_t input_coordinate_space;
    uint32_t max_in_flight_frames;
    uint32_t stripe_requirement_count;
    RofStripeRequirement stripe_requirements[ROF_CAPTURE_PLAN_MAX_STRIPE_REQUIREMENTS];
    uint32_t auxiliary_frame_count;
    int32_t auxiliary_projector_indices[ROF_CAPTURE_PLAN_MAX_AUXILIARY_FRAMES];
} RofCapturePlan;

typedef struct RofCameraModel {
    uint32_t struct_size;
    uint32_t model_type;
    uint32_t rows;
    uint32_t cols;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t coordinate_space;
    double values[16];
} RofCameraModel;

typedef struct RofImageStackView {
    uint32_t struct_size;
    const void* data;
    size_t byte_size;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride_bytes;
    uint32_t image_stride_bytes;
    uint32_t image_count;
    uint32_t element_type;
    uint32_t memory_kind;
    uint32_t coordinate_space;
} RofImageStackView;

typedef struct RofMutableImageView {
    uint32_t struct_size;
    void* data;
    size_t capacity_bytes;
    size_t written_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride_bytes;
    uint32_t channels;
    uint32_t element_type;
    uint32_t coordinate_space;
} RofMutableImageView;

typedef struct RofCalcInput {
    uint32_t struct_size;
    uint64_t frame_id;
    uint64_t capture_timestamp_ns;
    RofImageStackView left;
    RofImageStackView right;
    uint32_t flags;
    uint32_t reserved;
} RofCalcInput;

typedef struct RofCalcOutput {
    uint32_t struct_size;
    uint32_t output_mask;
    RofMutableImageView depth;
    RofMutableImageView normal;
    RofMutableImageView color;
    RofMutableImageView quality;
} RofCalcOutput;

typedef struct RofCalcMetrics {
    uint32_t struct_size;
    uint64_t frame_id;
    int32_t status_code;
    uint32_t output_width;
    uint32_t output_height;
    uint64_t point_count;
    double elapsed_ms;
} RofCalcMetrics;

typedef int32_t (*RofInitFn)(
    const RofRuntimeInitOptions* options,
    RofContextHandle* out_context,
    RofStatus* out_status);
typedef int32_t (*RofSetConfigFn)(
    RofContextHandle context,
    const RofConfigOptions* config,
    RofStatus* out_status);
typedef int32_t (*RofGetCapturePlanFn)(
    RofContextHandle context,
    RofCapturePlan* out_plan,
    RofStatus* out_status);
typedef int32_t (*RofGetCameraModelFn)(
    RofContextHandle context,
    RofCameraModel* out_model,
    RofStatus* out_status);
typedef int32_t (*RofCalcFn)(
    RofContextHandle context,
    const RofCalcInput* input,
    RofCalcOutput* output,
    RofCalcMetrics* out_metrics,
    RofStatus* out_status);
typedef int32_t (*RofCopyLastErrorFn)(
    RofContextHandle context,
    char* destination,
    size_t capacity,
    size_t* out_required_size);
typedef int32_t (*RofShutdownFn)(
    RofContextHandle context,
    RofStatus* out_status);
typedef void (*RofDestroyFn)(RofContextHandle context);

typedef struct RofApi {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
    uint64_t capabilities;
    char plugin_id[32];
    RofInitFn init;
    RofSetConfigFn set_config;
    RofGetCapturePlanFn get_capture_plan;
    RofGetCameraModelFn get_camera_model;
    RofCalcFn calc;
    RofCopyLastErrorFn copy_last_error;
    RofShutdownFn shutdown;
    RofDestroyFn destroy;
} RofApi;

ROF_C_API int32_t rof_get_api(
    uint32_t requested_abi_major,
    uint32_t requested_abi_minor,
    uint32_t host_table_size,
    RofApi* out_api);

#ifdef __cplusplus
}
#endif

#endif
