#include "reconstruct_one_frame/reconstructInterface.h"

#include "calibration_model/CalibrationModel.h"
#include "config/ReconsConfig.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace StructureLight {
class threeScan;
}

namespace {

using reconstruct_one_frame::CameraSide;
using reconstruct_one_frame::FrameResult;
using reconstruct_one_frame::ImageElementType;
using reconstruct_one_frame::ImageView;
using reconstruct_one_frame::InitOptions;
using reconstruct_one_frame::ReconsConfig;
using reconstruct_one_frame::ReconstructEngine;
using reconstruct_one_frame::Status;
using reconstruct_one_frame::StripeFrameGroup;
using reconstruct_one_frame::StripeImage;
using reconstruct_one_frame::StripeRequirement;

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
    if (!value.empty()) {
        return value;
    }
    return fallback;
}

std::uint8_t floatToByte(float value)
{
    if (!std::isfinite(value)) {
        return 0U;
    }
    const float clamped = std::clamp(value, 0.0F, 255.0F);
    return static_cast<std::uint8_t>(std::lround(clamped));
}

std::uint32_t requiredLiveImageCount(const ReconsConfig& config)
{
    int maxProjectorIndex = 0;
    for (const StripeRequirement& requirement : config.stripeRequirements) {
        maxProjectorIndex = std::max(maxProjectorIndex,
                                     requirement.firstProjectorIndex + requirement.requiredPhaseSteps - 1);
    }
    if (config.colorTextureEnabled) {
        for (int projectorIndex : config.colorTextureProjectorIndices) {
            maxProjectorIndex = std::max(maxProjectorIndex, projectorIndex);
        }
    }
    return static_cast<std::uint32_t>(std::max(maxProjectorIndex, 0));
}

cv::Mat matrixFromRowMajor(const std::vector<double>& values, int rows, int cols)
{
    if (values.size() != static_cast<std::size_t>(rows * cols)) {
        return {};
    }
    cv::Mat mat(rows, cols, CV_64FC1);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            mat.at<double>(row, col) = values[static_cast<std::size_t>(row * cols + col)];
        }
    }
    return mat;
}

} // namespace

namespace StructureLight {

class threeScan {
public:
    threeScan() = default;

    void init(cv::Mat& cameraMatrix, std::string& version, unsigned int& imageCount)
    {
        configPath_ = resolvePath("ROF_CONFIG_PATH", kDefaultConfigPath);
        calibrationPath_ = resolvePath("ROF_CALIB_PATH", kDefaultCalibrationPath);

        Status status = reconstruct_one_frame::loadReconsConfig(configPath_, config_);
        if (!status.ok()) {
            initialized_ = false;
            lastError_ = "loadReconsConfig failed: " + status.message;
            version = "reconstructOneFrame init failed";
            imageCount = 0U;
            return;
        }

        imageCount_ = requiredLiveImageCount(config_);
        imageCount = imageCount_;

        status = reconstruct_one_frame::loadCalibrationResultJson(calibrationPath_, calibration_);
        if (!status.ok()) {
            initialized_ = false;
            lastError_ = "loadCalibrationResultJson failed: " + status.message;
            version = "reconstructOneFrame init failed";
            return;
        }

        cv::Mat qMatrix = matrixFromRowMajor(calibration_.qMatrix, 4, 4);
        if (!qMatrix.empty()) {
            cameraMatrix = qMatrix;
        } else {
            cv::Mat leftK = matrixFromRowMajor(calibration_.leftIntrinsics, 3, 3);
            cameraMatrix = leftK.empty() ? cv::Mat::eye(3, 3, CV_64FC1) : leftK;
        }

        InitOptions options;
        options.configPath = configPath_;
        options.calibrationPath = calibrationPath_;
        options.materializeFrameOutputs = true;
        options.writePly = false;
        status = engine_.init(options);
        if (!status.ok()) {
            initialized_ = false;
            lastError_ = "ReconstructEngine init failed: " + status.message;
            version = "reconstructOneFrame init failed";
            return;
        }

        initialized_ = true;
        lastError_.clear();
        version = "reconstructOneFrame DSSI compat";
    }

    int startScan(bool /*isAiScan*/,
                  bool /*isMetalScan*/,
                  const float* leftImages,
                  const float* rightImages,
                  int& /*exposure*/,
                  cv::Mat& depthImage,
                  cv::Mat& colorImage,
                  cv::Mat& normalImage,
                  cv::Mat& qualityInfo,
                  const std::string& /*basePath*/)
    {
        if (!initialized_ || leftImages == nullptr || rightImages == nullptr) {
            lastError_ = "engine is not initialized or live image buffers are null";
            lastSummary_ = lastError_;
            return -1;
        }

        LiveFrameBuffers buffers;
        Status status = buildFrame(leftImages, rightImages, buffers);
        if (!status.ok()) {
            lastError_ = status.message;
            lastSummary_ = "frameId=" + std::to_string(frameId_) + ", buildFrame failed: " + lastError_;
            return -1;
        }

        FrameResult result = engine_.run(buffers.frame);
        std::ostringstream summary;
        summary << "frameId=" << buffers.frame.frameId
                << ", status=" << static_cast<int>(result.status.code)
                << ", vertices=" << result.pointCloudVertexCount;
        if (!result.matchingSummary.empty()) {
            summary << ", " << result.matchingSummary;
        }
        if (!result.pointCloudSummary.empty()) {
            summary << ", " << result.pointCloudSummary;
        }
        lastSummary_ = summary.str();
        if (!result.status.ok()) {
            lastError_ = result.status.module + ": " + result.status.message;
            return -1;
        }

        const std::size_t expectedValues = static_cast<std::size_t>(result.outputWidth) *
            static_cast<std::size_t>(result.outputHeight) * 3U;
        if (result.outputWidth <= 0 || result.outputHeight <= 0 ||
            result.depthXyz.size() != expectedValues ||
            result.normalXyz.size() != expectedValues ||
            result.colorBgr.size() != expectedValues) {
            lastError_ = "materialized frame outputs are incomplete";
            lastSummary_ += ", " + lastError_;
            return -1;
        }

        depthImage = cv::Mat(result.outputHeight,
                             result.outputWidth,
                             CV_32FC3,
                             const_cast<float*>(result.depthXyz.data())).clone();
        normalImage = cv::Mat(result.outputHeight,
                              result.outputWidth,
                              CV_32FC3,
                              const_cast<float*>(result.normalXyz.data())).clone();
        colorImage = cv::Mat(result.outputHeight,
                             result.outputWidth,
                             CV_8UC3,
                             const_cast<std::uint8_t*>(result.colorBgr.data())).clone();
        if (!result.qualityInfoU16.empty()) {
            qualityInfo = cv::Mat(result.outputHeight,
                                  result.outputWidth,
                                  CV_16UC3,
                                  const_cast<std::uint16_t*>(result.qualityInfoU16.data())).clone();
        } else {
            qualityInfo.release();
        }

        lastError_.clear();
        return 0;
    }

    const char* lastError() const noexcept
    {
        return lastError_.c_str();
    }

    const char* lastSummary() const noexcept
    {
        return lastSummary_.c_str();
    }

    void destroy()
    {
        engine_.shutdown();
        initialized_ = false;
    }

    void prepareData(const std::vector<std::string>& /*paths*/, float* /*data*/)
    {
        // DSSI live scan passes camera buffers directly to startScan(). The
        // Legacy file-loading helper remains exported only for ABI compatibility.
    }

private:
    struct LiveFrameBuffers {
        StripeFrameGroup frame;
        std::vector<std::vector<std::uint8_t>> left;
        std::vector<std::vector<std::uint8_t>> right;
        std::vector<std::uint8_t> colorBgr;
    };

    Status appendStripe(const float* images,
                        int imageIndex,
                        CameraSide side,
                        int frequencyIndex,
                        int phaseStepIndex,
                        int projectorIndex,
                        std::vector<std::vector<std::uint8_t>>& storage,
                        std::vector<StripeImage>& target) const
    {
        if (imageIndex < 0 || static_cast<std::uint32_t>(imageIndex) >= imageCount_) {
            return {reconstruct_one_frame::StatusCode::InputPhaseStepMissing,
                    "DssiThreeScanCompat",
                    "projector index exceeds live image count"};
        }
        const std::size_t pixelCount =
            static_cast<std::size_t>(config_.imageWidth) * static_cast<std::size_t>(config_.imageHeight);
        storage.emplace_back(pixelCount);
        const float* source = images + static_cast<std::size_t>(imageIndex) * pixelCount;
        std::vector<std::uint8_t>& pixels = storage.back();
        for (std::size_t idx = 0; idx < pixelCount; ++idx) {
            pixels[idx] = floatToByte(source[idx]);
        }

        ImageView view;
        view.data = pixels.data();
        view.width = config_.imageWidth;
        view.height = config_.imageHeight;
        view.channels = 1;
        view.strideBytes = config_.imageWidth;
        view.elementType = ImageElementType::UInt8;
        target.push_back({side, frequencyIndex, phaseStepIndex, projectorIndex, view});
        return {};
    }

    Status appendColor(const float* leftImages, LiveFrameBuffers& output) const
    {
        if (!config_.colorTextureEnabled) {
            return {};
        }
        if (config_.colorTextureProjectorIndices.size() != 3U) {
            return {reconstruct_one_frame::StatusCode::ConfigInvalidValue,
                    "DssiThreeScanCompat",
                    "colorTextureProjectorIndices must contain B, G, and R"};
        }

        const std::size_t pixelCount =
            static_cast<std::size_t>(config_.imageWidth) * static_cast<std::size_t>(config_.imageHeight);
        output.colorBgr.assign(pixelCount * 3U, 0U);
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const int projectorIndex = config_.colorTextureProjectorIndices[channel];
            const int imageIndex = projectorIndex - 1;
            if (imageIndex < 0 || static_cast<std::uint32_t>(imageIndex) >= imageCount_) {
                return {reconstruct_one_frame::StatusCode::InputPhaseStepMissing,
                        "DssiThreeScanCompat",
                        "color projector index exceeds live image count"};
            }
            const float* source = leftImages + static_cast<std::size_t>(imageIndex) * pixelCount;
            for (std::size_t idx = 0; idx < pixelCount; ++idx) {
                output.colorBgr[idx * 3U + channel] = floatToByte(source[idx]);
            }
        }

        ImageView view;
        view.data = output.colorBgr.data();
        view.width = config_.imageWidth;
        view.height = config_.imageHeight;
        view.channels = 3;
        view.strideBytes = config_.imageWidth * 3;
        view.elementType = ImageElementType::UInt8;
        output.frame.leftColor = view;
        output.frame.color = view;
        return {};
    }

    Status buildFrame(const float* leftImages, const float* rightImages, LiveFrameBuffers& output) const
    {
        output.frame.frameId = frameId_++;
        output.left.reserve(imageCount_);
        output.right.reserve(imageCount_);

        for (const StripeRequirement& requirement : config_.stripeRequirements) {
            for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
                const int projectorIndex = requirement.firstProjectorIndex + step;
                const int imageIndex = projectorIndex - 1;
                Status status = appendStripe(leftImages,
                                             imageIndex,
                                             CameraSide::Left,
                                             requirement.frequencyIndex,
                                             step,
                                             projectorIndex,
                                             output.left,
                                             output.frame.leftStripes);
                if (!status.ok()) {
                    return status;
                }
                status = appendStripe(rightImages,
                                      imageIndex,
                                      CameraSide::Right,
                                      requirement.frequencyIndex,
                                      step,
                                      projectorIndex,
                                      output.right,
                                      output.frame.rightStripes);
                if (!status.ok()) {
                    return status;
                }
            }
        }
        return appendColor(leftImages, output);
    }

    ReconstructEngine engine_;
    ReconsConfig config_;
    reconstruct_one_frame::CalibrationModel calibration_;
    std::string configPath_;
    std::string calibrationPath_;
    std::string lastError_;
    std::string lastSummary_;
    std::uint32_t imageCount_ = 0;
    mutable std::uint64_t frameId_ = 0;
    bool initialized_ = false;
};

} // namespace StructureLight

extern "C" {

__declspec(dllexport) StructureLight::threeScan* threeScan_create()
{
    return new StructureLight::threeScan();
}

__declspec(dllexport) void threeScan_init(StructureLight::threeScan* obj,
                                          cv::Mat* cameraMatrix,
                                          std::string* version,
                                          unsigned int* imageCount)
{
    if (obj == nullptr || cameraMatrix == nullptr || version == nullptr || imageCount == nullptr) {
        return;
    }
    obj->init(*cameraMatrix, *version, *imageCount);
}

__declspec(dllexport) void threeScan_prepareData(StructureLight::threeScan* obj,
                                                 const std::vector<std::string>* paths,
                                                 float* data)
{
    if (obj != nullptr && paths != nullptr) {
        obj->prepareData(*paths, data);
    }
}

__declspec(dllexport) int threeScan_startScan(StructureLight::threeScan* obj,
                                              bool isAiScan,
                                              bool isMetalScan,
                                              const float* leftImages,
                                              const float* rightImages,
                                              int* exposure,
                                              cv::Mat* depthImage,
                                              cv::Mat* colorImage,
                                              cv::Mat* normalImage,
                                              cv::Mat* qualityInfo,
                                              const std::string* basePath)
{
    if (obj == nullptr || exposure == nullptr || depthImage == nullptr ||
        colorImage == nullptr || normalImage == nullptr || qualityInfo == nullptr ||
        basePath == nullptr) {
        return -1;
    }
    return obj->startScan(isAiScan,
                          isMetalScan,
                          leftImages,
                          rightImages,
                          *exposure,
                          *depthImage,
                          *colorImage,
                          *normalImage,
                          *qualityInfo,
                          *basePath);
}

__declspec(dllexport) const char* threeScan_getLastError(StructureLight::threeScan* obj)
{
    return obj == nullptr ? "threeScan object is null" : obj->lastError();
}

__declspec(dllexport) const char* threeScan_getLastSummary(StructureLight::threeScan* obj)
{
    return obj == nullptr ? "threeScan object is null" : obj->lastSummary();
}

__declspec(dllexport) void threeScan_destroy(StructureLight::threeScan* obj)
{
    if (obj != nullptr) {
        obj->destroy();
    }
}

__declspec(dllexport) void threeScan_delete(StructureLight::threeScan* obj)
{
    delete obj;
}

} // extern "C"
