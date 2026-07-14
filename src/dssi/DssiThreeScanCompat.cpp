#include "reconstruct_one_frame/rof_c_api.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace StructureLight {
class threeScan;
}

namespace {

constexpr const char* kDefaultConfigPath = "D:/code/reconstructOneFrame/config/reconsAlgPara.json";
constexpr const char* kDefaultCalibrationPath = "D:/Data/Calib/2607011016_mach6/calibResult.json";

std::string getenvString(const char* key)
{
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, key) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::string resolvePath(const char* environmentKey, const char* fallback)
{
    std::string value = getenvString(environmentKey);
    return value.empty() ? std::string(fallback) : value;
}

RofStatusV1 makeStatus()
{
    RofStatusV1 status {};
    status.struct_size = sizeof(status);
    return status;
}

RofMutableImageViewV1 makeOutputView(cv::Mat& image,
                                     std::uint32_t elementType,
                                     std::uint32_t coordinateSpace)
{
    RofMutableImageViewV1 view {};
    view.struct_size = sizeof(view);
    view.data = image.data;
    view.capacity_bytes = image.step[0] * static_cast<std::size_t>(image.rows);
    view.row_stride_bytes = static_cast<std::uint32_t>(image.step[0]);
    view.width = static_cast<std::uint32_t>(image.cols);
    view.height = static_cast<std::uint32_t>(image.rows);
    view.channels = static_cast<std::uint32_t>(image.channels());
    view.element_type = elementType;
    view.coordinate_space = coordinateSpace;
    return view;
}

} // namespace

namespace StructureLight {

class threeScan {
public:
    threeScan() = default;
    ~threeScan() noexcept
    {
        destroy();
    }

    threeScan(const threeScan&) = delete;
    threeScan& operator=(const threeScan&) = delete;

    void init(cv::Mat& cameraMatrix, std::string& version, unsigned int& imageCount)
    {
        destroy();
        lastError_.clear();
        lastSummary_.clear();
        version = "reconstructOneFrame init failed";
        imageCount = 0U;

        api_ = {};
        api_.struct_size = sizeof(api_);
        const int apiResult = rof_get_api(
            ROF_ABI_MAJOR_V1, ROF_ABI_MINOR_V1, sizeof(api_), &api_);
        if (apiResult != ROF_STATUS_OK_V1) {
            setFailure("rof_get_api failed: " + std::to_string(apiResult));
            return;
        }

        configPath_ = resolvePath("ROF_CONFIG_PATH", kDefaultConfigPath);
        calibrationPath_ = resolvePath("ROF_CALIB_PATH", kDefaultCalibrationPath);
        RofSessionConfigV1 config {};
        config.struct_size = sizeof(config);
        config.config_path = configPath_.data();
        config.config_path_size = configPath_.size();
        config.calibration_path = calibrationPath_.data();
        config.calibration_path_size = calibrationPath_.size();
        config.output_mask = ROF_OUTPUT_ALL_V1;

        RofStatusV1 status = makeStatus();
        const int createResult = api_.create_session(&config, &session_, &status);
        if (createResult != ROF_STATUS_OK_V1 || session_ == nullptr) {
            setFailure("create_session failed: " + readApiError(nullptr));
            return;
        }

        plan_ = {};
        plan_.struct_size = sizeof(plan_);
        status = makeStatus();
        if (api_.get_capture_plan(session_, &plan_, &status) != ROF_STATUS_OK_V1) {
            setFailure("get_capture_plan failed: " + readApiError(session_));
            destroy();
            return;
        }

        camera_ = {};
        camera_.struct_size = sizeof(camera_);
        status = makeStatus();
        if (api_.get_camera_model(session_, &camera_, &status) != ROF_STATUS_OK_V1 ||
            camera_.rows == 0U || camera_.cols == 0U || camera_.rows * camera_.cols > 16U) {
            setFailure("get_camera_model failed: " + readApiError(session_));
            destroy();
            return;
        }

        cameraMatrix.create(static_cast<int>(camera_.rows),
                            static_cast<int>(camera_.cols),
                            CV_64FC1);
        for (std::uint32_t row = 0; row < camera_.rows; ++row) {
            for (std::uint32_t col = 0; col < camera_.cols; ++col) {
                cameraMatrix.at<double>(static_cast<int>(row), static_cast<int>(col)) =
                    camera_.values[static_cast<std::size_t>(row) * camera_.cols + col];
            }
        }

        imageCount = plan_.live_image_count;
        initialized_ = imageCount != 0U;
        if (!initialized_) {
            setFailure("capture plan returned zero live images");
            destroy();
            return;
        }
        version = std::string(api_.plugin_id) + " ABI " +
            std::to_string(api_.abi_major) + "." + std::to_string(api_.abi_minor);
        lastError_.clear();
    }

    int startScan(bool isAiScan,
                  bool isMetalScan,
                  const float* leftImages,
                  const float* rightImages,
                  int& /*exposure*/,
                  cv::Mat& depthImage,
                  cv::Mat& colorImage,
                  cv::Mat& normalImage,
                  cv::Mat& qualityInfo,
                  const std::string& /*basePath*/)
    {
        if (!initialized_ || session_ == nullptr || leftImages == nullptr || rightImages == nullptr) {
            setFailure("stable ABI session is not initialized or input buffers are null");
            return -1;
        }

        const int width = static_cast<int>(plan_.input_width);
        const int height = static_cast<int>(plan_.input_height);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * sizeof(float);
        const std::size_t imageBytes = rowBytes * static_cast<std::size_t>(height);
        const std::size_t stackBytes = imageBytes * static_cast<std::size_t>(plan_.live_image_count);

        RofFrameInputV1 input {};
        input.struct_size = sizeof(input);
        input.frame_id = frameId_++;
        input.flags = (isAiScan ? ROF_FRAME_FLAG_AI_SCAN_V1 : 0U) |
            (isMetalScan ? ROF_FRAME_FLAG_METAL_SCAN_V1 : 0U);
        input.left = makeInputStack(leftImages, stackBytes, rowBytes, imageBytes);
        input.right = makeInputStack(rightImages, stackBytes, rowBytes, imageBytes);

        depthImage.create(static_cast<int>(plan_.output_height),
                          static_cast<int>(plan_.output_width),
                          CV_32FC3);
        normalImage.create(static_cast<int>(plan_.output_height),
                           static_cast<int>(plan_.output_width),
                           CV_32FC3);
        colorImage.create(static_cast<int>(plan_.output_height),
                          static_cast<int>(plan_.output_width),
                          CV_8UC3);
        qualityInfo.create(static_cast<int>(plan_.output_height),
                           static_cast<int>(plan_.output_width),
                           CV_16UC3);

        RofFrameOutputV1 output {};
        output.struct_size = sizeof(output);
        output.output_mask = ROF_OUTPUT_ALL_V1;
        output.depth = makeOutputView(
            depthImage, ROF_ELEMENT_FLOAT32_V1, ROF_COORDINATE_LEFT_CAMERA_MM_V1);
        output.normal = makeOutputView(
            normalImage, ROF_ELEMENT_FLOAT32_V1, ROF_COORDINATE_LEFT_CAMERA_MM_V1);
        output.color = makeOutputView(
            colorImage, ROF_ELEMENT_UINT8_V1, ROF_COORDINATE_RECTIFIED_LEFT_IMAGE_V1);
        output.quality = makeOutputView(
            qualityInfo, ROF_ELEMENT_UINT16_V1, ROF_COORDINATE_RECTIFIED_LEFT_IMAGE_V1);

        RofFrameMetricsV1 metrics {};
        metrics.struct_size = sizeof(metrics);
        RofStatusV1 status = makeStatus();
        const int processResult = api_.process_frame(
            session_, &input, &output, &metrics, &status);
        if (processResult != ROF_STATUS_OK_V1) {
            depthImage.release();
            normalImage.release();
            colorImage.release();
            qualityInfo.release();
            setFailure(readApiError(session_));
            lastSummary_ = "frameId=" + std::to_string(input.frame_id) +
                ", status=" + std::to_string(processResult);
            return -1;
        }

        if (output.quality.written_bytes == 0U) {
            qualityInfo.release();
        }
        std::ostringstream summary;
        summary << "frameId=" << metrics.frame_id
                << ", status=" << metrics.status_code
                << ", vertices=" << metrics.point_count
                << ", elapsedMs=" << metrics.elapsed_ms;
        lastSummary_ = summary.str();
        lastError_.clear();
        return 0;
    }

    void prepareData(const std::vector<std::string>& /*paths*/, float* /*data*/) noexcept
    {
    }

    const char* lastError() const noexcept
    {
        return lastError_.c_str();
    }

    const char* lastSummary() const noexcept
    {
        return lastSummary_.c_str();
    }

    void setFailure(std::string message) noexcept
    {
        try {
            lastError_ = std::move(message);
        } catch (...) {
        }
        initialized_ = false;
    }

    void destroy() noexcept
    {
        try {
            if (session_ != nullptr && api_.drain != nullptr) {
                RofStatusV1 status = makeStatus();
                (void)api_.drain(session_, &status);
            }
            if (session_ != nullptr && api_.destroy_session != nullptr) {
                api_.destroy_session(session_);
            }
        } catch (...) {
        }
        session_ = nullptr;
        initialized_ = false;
    }

private:
    RofImageStackViewV1 makeInputStack(const float* data,
                                       std::size_t stackBytes,
                                       std::size_t rowBytes,
                                       std::size_t imageBytes) const
    {
        RofImageStackViewV1 stack {};
        stack.struct_size = sizeof(stack);
        stack.data = data;
        stack.byte_size = stackBytes;
        stack.width = plan_.input_width;
        stack.height = plan_.input_height;
        stack.row_stride_bytes = static_cast<std::uint32_t>(rowBytes);
        stack.image_stride_bytes = static_cast<std::uint32_t>(imageBytes);
        stack.image_count = plan_.live_image_count;
        stack.element_type = ROF_ELEMENT_FLOAT32_V1;
        stack.memory_kind = ROF_MEMORY_HOST_V1;
        stack.coordinate_space = ROF_COORDINATE_CALIBRATION_INPUT_V1;
        return stack;
    }

    std::string readApiError(RofSessionHandle session) const
    {
        if (api_.copy_last_error == nullptr) {
            return "stable ABI did not provide an error reader";
        }
        std::size_t required = 0U;
        (void)api_.copy_last_error(session, nullptr, 0U, &required);
        if (required == 0U) {
            return "stable ABI returned no error text";
        }
        std::vector<char> buffer(required, '\0');
        (void)api_.copy_last_error(session, buffer.data(), buffer.size(), &required);
        return std::string(buffer.data());
    }

    RofApiV1 api_ {};
    RofSessionHandle session_ = nullptr;
    RofCapturePlanV1 plan_ {};
    RofCameraModelV1 camera_ {};
    std::string configPath_;
    std::string calibrationPath_;
    std::string lastError_;
    std::string lastSummary_;
    std::uint64_t frameId_ = 0U;
    bool initialized_ = false;
};

} // namespace StructureLight

#if defined(_WIN32)
#define ROF_LEGACY_API __declspec(dllexport)
#else
#define ROF_LEGACY_API
#endif

extern "C" {

ROF_LEGACY_API StructureLight::threeScan* threeScan_create() noexcept
{
    try {
        auto object = std::make_unique<StructureLight::threeScan>();
        return object.release();
    } catch (...) {
        return nullptr;
    }
}

ROF_LEGACY_API void threeScan_init(StructureLight::threeScan* object,
                                   cv::Mat* cameraMatrix,
                                   std::string* version,
                                   unsigned int* imageCount) noexcept
{
    if (object == nullptr || cameraMatrix == nullptr || version == nullptr || imageCount == nullptr) {
        return;
    }
    try {
        object->init(*cameraMatrix, *version, *imageCount);
    } catch (const std::exception& ex) {
        object->setFailure(ex.what());
        *version = "reconstructOneFrame init failed";
        *imageCount = 0U;
    } catch (...) {
        object->setFailure("unknown threeScan_init exception");
        *version = "reconstructOneFrame init failed";
        *imageCount = 0U;
    }
}

ROF_LEGACY_API void threeScan_prepareData(StructureLight::threeScan* object,
                                          const std::vector<std::string>* paths,
                                          float* data) noexcept
{
    if (object == nullptr || paths == nullptr) {
        return;
    }
    try {
        object->prepareData(*paths, data);
    } catch (...) {
        object->setFailure("threeScan_prepareData exception");
    }
}

ROF_LEGACY_API int threeScan_startScan(StructureLight::threeScan* object,
                                       bool isAiScan,
                                       bool isMetalScan,
                                       const float* leftImages,
                                       const float* rightImages,
                                       int* exposure,
                                       cv::Mat* depthImage,
                                       cv::Mat* colorImage,
                                       cv::Mat* normalImage,
                                       cv::Mat* qualityInfo,
                                       const std::string* basePath) noexcept
{
    if (object == nullptr || exposure == nullptr || depthImage == nullptr ||
        colorImage == nullptr || normalImage == nullptr || qualityInfo == nullptr ||
        basePath == nullptr) {
        return -1;
    }
    try {
        return object->startScan(isAiScan,
                                 isMetalScan,
                                 leftImages,
                                 rightImages,
                                 *exposure,
                                 *depthImage,
                                 *colorImage,
                                 *normalImage,
                                 *qualityInfo,
                                 *basePath);
    } catch (const std::exception& ex) {
        object->setFailure(ex.what());
        return -1;
    } catch (...) {
        object->setFailure("unknown threeScan_startScan exception");
        return -1;
    }
}

ROF_LEGACY_API const char* threeScan_getLastError(StructureLight::threeScan* object) noexcept
{
    return object == nullptr ? "threeScan object is null" : object->lastError();
}

ROF_LEGACY_API const char* threeScan_getLastSummary(StructureLight::threeScan* object) noexcept
{
    return object == nullptr ? "threeScan object is null" : object->lastSummary();
}

ROF_LEGACY_API void threeScan_destroy(StructureLight::threeScan* object) noexcept
{
    if (object != nullptr) {
        object->destroy();
    }
}

ROF_LEGACY_API void threeScan_delete(StructureLight::threeScan* object) noexcept
{
    try {
        std::unique_ptr<StructureLight::threeScan> owner(object);
    } catch (...) {
    }
}

} // extern "C"
