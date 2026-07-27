#include "reconstruct_one_frame/rof_c_api.h"

#include <cstddef>
#include <cstdint>

static_assert(sizeof(void*) == 8U, "published Windows ABI layout requires a 64-bit host");
static_assert(sizeof(RofStatus) == 16U);
static_assert(sizeof(RofRuntimeInitOptions) == 24U);
static_assert(sizeof(RofConfigOptions) == 96U);
static_assert(sizeof(RofStripeRequirement) == 16U);
static_assert(sizeof(RofCapturePlan) == 196U);
static_assert(sizeof(RofCameraModel) == 160U);
static_assert(sizeof(RofImageStackView) == 56U);
static_assert(sizeof(RofMutableImageView) == 56U);
static_assert(sizeof(RofCalcInput) == 144U);
static_assert(sizeof(RofCalcOutput) == 232U);
static_assert(sizeof(RofCalcMetrics) == 48U);
static_assert(sizeof(RofApi) == 120U);

static_assert(offsetof(RofRuntimeInitOptions, log_directory) == 8U);
static_assert(offsetof(RofConfigOptions, config_path) == 8U);
static_assert(offsetof(RofConfigOptions, output_mask) == 88U);
static_assert(offsetof(RofCapturePlan, stripe_requirements) == 48U);
static_assert(offsetof(RofCapturePlan, auxiliary_projector_indices) == 180U);
static_assert(offsetof(RofCalcInput, left) == 24U);
static_assert(offsetof(RofCalcInput, right) == 80U);
static_assert(offsetof(RofCalcInput, flags) == 136U);
static_assert(offsetof(RofCalcInput, reserved) == 140U);
static_assert(offsetof(RofCalcOutput, depth) == 8U);
static_assert(offsetof(RofCalcMetrics, point_count) == 32U);
static_assert(offsetof(RofApi, init) == 56U);
static_assert(offsetof(RofApi, set_config) == 64U);
static_assert(offsetof(RofApi, calc) == 88U);
static_assert(offsetof(RofApi, destroy) == 112U);

static_assert(ROF_ABI_MAJOR == 2U && ROF_ABI_MINOR == 0U);
static_assert(ROF_STATUS_INTERNAL_ERROR == 100);
static_assert(ROF_ELEMENT_UINT8 == 1U && ROF_ELEMENT_FLOAT32 == 3U);
static_assert(ROF_COORDINATE_CALIBRATION_INPUT == 4U);
static_assert(ROF_OUTPUT_ALL == 15U);
static_assert(ROF_FRAME_FLAG_ALL == 3U);
static_assert((ROF_CAPABILITY_INIT_SETCONFIG_CALC & (1ULL << 7U)) != 0U);

int main()
{
    return 0;
}
