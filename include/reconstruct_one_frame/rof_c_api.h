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

#define ROF_ABI_MAJOR_V1 1U
#define ROF_ABI_MINOR_V1 2U

#define ROF_STATUS_OK_V1 0
#define ROF_STATUS_INVALID_ARGUMENT_V1 1
#define ROF_STATUS_ABI_MISMATCH_V1 2
#define ROF_STATUS_CONFIG_ERROR_V1 10
#define ROF_STATUS_CALIBRATION_ERROR_V1 11
#define ROF_STATUS_NOT_READY_V1 20
#define ROF_STATUS_INPUT_ERROR_V1 30
#define ROF_STATUS_PROCESSING_ERROR_V1 40
#define ROF_STATUS_BUFFER_TOO_SMALL_V1 50
#define ROF_STATUS_INTERNAL_ERROR_V1 100

#define ROF_SEVERITY_INFO_V1 0U
#define ROF_SEVERITY_WARNING_V1 1U
#define ROF_SEVERITY_ERROR_V1 2U

#define ROF_STATUS_FLAG_RETRIABLE_V1 (1U << 0U)

#define ROF_ELEMENT_UINT8_V1 1U
#define ROF_ELEMENT_UINT16_V1 2U
#define ROF_ELEMENT_FLOAT32_V1 3U

#define ROF_MEMORY_HOST_V1 1U

#define ROF_CAMERA_MODEL_INTRINSICS_3X3_V1 1U
#define ROF_CAMERA_MODEL_REPROJECTION_Q_4X4_V1 2U

#define ROF_COORDINATE_SENSOR_INPUT_V1 1U
#define ROF_COORDINATE_LEFT_CAMERA_MM_V1 2U
#define ROF_COORDINATE_RECTIFIED_LEFT_IMAGE_V1 3U
#define ROF_COORDINATE_CALIBRATION_INPUT_V1 4U

#define ROF_CAPTURE_PLAN_MAX_STRIPE_REQUIREMENTS_V1 8U
#define ROF_CAPTURE_PLAN_MAX_AUXILIARY_FRAMES_V1 4U

#define ROF_OUTPUT_DEPTH_V1 (1U << 0U)
#define ROF_OUTPUT_NORMAL_V1 (1U << 1U)
#define ROF_OUTPUT_COLOR_V1 (1U << 2U)
#define ROF_OUTPUT_QUALITY_V1 (1U << 3U)
#define ROF_OUTPUT_ALL_V1 \
    (ROF_OUTPUT_DEPTH_V1 | ROF_OUTPUT_NORMAL_V1 | ROF_OUTPUT_COLOR_V1 | ROF_OUTPUT_QUALITY_V1)

#define ROF_CAPABILITY_CALLER_OWNED_OUTPUT_V1 (1ULL << 0U)
#define ROF_CAPABILITY_FLOAT32_INPUT_V1 (1ULL << 1U)
#define ROF_CAPABILITY_UINT8_INPUT_V1 (1ULL << 2U)
#define ROF_CAPABILITY_CAPTURE_PLAN_V1 (1ULL << 3U)
#define ROF_CAPABILITY_FRAME_FLAGS_V1 (1ULL << 4U)
#define ROF_CAPABILITY_METAL_SCAN_MODE_V1 (1ULL << 5U)
#define ROF_CAPABILITY_AI_SCAN_MODE_V1 (1ULL << 6U)

#define ROF_FRAME_FLAG_AI_SCAN_V1 (1U << 0U)
#define ROF_FRAME_FLAG_METAL_SCAN_V1 (1U << 1U)
#define ROF_FRAME_FLAG_ALL_V1 (ROF_FRAME_FLAG_AI_SCAN_V1 | ROF_FRAME_FLAG_METAL_SCAN_V1)

#define ROF_FRAME_INPUT_V11_SIZE_V1 136U

typedef void* RofSessionHandle;

typedef struct RofStatusV1 {
    uint32_t struct_size;
    int32_t code;
    uint32_t severity;
    uint32_t flags;
} RofStatusV1;

typedef struct RofSessionConfigV1 {
    uint32_t struct_size;
    const char* config_path;
    size_t config_path_size;
    const char* calibration_path;
    size_t calibration_path_size;
    uint32_t output_mask;
} RofSessionConfigV1;

typedef struct RofStripeRequirementV1 {
    int32_t frequency_index;
    int32_t required_phase_steps;
    int32_t first_projector_index;
    int32_t reserved;
} RofStripeRequirementV1;

typedef struct RofCapturePlanV1 {
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
    RofStripeRequirementV1 stripe_requirements[ROF_CAPTURE_PLAN_MAX_STRIPE_REQUIREMENTS_V1];
    uint32_t auxiliary_frame_count;
    int32_t auxiliary_projector_indices[ROF_CAPTURE_PLAN_MAX_AUXILIARY_FRAMES_V1];
} RofCapturePlanV1;

typedef struct RofCameraModelV1 {
    uint32_t struct_size;
    uint32_t model_type;
    uint32_t rows;
    uint32_t cols;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t coordinate_space;
    double values[16];
} RofCameraModelV1;

typedef struct RofImageStackViewV1 {
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
} RofImageStackViewV1;

typedef struct RofMutableImageViewV1 {
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
} RofMutableImageViewV1;

typedef struct RofFrameInputV1 {
    uint32_t struct_size;
    uint64_t frame_id;
    uint64_t capture_timestamp_ns;
    RofImageStackViewV1 left;
    RofImageStackViewV1 right;
    uint32_t flags;
    uint32_t reserved;
} RofFrameInputV1;

typedef struct RofFrameOutputV1 {
    uint32_t struct_size;
    uint32_t output_mask;
    RofMutableImageViewV1 depth;
    RofMutableImageViewV1 normal;
    RofMutableImageViewV1 color;
    RofMutableImageViewV1 quality;
} RofFrameOutputV1;

typedef struct RofFrameMetricsV1 {
    uint32_t struct_size;
    uint64_t frame_id;
    int32_t status_code;
    uint32_t output_width;
    uint32_t output_height;
    uint64_t point_count;
    double elapsed_ms;
} RofFrameMetricsV1;

typedef int32_t (*RofCreateSessionFnV1)(
    const RofSessionConfigV1* config,
    RofSessionHandle* out_session,
    RofStatusV1* out_status);
typedef int32_t (*RofGetCapturePlanFnV1)(
    RofSessionHandle session,
    RofCapturePlanV1* out_plan,
    RofStatusV1* out_status);
typedef int32_t (*RofGetCameraModelFnV1)(
    RofSessionHandle session,
    RofCameraModelV1* out_model,
    RofStatusV1* out_status);
typedef int32_t (*RofProcessFrameFnV1)(
    RofSessionHandle session,
    const RofFrameInputV1* input,
    RofFrameOutputV1* output,
    RofFrameMetricsV1* out_metrics,
    RofStatusV1* out_status);
typedef int32_t (*RofCopyLastErrorFnV1)(
    RofSessionHandle session,
    char* destination,
    size_t capacity,
    size_t* out_required_size);
typedef int32_t (*RofDrainFnV1)(
    RofSessionHandle session,
    RofStatusV1* out_status);
typedef void (*RofDestroySessionFnV1)(RofSessionHandle session);

typedef struct RofApiV1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
    uint64_t capabilities;
    char plugin_id[32];
    RofCreateSessionFnV1 create_session;
    RofGetCapturePlanFnV1 get_capture_plan;
    RofGetCameraModelFnV1 get_camera_model;
    RofProcessFrameFnV1 process_frame;
    RofCopyLastErrorFnV1 copy_last_error;
    RofDrainFnV1 drain;
    RofDestroySessionFnV1 destroy_session;
} RofApiV1;

ROF_C_API int32_t rof_get_api(
    uint32_t requested_abi_major,
    uint32_t requested_abi_minor,
    uint32_t host_table_size,
    RofApiV1* out_api);

#ifdef __cplusplus
}
#endif

#endif
