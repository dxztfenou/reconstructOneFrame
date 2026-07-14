#include "reconstruct_one_frame/rof_c_api.h"

#include "reconstruct_one_frame/reconstructInterface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using reconstruct_one_frame::CameraSide;
using reconstruct_one_frame::CaptureStripeRequirement;
using reconstruct_one_frame::EngineDescriptor;
using reconstruct_one_frame::FrameResult;
using reconstruct_one_frame::ImageElementType;
using reconstruct_one_frame::ImageView;
using reconstruct_one_frame::InitOptions;
using reconstruct_one_frame::ReconstructEngine;
using reconstruct_one_frame::Status;
using reconstruct_one_frame::StatusCode;
using reconstruct_one_frame::StripeFrameGroup;
using reconstruct_one_frame::StripeImage;

thread_local std::string g_lastBoundaryError;

struct RofSessionImpl {
    ReconstructEngine engine;
    EngineDescriptor descriptor;
    std::mutex mutex;
    std::string lastError;
    std::string lastSummary;
    std::uint32_t outputMask = ROF_OUTPUT_ALL_V1;
    bool ready = false;
};

struct OwnedFrame {
    StripeFrameGroup frame;
    std::vector<std::vector<std::uint8_t>> left;
    std::vector<std::vector<std::uint8_t>> right;
    std::vector<std::uint8_t> colorBgr;
};

bool hasStructSize(std::uint32_t actual, std::size_t expected)
{
    return static_cast<std::size_t>(actual) >= expected;
}

void setStatus(RofStatusV1* output,
               std::int32_t code,
               std::uint32_t severity = ROF_SEVERITY_ERROR_V1,
               std::uint32_t flags = 0U)
{
    if (output == nullptr || !hasStructSize(output->struct_size, sizeof(RofStatusV1))) {
        return;
    }
    output->code = code;
    output->severity = code == ROF_STATUS_OK_V1 ? ROF_SEVERITY_INFO_V1 : severity;
    output->flags = flags;
}

std::int32_t mapStatusCode(StatusCode code)
{
    switch (code) {
    case StatusCode::Ok:
        return ROF_STATUS_OK_V1;
    case StatusCode::ConfigMissing:
    case StatusCode::ConfigParseFailed:
    case StatusCode::ConfigInvalidValue:
        return ROF_STATUS_CONFIG_ERROR_V1;
    case StatusCode::CalibrationMissing:
    case StatusCode::CalibrationParseFailed:
    case StatusCode::CalibrationInvalid:
    case StatusCode::CalibrationFieldMissing:
    case StatusCode::CalibrationMatrixShapeInvalid:
    case StatusCode::CalibrationImageSizeMismatch:
        return ROF_STATUS_CALIBRATION_ERROR_V1;
    case StatusCode::InputMissing:
    case StatusCode::InputEmptyImage:
    case StatusCode::InputBlackImage:
    case StatusCode::InputSaturatedImage:
    case StatusCode::InputSizeMismatch:
    case StatusCode::InputTypeUnsupported:
    case StatusCode::InputStrideInvalid:
    case StatusCode::InputInvalidValue:
    case StatusCode::InputMissingLeftStripes:
    case StatusCode::InputMissingRightStripes:
    case StatusCode::InputFrequencyPlanMismatch:
    case StatusCode::InputPhaseStepMissing:
    case StatusCode::InputCameraSideMismatch:
    case StatusCode::InputNonFinitePixel:
    case StatusCode::InputManifestMissing:
    case StatusCode::InputManifestParseFailed:
        return ROF_STATUS_INPUT_ERROR_V1;
    case StatusCode::InternalError:
        return ROF_STATUS_INTERNAL_ERROR_V1;
    default:
        return ROF_STATUS_PROCESSING_ERROR_V1;
    }
}

std::string copyStringView(const char* data, std::size_t size)
{
    if (data == nullptr || size == 0U) {
        return {};
    }
    return std::string(data, size);
}

bool checkedMultiply(std::size_t lhs, std::size_t rhs, std::size_t& output)
{
    if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    output = lhs * rhs;
    return true;
}

std::size_t elementSize(std::uint32_t elementType)
{
    switch (elementType) {
    case ROF_ELEMENT_UINT8_V1:
        return sizeof(std::uint8_t);
    case ROF_ELEMENT_UINT16_V1:
        return sizeof(std::uint16_t);
    case ROF_ELEMENT_FLOAT32_V1:
        return sizeof(float);
    default:
        return 0U;
    }
}

std::int32_t fail(RofSessionImpl* session,
                  RofStatusV1* outputStatus,
                  std::int32_t code,
                  const std::string& message)
{
    if (session != nullptr) {
        session->lastError = message;
    } else {
        g_lastBoundaryError = message;
    }
    setStatus(outputStatus, code);
    return code;
}

bool validateStack(const RofImageStackViewV1& stack,
                   const EngineDescriptor& descriptor,
                   std::string& error)
{
    constexpr std::size_t v10Size = offsetof(RofImageStackViewV1, coordinate_space);
    if (!hasStructSize(stack.struct_size, v10Size)) {
        error = "image stack struct is undersized";
        return false;
    }
    if (stack.data == nullptr || stack.memory_kind != ROF_MEMORY_HOST_V1) {
        error = "image stack must reference host memory";
        return false;
    }
    if (hasStructSize(stack.struct_size, sizeof(RofImageStackViewV1)) &&
        stack.coordinate_space != ROF_COORDINATE_SENSOR_INPUT_V1 &&
        stack.coordinate_space != ROF_COORDINATE_CALIBRATION_INPUT_V1) {
        error = "image stack coordinate space must be SensorInput or CalibrationInput";
        return false;
    }
    if (stack.width != static_cast<std::uint32_t>(descriptor.imageWidth) ||
        stack.height != static_cast<std::uint32_t>(descriptor.imageHeight) ||
        stack.image_count < descriptor.liveImageCount) {
        error = "image stack shape/count does not match capture plan";
        return false;
    }
    const std::size_t bytesPerElement = elementSize(stack.element_type);
    if (bytesPerElement != sizeof(std::uint8_t) && bytesPerElement != sizeof(float)) {
        error = "image stack element type must be UInt8 or Float32";
        return false;
    }
    std::size_t packedRowBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(stack.width), bytesPerElement, packedRowBytes) ||
        stack.row_stride_bytes < packedRowBytes) {
        error = "image stack row stride is invalid";
        return false;
    }
    std::size_t minimumImageBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(stack.row_stride_bytes),
                         static_cast<std::size_t>(stack.height),
                         minimumImageBytes) ||
        stack.image_stride_bytes < minimumImageBytes) {
        error = "image stack image stride is invalid";
        return false;
    }
    std::size_t minimumStackBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(stack.image_stride_bytes),
                         static_cast<std::size_t>(stack.image_count),
                         minimumStackBytes) ||
        stack.byte_size < minimumStackBytes) {
        error = "image stack byte size is smaller than its declared layout";
        return false;
    }
    return true;
}

std::uint8_t floatToByte(float value)
{
    if (!std::isfinite(value)) {
        return 0U;
    }
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 255.0F)));
}

bool copyImageToU8(const RofImageStackViewV1& stack,
                   int imageIndex,
                   std::vector<std::uint8_t>& output,
                   std::string& error)
{
    if (imageIndex < 0 || static_cast<std::uint32_t>(imageIndex) >= stack.image_count) {
        error = "projector index exceeds image stack count";
        return false;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(stack.width) * static_cast<std::size_t>(stack.height);
    output.resize(pixelCount);
    const auto* stackBytes = static_cast<const std::uint8_t*>(stack.data);
    const auto* imageBase = stackBytes + static_cast<std::size_t>(imageIndex) * stack.image_stride_bytes;

    for (std::uint32_t row = 0; row < stack.height; ++row) {
        const auto* sourceRow = imageBase + static_cast<std::size_t>(row) * stack.row_stride_bytes;
        auto* destinationRow = output.data() + static_cast<std::size_t>(row) * stack.width;
        if (stack.element_type == ROF_ELEMENT_UINT8_V1) {
            std::memcpy(destinationRow, sourceRow, stack.width);
            continue;
        }
        for (std::uint32_t col = 0; col < stack.width; ++col) {
            float value = 0.0F;
            std::memcpy(&value, sourceRow + static_cast<std::size_t>(col) * sizeof(float), sizeof(float));
            destinationRow[col] = floatToByte(value);
        }
    }
    return true;
}

bool appendStripe(const RofImageStackViewV1& stack,
                  int projectorIndex,
                  CameraSide camera,
                  const CaptureStripeRequirement& requirement,
                  int phaseStepIndex,
                  std::vector<std::vector<std::uint8_t>>& storage,
                  std::vector<StripeImage>& stripes,
                  std::string& error)
{
    storage.emplace_back();
    if (!copyImageToU8(stack, projectorIndex - 1, storage.back(), error)) {
        storage.pop_back();
        return false;
    }

    ImageView view;
    view.data = storage.back().data();
    view.width = static_cast<int>(stack.width);
    view.height = static_cast<int>(stack.height);
    view.channels = 1;
    view.strideBytes = static_cast<int>(stack.width);
    view.elementType = ImageElementType::UInt8;
    stripes.push_back({camera,
                       requirement.frequencyIndex,
                       phaseStepIndex,
                       projectorIndex,
                       view});
    return true;
}

bool buildFrame(const EngineDescriptor& descriptor,
                const RofFrameInputV1& input,
                std::uint32_t frameFlags,
                OwnedFrame& output,
                std::string& error)
{
    if (!validateStack(input.left, descriptor, error) ||
        !validateStack(input.right, descriptor, error)) {
        return false;
    }

    std::size_t stripeCount = 0U;
    for (const CaptureStripeRequirement& requirement : descriptor.stripeRequirements) {
        stripeCount += static_cast<std::size_t>(requirement.requiredPhaseSteps);
    }
    output.left.reserve(stripeCount);
    output.right.reserve(stripeCount);
    output.frame.leftStripes.reserve(stripeCount);
    output.frame.rightStripes.reserve(stripeCount);
    output.frame.frameId = input.frame_id;
    output.frame.aiScan = (frameFlags & ROF_FRAME_FLAG_AI_SCAN_V1) != 0U;
    output.frame.metalScan = (frameFlags & ROF_FRAME_FLAG_METAL_SCAN_V1) != 0U;

    for (const CaptureStripeRequirement& requirement : descriptor.stripeRequirements) {
        for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
            const int projectorIndex = requirement.firstProjectorIndex + step;
            if (!appendStripe(input.left,
                              projectorIndex,
                              CameraSide::Left,
                              requirement,
                              step,
                              output.left,
                              output.frame.leftStripes,
                              error) ||
                !appendStripe(input.right,
                              projectorIndex,
                              CameraSide::Right,
                              requirement,
                              step,
                              output.right,
                              output.frame.rightStripes,
                              error)) {
                return false;
            }
        }
    }

    if (!descriptor.colorProjectorIndices.empty()) {
        if (descriptor.colorProjectorIndices.size() != 3U) {
            error = "color projector plan must contain exactly three indices";
            return false;
        }
        const std::size_t pixelCount =
            static_cast<std::size_t>(descriptor.imageWidth) * static_cast<std::size_t>(descriptor.imageHeight);
        output.colorBgr.assign(pixelCount * 3U, 0U);
        std::vector<std::uint8_t> channel;
        for (std::size_t channelIndex = 0; channelIndex < 3U; ++channelIndex) {
            if (!copyImageToU8(input.left,
                               descriptor.colorProjectorIndices[channelIndex] - 1,
                               channel,
                               error)) {
                return false;
            }
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
                output.colorBgr[pixel * 3U + channelIndex] = channel[pixel];
            }
        }
        ImageView colorView;
        colorView.data = output.colorBgr.data();
        colorView.width = descriptor.imageWidth;
        colorView.height = descriptor.imageHeight;
        colorView.channels = 3;
        colorView.strideBytes = descriptor.imageWidth * 3;
        colorView.elementType = ImageElementType::UInt8;
        output.frame.color = colorView;
        output.frame.leftColor = colorView;
    }
    return true;
}

template <typename T>
std::int32_t copyOutputImage(const std::vector<T>& source,
                             int width,
                             int height,
                             std::uint32_t elementType,
                             std::uint32_t coordinateSpace,
                             RofMutableImageViewV1& destination,
                             std::string& error)
{
    if (!hasStructSize(destination.struct_size, sizeof(RofMutableImageViewV1))) {
        error = "output image view is undersized";
        return ROF_STATUS_INVALID_ARGUMENT_V1;
    }
    if (source.empty()) {
        destination.written_bytes = 0U;
        destination.width = static_cast<std::uint32_t>(width);
        destination.height = static_cast<std::uint32_t>(height);
        destination.channels = 3U;
        destination.element_type = elementType;
        destination.coordinate_space = coordinateSpace;
        return ROF_STATUS_OK_V1;
    }
    const std::size_t packedRowBytes = static_cast<std::size_t>(width) * 3U * sizeof(T);
    if (destination.data == nullptr || destination.row_stride_bytes < packedRowBytes) {
        error = "output image row stride is too small";
        return ROF_STATUS_BUFFER_TOO_SMALL_V1;
    }
    std::size_t requiredBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(destination.row_stride_bytes),
                         static_cast<std::size_t>(height),
                         requiredBytes) ||
        destination.capacity_bytes < requiredBytes) {
        error = "output image capacity is too small";
        return ROF_STATUS_BUFFER_TOO_SMALL_V1;
    }
    const std::size_t expectedElements = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 3U;
    if (source.size() != expectedElements) {
        error = "materialized output size does not match frame dimensions";
        return ROF_STATUS_PROCESSING_ERROR_V1;
    }

    auto* destinationBytes = static_cast<std::uint8_t*>(destination.data);
    for (int row = 0; row < height; ++row) {
        std::memcpy(destinationBytes + static_cast<std::size_t>(row) * destination.row_stride_bytes,
                    source.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 3U,
                    packedRowBytes);
    }
    destination.written_bytes = packedRowBytes * static_cast<std::size_t>(height);
    destination.width = static_cast<std::uint32_t>(width);
    destination.height = static_cast<std::uint32_t>(height);
    destination.channels = 3U;
    destination.element_type = elementType;
    destination.coordinate_space = coordinateSpace;
    return ROF_STATUS_OK_V1;
}

std::int32_t createSession(const RofSessionConfigV1* config,
                           RofSessionHandle* outSession,
                           RofStatusV1* outStatus) noexcept
{
    if (outSession != nullptr) {
        *outSession = nullptr;
    }
    try {
        if (config == nullptr || outSession == nullptr ||
            !hasStructSize(config->struct_size, sizeof(RofSessionConfigV1))) {
            return fail(nullptr, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "session config/output handle is invalid");
        }
        const std::string configPath = copyStringView(config->config_path, config->config_path_size);
        const std::string calibrationPath =
            copyStringView(config->calibration_path, config->calibration_path_size);
        if (configPath.empty() || calibrationPath.empty()) {
            return fail(nullptr, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "config and calibration paths are required");
        }
        if ((config->output_mask & ~ROF_OUTPUT_ALL_V1) != 0U) {
            return fail(nullptr, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "session output mask contains unsupported bits");
        }

        auto session = std::make_unique<RofSessionImpl>();
        InitOptions options;
        options.configPath = configPath;
        options.calibrationPath = calibrationPath;
        options.materializeFrameOutputs = true;
        const Status initStatus = session->engine.init(options);
        if (!initStatus.ok()) {
            return fail(nullptr,
                        outStatus,
                        mapStatusCode(initStatus.code),
                        initStatus.module + ": " + initStatus.message);
        }
        const Status describeStatus = session->engine.describe(session->descriptor);
        if (!describeStatus.ok()) {
            return fail(nullptr,
                        outStatus,
                        mapStatusCode(describeStatus.code),
                        describeStatus.module + ": " + describeStatus.message);
        }
        session->outputMask = config->output_mask == 0U ? ROF_OUTPUT_ALL_V1 : config->output_mask;
        session->ready = true;
        session->lastError.clear();
        g_lastBoundaryError.clear();
        *outSession = session.release();
        setStatus(outStatus, ROF_STATUS_OK_V1);
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        return fail(nullptr, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, ex.what());
    } catch (...) {
        return fail(nullptr, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, "unknown create_session exception");
    }
}

std::int32_t getCapturePlan(RofSessionHandle handle,
                            RofCapturePlanV1* outPlan,
                            RofStatusV1* outStatus) noexcept
{
    auto* session = static_cast<RofSessionImpl*>(handle);
    try {
        constexpr std::size_t v10Size = offsetof(RofCapturePlanV1, preferred_input_element_type);
        if (session == nullptr || outPlan == nullptr ||
            !hasStructSize(outPlan->struct_size, v10Size)) {
            return fail(session, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "capture plan request is invalid");
        }
        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->ready) {
            return fail(session, outStatus, ROF_STATUS_NOT_READY_V1, "session is not ready");
        }
        std::size_t stripeCount = 0U;
        for (const CaptureStripeRequirement& requirement : session->descriptor.stripeRequirements) {
            stripeCount += static_cast<std::size_t>(requirement.requiredPhaseSteps);
        }
        outPlan->input_width = static_cast<std::uint32_t>(session->descriptor.imageWidth);
        outPlan->input_height = static_cast<std::uint32_t>(session->descriptor.imageHeight);
        outPlan->output_width = static_cast<std::uint32_t>(session->descriptor.imageWidth);
        outPlan->output_height = static_cast<std::uint32_t>(session->descriptor.imageHeight);
        outPlan->live_image_count = session->descriptor.liveImageCount;
        outPlan->required_stripe_count = static_cast<std::uint32_t>(stripeCount);
        outPlan->supported_output_mask = ROF_OUTPUT_ALL_V1;
        if (hasStructSize(outPlan->struct_size, sizeof(RofCapturePlanV1))) {
            if (session->descriptor.stripeRequirements.size() >
                    ROF_CAPTURE_PLAN_MAX_STRIPE_REQUIREMENTS_V1 ||
                session->descriptor.colorProjectorIndices.size() >
                    ROF_CAPTURE_PLAN_MAX_AUXILIARY_FRAMES_V1) {
                return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1,
                            "capture plan exceeds ABI v1 fixed capacity");
            }
            outPlan->preferred_input_element_type = ROF_ELEMENT_UINT8_V1;
            outPlan->input_coordinate_space = ROF_COORDINATE_CALIBRATION_INPUT_V1;
            outPlan->max_in_flight_frames = 1U;
            outPlan->stripe_requirement_count =
                static_cast<std::uint32_t>(session->descriptor.stripeRequirements.size());
            std::fill(std::begin(outPlan->stripe_requirements),
                      std::end(outPlan->stripe_requirements),
                      RofStripeRequirementV1 {});
            for (std::size_t index = 0U;
                 index < session->descriptor.stripeRequirements.size(); ++index) {
                const auto& source = session->descriptor.stripeRequirements[index];
                auto& destination = outPlan->stripe_requirements[index];
                destination.frequency_index = source.frequencyIndex;
                destination.required_phase_steps = source.requiredPhaseSteps;
                destination.first_projector_index = source.firstProjectorIndex;
            }
            outPlan->auxiliary_frame_count =
                static_cast<std::uint32_t>(session->descriptor.colorProjectorIndices.size());
            std::fill(std::begin(outPlan->auxiliary_projector_indices),
                      std::end(outPlan->auxiliary_projector_indices), 0);
            std::copy(session->descriptor.colorProjectorIndices.begin(),
                      session->descriptor.colorProjectorIndices.end(),
                      std::begin(outPlan->auxiliary_projector_indices));
        }
        setStatus(outStatus, ROF_STATUS_OK_V1);
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, ex.what());
    } catch (...) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, "unknown get_capture_plan exception");
    }
}

std::int32_t getCameraModel(RofSessionHandle handle,
                            RofCameraModelV1* outModel,
                            RofStatusV1* outStatus) noexcept
{
    auto* session = static_cast<RofSessionImpl*>(handle);
    try {
        if (session == nullptr || outModel == nullptr ||
            !hasStructSize(outModel->struct_size, sizeof(RofCameraModelV1))) {
            return fail(session, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "camera model request is invalid");
        }
        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->ready) {
            return fail(session, outStatus, ROF_STATUS_NOT_READY_V1, "session is not ready");
        }
        outModel->rows = static_cast<std::uint32_t>(session->descriptor.cameraModelRows);
        outModel->cols = static_cast<std::uint32_t>(session->descriptor.cameraModelCols);
        outModel->model_type = session->descriptor.cameraModelRows == 4
            ? ROF_CAMERA_MODEL_REPROJECTION_Q_4X4_V1
            : ROF_CAMERA_MODEL_INTRINSICS_3X3_V1;
        outModel->image_width = static_cast<std::uint32_t>(session->descriptor.imageWidth);
        outModel->image_height = static_cast<std::uint32_t>(session->descriptor.imageHeight);
        outModel->coordinate_space = ROF_COORDINATE_LEFT_CAMERA_MM_V1;
        std::copy(session->descriptor.cameraModelValues.begin(),
                  session->descriptor.cameraModelValues.end(),
                  std::begin(outModel->values));
        setStatus(outStatus, ROF_STATUS_OK_V1);
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, ex.what());
    } catch (...) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, "unknown get_camera_model exception");
    }
}

std::int32_t processFrame(RofSessionHandle handle,
                          const RofFrameInputV1* input,
                          RofFrameOutputV1* output,
                          RofFrameMetricsV1* outMetrics,
                          RofStatusV1* outStatus) noexcept
{
    auto* session = static_cast<RofSessionImpl*>(handle);
    try {
        if (session == nullptr || input == nullptr || output == nullptr ||
            !hasStructSize(input->struct_size, ROF_FRAME_INPUT_V11_SIZE_V1) ||
            !hasStructSize(output->struct_size, sizeof(RofFrameOutputV1)) ||
            (outMetrics != nullptr &&
             !hasStructSize(outMetrics->struct_size, sizeof(RofFrameMetricsV1)))) {
            return fail(session, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1,
                        "frame input/output descriptor is invalid");
        }

        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->ready) {
            return fail(session, outStatus, ROF_STATUS_NOT_READY_V1, "session is not ready");
        }

        std::uint32_t frameFlags = 0U;
        if (hasStructSize(input->struct_size, offsetof(RofFrameInputV1, reserved))) {
            frameFlags = input->flags;
        }
        if ((frameFlags & ~ROF_FRAME_FLAG_ALL_V1) != 0U) {
            return fail(session, outStatus, ROF_STATUS_INPUT_ERROR_V1, "frame input contains unknown flags");
        }
        if ((frameFlags & ROF_FRAME_FLAG_AI_SCAN_V1) != 0U) {
            return fail(session, outStatus, ROF_STATUS_INPUT_ERROR_V1,
                        "AI scan mode is not supported by this plugin");
        }

        OwnedFrame ownedFrame;
        std::string error;
        if (!buildFrame(session->descriptor, *input, frameFlags, ownedFrame, error)) {
            return fail(session, outStatus, ROF_STATUS_INPUT_ERROR_V1, error);
        }

        const auto startedAt = std::chrono::steady_clock::now();
        FrameResult result = session->engine.run(ownedFrame.frame);
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startedAt).count();
        if (outMetrics != nullptr) {
            outMetrics->frame_id = input->frame_id;
            outMetrics->status_code = mapStatusCode(result.status.code);
            outMetrics->output_width = static_cast<std::uint32_t>(std::max(result.outputWidth, 0));
            outMetrics->output_height = static_cast<std::uint32_t>(std::max(result.outputHeight, 0));
            outMetrics->point_count = static_cast<std::uint64_t>(result.pointCloudVertexCount);
            outMetrics->elapsed_ms = elapsedMs;
        }
        if (!result.status.ok()) {
            return fail(session,
                        outStatus,
                        mapStatusCode(result.status.code),
                        result.status.module + ": " + result.status.message);
        }

        const std::uint32_t requestedMask = output->output_mask & session->outputMask;
        auto copyRequested = [&](std::uint32_t flag,
                                 const auto& source,
                                 std::uint32_t elementType,
                                 std::uint32_t coordinateSpace,
                                 RofMutableImageViewV1& destination) -> std::int32_t {
            if ((requestedMask & flag) == 0U) {
                destination.written_bytes = 0U;
                return ROF_STATUS_OK_V1;
            }
            return copyOutputImage(source,
                                   result.outputWidth,
                                   result.outputHeight,
                                   elementType,
                                   coordinateSpace,
                                   destination,
                                   error);
        };

        std::int32_t copyStatus = copyRequested(
            ROF_OUTPUT_DEPTH_V1,
            result.depthXyz,
            ROF_ELEMENT_FLOAT32_V1,
            ROF_COORDINATE_LEFT_CAMERA_MM_V1,
            output->depth);
        if (copyStatus == ROF_STATUS_OK_V1) {
            copyStatus = copyRequested(ROF_OUTPUT_NORMAL_V1,
                                       result.normalXyz,
                                       ROF_ELEMENT_FLOAT32_V1,
                                       ROF_COORDINATE_LEFT_CAMERA_MM_V1,
                                       output->normal);
        }
        if (copyStatus == ROF_STATUS_OK_V1) {
            copyStatus = copyRequested(ROF_OUTPUT_COLOR_V1,
                                       result.colorBgr,
                                       ROF_ELEMENT_UINT8_V1,
                                       ROF_COORDINATE_RECTIFIED_LEFT_IMAGE_V1,
                                       output->color);
        }
        if (copyStatus == ROF_STATUS_OK_V1) {
            copyStatus = copyRequested(ROF_OUTPUT_QUALITY_V1,
                                       result.qualityInfoU16,
                                       ROF_ELEMENT_UINT16_V1,
                                       ROF_COORDINATE_RECTIFIED_LEFT_IMAGE_V1,
                                       output->quality);
        }
        if (copyStatus != ROF_STATUS_OK_V1) {
            return fail(session, outStatus, copyStatus, error);
        }

        session->lastError.clear();
        session->lastSummary = "frameId=" + std::to_string(input->frame_id) +
            ", vertices=" + std::to_string(result.pointCloudVertexCount) +
            ", elapsedMs=" + std::to_string(elapsedMs);
        setStatus(outStatus, ROF_STATUS_OK_V1);
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, ex.what());
    } catch (...) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, "unknown process_frame exception");
    }
}

std::int32_t copyLastError(RofSessionHandle handle,
                           char* destination,
                           std::size_t capacity,
                           std::size_t* outRequiredSize) noexcept
{
    try {
        const auto* session = static_cast<const RofSessionImpl*>(handle);
        const std::string& source = session == nullptr ? g_lastBoundaryError : session->lastError;
        const std::size_t required = source.size() + 1U;
        if (outRequiredSize != nullptr) {
            *outRequiredSize = required;
        }
        if (destination == nullptr || capacity == 0U) {
            return ROF_STATUS_BUFFER_TOO_SMALL_V1;
        }
        const std::size_t copySize = std::min(source.size(), capacity - 1U);
        if (copySize != 0U) {
            std::memcpy(destination, source.data(), copySize);
        }
        destination[copySize] = '\0';
        return capacity >= required ? ROF_STATUS_OK_V1 : ROF_STATUS_BUFFER_TOO_SMALL_V1;
    } catch (...) {
        if (destination != nullptr && capacity != 0U) {
            destination[0] = '\0';
        }
        return ROF_STATUS_INTERNAL_ERROR_V1;
    }
}

std::int32_t drain(RofSessionHandle handle, RofStatusV1* outStatus) noexcept
{
    auto* session = static_cast<RofSessionImpl*>(handle);
    try {
        if (session == nullptr) {
            return fail(nullptr, outStatus, ROF_STATUS_INVALID_ARGUMENT_V1, "session is null");
        }
        std::lock_guard<std::mutex> lock(session->mutex);
        if (session->ready) {
            session->engine.shutdown();
            session->ready = false;
        }
        setStatus(outStatus, ROF_STATUS_OK_V1);
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, ex.what());
    } catch (...) {
        return fail(session, outStatus, ROF_STATUS_INTERNAL_ERROR_V1, "unknown drain exception");
    }
}

void destroySession(RofSessionHandle handle) noexcept
{
    try {
        std::unique_ptr<RofSessionImpl> session(static_cast<RofSessionImpl*>(handle));
        if (session != nullptr) {
            session->engine.shutdown();
            session->ready = false;
        }
    } catch (...) {
    }
}

} // namespace

extern "C" ROF_C_API std::int32_t rof_get_api(std::uint32_t requestedAbiMajor,
                                                std::uint32_t requestedAbiMinor,
                                                std::uint32_t hostTableSize,
                                                RofApiV1* outApi)
{
    try {
        if (outApi == nullptr || hostTableSize < sizeof(RofApiV1) ||
            !hasStructSize(outApi->struct_size, sizeof(RofApiV1))) {
            g_lastBoundaryError = "RofApiV1 table is null or undersized";
            return ROF_STATUS_INVALID_ARGUMENT_V1;
        }
        if (requestedAbiMajor != ROF_ABI_MAJOR_V1 || requestedAbiMinor > ROF_ABI_MINOR_V1) {
            g_lastBoundaryError = "requested ROF ABI version is not supported";
            return ROF_STATUS_ABI_MISMATCH_V1;
        }

        RofApiV1 api {};
        api.struct_size = sizeof(api);
        api.abi_major = ROF_ABI_MAJOR_V1;
        api.abi_minor = ROF_ABI_MINOR_V1;
        api.capabilities = ROF_CAPABILITY_CALLER_OWNED_OUTPUT_V1 |
            ROF_CAPABILITY_FLOAT32_INPUT_V1 |
            ROF_CAPABILITY_UINT8_INPUT_V1 |
            ROF_CAPABILITY_CAPTURE_PLAN_V1 |
            ROF_CAPABILITY_FRAME_FLAGS_V1 |
            ROF_CAPABILITY_METAL_SCAN_MODE_V1;
        constexpr char pluginId[] = "reconstructOneFrame";
        static_assert(sizeof(pluginId) <= sizeof(api.plugin_id), "plugin id must fit ABI field");
        std::memcpy(api.plugin_id, pluginId, sizeof(pluginId));
        api.create_session = &createSession;
        api.get_capture_plan = &getCapturePlan;
        api.get_camera_model = &getCameraModel;
        api.process_frame = &processFrame;
        api.copy_last_error = &copyLastError;
        api.drain = &drain;
        api.destroy_session = &destroySession;
        *outApi = api;
        g_lastBoundaryError.clear();
        return ROF_STATUS_OK_V1;
    } catch (const std::exception& ex) {
        g_lastBoundaryError = ex.what();
        return ROF_STATUS_INTERNAL_ERROR_V1;
    } catch (...) {
        g_lastBoundaryError = "unknown rof_get_api exception";
        return ROF_STATUS_INTERNAL_ERROR_V1;
    }
}
