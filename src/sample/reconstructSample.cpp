#include "reconstruct_one_frame/reconstructInterface.h"

#include "diagnostics/DiagnosticSummary.h"
#include "image/InputManifest.h"
#include "logging/LogSession.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace reconstruct_one_frame;

void printHelp()
{
    std::cout
        << "reconstructSample phase-6 CUDA reconstruction and quality-evaluation sample\n"
        << "Usage:\n"
        << "  reconstructSample [--config-base <path>] --config <path> [--calib <path>] [--input-manifest <path>] [--single-stripe-root <path>] [--group <n>] [--source-img-root <path>] [--frame <n>|--first <n> --last <n>] [--output <dir>] [--compare-legacy <ply>] [--dry-run] [--dry-run-no-calib]\n"
        << "  reconstructSample --help\n\n"
        << "Notes:\n"
        << "  Phase 6 writes depth_points.ply, compares coordinates, and reports frame quality.\n"
        << "  This sample does not call Legacy DLLs, TensorRT, or DSSI.\n"
        << "  --config-base supplies production defaults; --config overrides dataset-specific keys.\n"
        << "  --input-manifest builds in-memory test stripes only.\n"
        << "  --single-stripe-root reads BMP stripes from <root>\\<group>\\L and <root>\\<group>\\R.\n"
        << "  --source-img-root reads BMP stripes from <root>\\<frame>\\SourceImg\\L0.bmp/R0.bmp.\n";
}

bool readOptionValue(int argc, char** argv, int& index, std::string& value)
{
    if (index + 1 >= argc) {
        return false;
    }
    value = argv[++index];
    return true;
}

StripeFrameGroup makeSyntheticFrame()
{
    static std::vector<std::vector<unsigned char>> leftBuffers;
    static std::vector<std::vector<unsigned char>> rightBuffers;
    leftBuffers.clear();
    rightBuffers.clear();
    leftBuffers.reserve(15);
    rightBuffers.reserve(15);

    StripeFrameGroup frame;
    frame.frameId = 1;
    for (int frequency = 0; frequency < 3; ++frequency) {
        for (int step = 0; step < 5; ++step) {
            leftBuffers.emplace_back(16, static_cast<unsigned char>(32 + frequency * 10 + step));
            rightBuffers.emplace_back(16, static_cast<unsigned char>(62 + frequency * 10 + step));

            ImageView leftView;
            leftView.data = leftBuffers.back().data();
            leftView.width = 4;
            leftView.height = 4;
            leftView.channels = 1;
            leftView.strideBytes = 4;
            leftView.elementType = ImageElementType::UInt8;

            ImageView rightView = leftView;
            rightView.data = rightBuffers.back().data();

            frame.leftStripes.push_back({CameraSide::Left, frequency, step, 0, leftView});
            frame.rightStripes.push_back({CameraSide::Right, frequency, step, 0, rightView});
        }
    }
    return frame;
}

int statusToExitCode(StatusCode code)
{
    return code == StatusCode::Ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    InitOptions options;
    options.dryRun = false;
    options.dryRunNoCalib = false;
    std::string inputManifestPath;
    std::string singleStripeRoot;
    std::string sourceImgRoot;
    int singleStripeGroup = 1;
    int sourceImgFrame = 0;
    int sourceImgFirst = -1;
    int sourceImgLast = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return EXIT_SUCCESS;
        }
        if (arg == "--config") {
            if (!readOptionValue(argc, argv, i, options.configPath)) {
                std::cerr << "--config requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--config-base") {
            if (!readOptionValue(argc, argv, i, options.configBasePath)) {
                std::cerr << "--config-base requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--calib") {
            if (!readOptionValue(argc, argv, i, options.calibrationPath)) {
                std::cerr << "--calib requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--input-manifest") {
            if (!readOptionValue(argc, argv, i, inputManifestPath)) {
                std::cerr << "--input-manifest requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--output") {
            if (!readOptionValue(argc, argv, i, options.outputDirectory)) {
                std::cerr << "--output requires a directory\n";
                return EXIT_FAILURE;
            }
            options.writePly = true;
            continue;
        }
        if (arg == "--compare-legacy") {
            if (!readOptionValue(argc, argv, i, options.compareLegacyPlyPath)) {
                std::cerr << "--compare-legacy requires a PLY path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--single-stripe-root") {
            if (!readOptionValue(argc, argv, i, singleStripeRoot)) {
                std::cerr << "--single-stripe-root requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--group") {
            std::string groupText;
            if (!readOptionValue(argc, argv, i, groupText)) {
                std::cerr << "--group requires a number\n";
                return EXIT_FAILURE;
            }
            singleStripeGroup = std::stoi(groupText);
            continue;
        }
        if (arg == "--source-img-root") {
            if (!readOptionValue(argc, argv, i, sourceImgRoot)) {
                std::cerr << "--source-img-root requires a path\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--frame") {
            std::string frameText;
            if (!readOptionValue(argc, argv, i, frameText)) {
                std::cerr << "--frame requires a number\n";
                return EXIT_FAILURE;
            }
            sourceImgFrame = std::stoi(frameText);
            continue;
        }
        if (arg == "--first") {
            std::string frameText;
            if (!readOptionValue(argc, argv, i, frameText)) {
                std::cerr << "--first requires a number\n";
                return EXIT_FAILURE;
            }
            sourceImgFirst = std::stoi(frameText);
            continue;
        }
        if (arg == "--last") {
            std::string frameText;
            if (!readOptionValue(argc, argv, i, frameText)) {
                std::cerr << "--last requires a number\n";
                return EXIT_FAILURE;
            }
            sourceImgLast = std::stoi(frameText);
            continue;
        }
        if (arg == "--dry-run") {
            options.dryRun = true;
            continue;
        }
        if (arg == "--dry-run-no-calib") {
            options.dryRun = true;
            options.dryRunNoCalib = true;
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        printHelp();
        return EXIT_FAILURE;
    }

    if (options.configPath.empty()) {
        std::cerr << "--config is required unless --help is used\n";
        return EXIT_FAILURE;
    }
    const int explicitInputCount = (!inputManifestPath.empty() ? 1 : 0) +
        (!singleStripeRoot.empty() ? 1 : 0) +
        (!sourceImgRoot.empty() ? 1 : 0);
    if (explicitInputCount > 1) {
        std::cerr << "Use only one of --input-manifest, --single-stripe-root, or --source-img-root\n";
        return EXIT_FAILURE;
    }
    if (sourceImgRoot.empty() && (sourceImgFirst >= 0 || sourceImgLast >= 0)) {
        std::cerr << "--first/--last are only supported with --source-img-root\n";
        return EXIT_FAILURE;
    }

    ManifestFrame manifestFrame;
    StripeFrameGroup syntheticFrame;
    const StripeFrameGroup* frameToRun = nullptr;

    LogSession logSession;
    if (!logSession.start("reconstructSample", "logs", LogLevel::Info, 10)) {
        std::cerr << "failed to initialize log session\n";
        return EXIT_FAILURE;
    }

    if (options.writePly && !sourceImgRoot.empty()) {
        const int firstFrame = sourceImgFirst >= 0 ? sourceImgFirst : sourceImgFrame;
        const int lastFrame = sourceImgLast >= 0 ? sourceImgLast : firstFrame;
        options.outputPerFrameSubdirectory = firstFrame != lastFrame;
    }
    const bool includeColor = options.writePly || !options.compareLegacyPlyPath.empty();

    ReconstructEngine engine;
    Status status = engine.init();
    if (!status.ok()) {
        logError(std::string("init failed: ") + statusCodeName(status.code) + " " + status.message);
        std::cout << "status=" << statusCodeName(status.code) << "\n"
                  << "module=" << status.module << "\n"
                  << "message=" << status.message << "\n";
        return statusToExitCode(status.code);
    }
    status = engine.setConfig(options);
    if (!status.ok()) {
        logError(std::string("setConfig failed: ") + statusCodeName(status.code) + " " + status.message);
        std::cout << "status=" << statusCodeName(status.code) << "\n"
                  << "module=" << status.module << "\n"
                  << "message=" << status.message << "\n";
        return statusToExitCode(status.code);
    }

    if (!sourceImgRoot.empty()) {
        ReconsConfig config;
        Status frameStatus = loadReconsConfigWithBase(options.configBasePath, options.configPath, config);
        if (!frameStatus.ok()) {
            std::cout << "status=" << statusCodeName(frameStatus.code) << "\n"
                      << "module=" << frameStatus.module << "\n"
                      << "message=" << frameStatus.message << "\n";
            return statusToExitCode(frameStatus.code);
        }

        const int firstFrame = sourceImgFirst >= 0 ? sourceImgFirst : sourceImgFrame;
        const int lastFrame = sourceImgLast >= 0 ? sourceImgLast : firstFrame;
        const int step = lastFrame >= firstFrame ? 1 : -1;
        int exitCode = EXIT_SUCCESS;
        for (int frameIndex = firstFrame;; frameIndex += step) {
            const auto loadStart = std::chrono::steady_clock::now();
            frameStatus =
                loadSourceImgFrameDirectory(sourceImgRoot, config, frameIndex, manifestFrame, includeColor);
            const double loadElapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - loadStart).count();
            if (!frameStatus.ok()) {
                std::cout << "status=" << statusCodeName(frameStatus.code) << "\n"
                          << "module=" << frameStatus.module << "\n"
                          << "message=" << frameStatus.message << "\n";
                return statusToExitCode(frameStatus.code);
            }

            const auto runStart = std::chrono::steady_clock::now();
            FrameResult result = engine.calc(manifestFrame.frame);
            const double runElapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - runStart).count();
            std::cout << formatFrameResultSummary(manifestFrame.frame.frameId, result);
            std::cout << "loadElapsedMs=" << loadElapsedMs << "\n";
            std::cout << "runElapsedMs=" << runElapsedMs << "\n";
            if (!result.status.ok()) {
                exitCode = statusToExitCode(result.status.code);
            }

            if (frameIndex == lastFrame) {
                break;
            }
        }
        std::cout << "log=" << logSession.path().string() << "\n";
        return exitCode;
    }

    if (!singleStripeRoot.empty()) {
        ReconsConfig config;
        Status frameStatus = loadReconsConfigWithBase(options.configBasePath, options.configPath, config);
        if (!frameStatus.ok()) {
            std::cout << "status=" << statusCodeName(frameStatus.code) << "\n"
                      << "module=" << frameStatus.module << "\n"
                      << "message=" << frameStatus.message << "\n";
            return statusToExitCode(frameStatus.code);
        }
        frameStatus =
            loadSingleStripeBmpDirectory(singleStripeRoot, config, singleStripeGroup, manifestFrame, includeColor);
        if (!frameStatus.ok()) {
            std::cout << "status=" << statusCodeName(frameStatus.code) << "\n"
                      << "module=" << frameStatus.module << "\n"
                      << "message=" << frameStatus.message << "\n";
            return statusToExitCode(frameStatus.code);
        }
        frameToRun = &manifestFrame.frame;
    } else if (!inputManifestPath.empty()) {
        InputManifest manifest;
        Status manifestStatus = loadInputManifest(inputManifestPath, manifest);
        if (!manifestStatus.ok()) {
            std::cout << "status=" << statusCodeName(manifestStatus.code) << "\n"
                      << "module=" << manifestStatus.module << "\n"
                      << "message=" << manifestStatus.message << "\n";
            return statusToExitCode(manifestStatus.code);
        }
        manifestStatus = buildFrameFromManifest(manifest, manifestFrame);
        if (!manifestStatus.ok()) {
            std::cout << "status=" << statusCodeName(manifestStatus.code) << "\n"
                      << "module=" << manifestStatus.module << "\n"
                      << "message=" << manifestStatus.message << "\n";
            return statusToExitCode(manifestStatus.code);
        }
        frameToRun = &manifestFrame.frame;
    } else {
        syntheticFrame = makeSyntheticFrame();
        frameToRun = &syntheticFrame;
    }

    const auto runStart = std::chrono::steady_clock::now();
    FrameResult result = engine.calc(*frameToRun);
    const double runElapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - runStart).count();
    std::cout << formatFrameResultSummary(frameToRun->frameId, result);
    std::cout << "runElapsedMs=" << runElapsedMs << "\n";
    std::cout << "log=" << logSession.path().string() << "\n";

    return statusToExitCode(result.status.code);
}
