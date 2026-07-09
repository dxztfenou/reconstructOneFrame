#include "image/ImagePreprocessor.h"

namespace reconstruct_one_frame {

PreprocessResult buildPreprocessDryRunPlan(const StripeFrameGroup& frame,
                                           const CalibrationModel* calibration)
{
    PreprocessResult result;
    result.stats.stageName = "image_preprocess_dry_run";
    result.stats.inputImageCount = frame.leftStripes.size() + frame.rightStripes.size();
    result.stats.validImageCount = result.stats.inputImageCount;

    const StripeImage* first = nullptr;
    if (!frame.leftStripes.empty()) {
        first = &frame.leftStripes.front();
    } else if (!frame.rightStripes.empty()) {
        first = &frame.rightStripes.front();
    }

    if (first == nullptr) {
        result.status = {StatusCode::InputMissing, "ImagePreprocessor", "no stripe images are available"};
        result.stats.status = result.status;
        return result;
    }

    result.plan.width = first->image.width;
    result.plan.height = first->image.height;
    result.plan.channels = first->image.channels;
    result.plan.elementType = first->image.elementType;
    result.plan.stripeCount = result.stats.inputImageCount;
    result.plan.normalizedFloatRequired = true;
    result.plan.gpuUploadRequired = false;
    result.plan.rectificationRequired = calibration != nullptr && !calibration->sourcePath.empty();
    result.status = {};
    result.stats.status = {};
    return result;
}

} // namespace reconstruct_one_frame
