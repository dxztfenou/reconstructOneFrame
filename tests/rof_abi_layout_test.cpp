#include "reconstruct_one_frame/rof_c_api.h"

#include <cstddef>
#include <cstdint>

static_assert(sizeof(void*) == 8U, "published Windows ABI layout requires a 64-bit host");
static_assert(sizeof(RofStatusV1) == 16U);
static_assert(sizeof(RofSessionConfigV1) == 48U);
static_assert(sizeof(RofStripeRequirementV1) == 16U);
static_assert(sizeof(RofCapturePlanV1) == 196U);
static_assert(sizeof(RofCameraModelV1) == 160U);
static_assert(sizeof(RofImageStackViewV1) == 56U);
static_assert(sizeof(RofMutableImageViewV1) == 56U);
static_assert(sizeof(RofFrameInputV1) == 144U);
static_assert(sizeof(RofFrameOutputV1) == 232U);
static_assert(sizeof(RofFrameMetricsV1) == 48U);
static_assert(sizeof(RofApiV1) == 112U);

static_assert(offsetof(RofSessionConfigV1, config_path) == 8U);
static_assert(offsetof(RofCapturePlanV1, stripe_requirements) == 48U);
static_assert(offsetof(RofCapturePlanV1, auxiliary_projector_indices) == 180U);
static_assert(offsetof(RofFrameInputV1, left) == 24U);
static_assert(offsetof(RofFrameInputV1, right) == 80U);
static_assert(offsetof(RofFrameInputV1, flags) == ROF_FRAME_INPUT_V11_SIZE_V1);
static_assert(offsetof(RofFrameInputV1, reserved) == 140U);
static_assert(offsetof(RofFrameOutputV1, depth) == 8U);
static_assert(offsetof(RofFrameMetricsV1, point_count) == 32U);
static_assert(offsetof(RofApiV1, create_session) == 56U);

static_assert(ROF_ABI_MAJOR_V1 == 1U && ROF_ABI_MINOR_V1 == 2U);
static_assert(ROF_STATUS_INTERNAL_ERROR_V1 == 100);
static_assert(ROF_ELEMENT_UINT8_V1 == 1U && ROF_ELEMENT_FLOAT32_V1 == 3U);
static_assert(ROF_COORDINATE_CALIBRATION_INPUT_V1 == 4U);
static_assert(ROF_OUTPUT_ALL_V1 == 15U);
static_assert(ROF_FRAME_FLAG_ALL_V1 == 3U);

int main()
{
    return 0;
}
