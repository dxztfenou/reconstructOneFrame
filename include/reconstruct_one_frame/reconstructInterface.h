#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(RECONSTRUCT_ONE_FRAME_STATIC)
#define ROF_API
#elif defined(_WIN32)
#if defined(RECONSTRUCT_ONE_FRAME_BUILDING_LIBRARY)
#define ROF_API __declspec(dllexport)
#else
#define ROF_API __declspec(dllimport)
#endif
#else
#define ROF_API
#endif

namespace reconstruct_one_frame {

enum class StatusCode {
    Ok = 0,
    InputMissing,
    InputEmptyImage,
    InputBlackImage,
    InputSaturatedImage,
    InputSizeMismatch,
    InputTypeUnsupported,
    InputStrideInvalid,
    InputInvalidValue,
    InputMissingLeftStripes,
    InputMissingRightStripes,
    InputFrequencyPlanMismatch,
    InputPhaseStepMissing,
    InputCameraSideMismatch,
    InputNonFinitePixel,
    InputManifestMissing,
    InputManifestParseFailed,
    ConfigMissing,
    ConfigParseFailed,
    ConfigInvalidValue,
    CalibrationMissing,
    CalibrationParseFailed,
    CalibrationInvalid,
    CalibrationFieldMissing,
    CalibrationMatrixShapeInvalid,
    CalibrationImageSizeMismatch,
    CudaInitFailed,
    CudaKernelFailed,
    DataQualityInsufficient,
    PhaseFailed,
    PhaseQualityInsufficient,
    UnwrapFailed,
    MatchingFailed,
    ReconstructionInsufficient,
    OutputWriteFailed,
    NotComputed,
    InternalError
};

struct Status {
    StatusCode code = StatusCode::Ok;
    std::string module;
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return code == StatusCode::Ok; }
};

enum class CameraSide {
    Left = 0,
    Right = 1,
    Unknown = 2
};

enum class ImageElementType {
    Unknown = 0,
    UInt8 = 1,
    UInt16 = 2,
    Float32 = 3
};

struct ImageView {
    const void* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    int strideBytes = 0;
    ImageElementType elementType = ImageElementType::Unknown;
};

struct StripeImage {
    CameraSide camera = CameraSide::Unknown;
    int frequencyIndex = -1;
    int phaseStepIndex = -1;
    int projectorIndex = -1;
    ImageView image;
};

struct StripeFrameGroup {
    std::uint64_t frameId = 0;
    std::vector<StripeImage> leftStripes;
    std::vector<StripeImage> rightStripes;
    ImageView color;
    ImageView leftColor;
    ImageView rightColor;
};

struct StageStats {
    std::string stageName;
    Status status;
    double elapsedMs = 0.0;
    std::size_t inputImageCount = 0;
    std::size_t validImageCount = 0;
    std::size_t rejectedImageCount = 0;
    std::size_t checkedPixels = 0;
    std::size_t blackPixels = 0;
    std::size_t saturatedPixels = 0;
    std::size_t nonFinitePixels = 0;
    std::size_t cudaComputedPixels = 0;
    double blackPixelRatio = 0.0;
    double saturatedPixelRatio = 0.0;
    double minPixelValue = 0.0;
    double maxPixelValue = 0.0;
    double meanPixelValue = 0.0;
    bool skipped = false;
    bool notComputed = false;
};

struct FrameResult {
    Status status;
    std::vector<StageStats> stats;
    bool depthComputed = false;
    bool normalComputed = false;
    bool qualityComputed = false;
    bool wrappedPhaseComputed = false;
    bool unwrappedPhaseComputed = false;
    std::size_t pointCloudVertexCount = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    std::vector<float> depthXyz;
    std::vector<float> normalXyz;
    std::vector<std::uint8_t> colorBgr;
    std::vector<std::uint16_t> qualityInfoU16;
    std::string outputPointCloudPath;
    std::string legacyComparisonSummary;
    std::string qualitySummary;
    std::string matchingSummary;
    std::string pointCloudSummary;
};

struct InitOptions {
    std::string configPath;
    std::string calibrationPath;
    std::string outputDirectory;
    std::string compareLegacyPlyPath;
    bool dryRun = false;
    bool dryRunNoCalib = false;
    bool writePly = false;
    bool materializeFrameOutputs = false;
    bool outputPerFrameSubdirectory = false;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
class ROF_API ReconstructEngine {
public:
    ReconstructEngine();
    ~ReconstructEngine();

    ReconstructEngine(const ReconstructEngine&) = delete;
    ReconstructEngine& operator=(const ReconstructEngine&) = delete;

    ReconstructEngine(ReconstructEngine&&) noexcept;
    ReconstructEngine& operator=(ReconstructEngine&&) noexcept;

    Status init(const InitOptions& options);
    FrameResult run(const StripeFrameGroup& frame);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Stable C ABI reservation for a later DSSI-compatible boundary.
//
// The exported threeScan_* functions in the DSSI compatibility source are a
// temporary production shim for the existing DSSI/Legacy contract. They
// intentionally mirror the old DLL ABI, including cv::Mat/std::string usage, so
// DSSI can hot-load reconstructOneFrame.dll without changing scanner runtime
// call sites. They are not the future stable ABI.
//
// Future stable C ABI functions must use opaque handles, POD structs,
// pointer+count arrays, and explicit ownership rules. Do not expose std::vector,
// std::string, cv::Mat, or C++ classes across that stable C ABI boundary.
extern "C" {
// Reserved naming shape:
// ROF_API int rof_create(void** handle);
// ROF_API int rof_init(void* handle, const RofInitOptions* options);
// ROF_API int rof_run(void* handle, const RofFrameInput* input, RofFrameOutput* output);
// ROF_API int rof_destroy(void* handle);
}

ROF_API const char* statusCodeName(StatusCode code) noexcept;

} // namespace reconstruct_one_frame
