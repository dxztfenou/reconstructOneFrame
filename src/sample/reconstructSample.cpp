#include "reconstruct_one_frame/reconstructInterface.h"

#include "logging/LogSession.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace reconstruct_one_frame;

void printHelp()
{
    std::cout
        << "reconstructSample phase-1 dry-run sample\n"
        << "Usage:\n"
        << "  reconstructSample --config <path> [--calib <path>] [--dry-run] [--dry-run-no-calib]\n"
        << "  reconstructSample --help\n\n"
        << "Notes:\n"
        << "  This sample does not read real scan directories, call Legacy DLLs, or write PLY/EXR/PNG.\n";
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
    static std::vector<unsigned char> left(16, 32);
    static std::vector<unsigned char> right(16, 48);

    ImageView leftView;
    leftView.data = left.data();
    leftView.width = 4;
    leftView.height = 4;
    leftView.channels = 1;
    leftView.strideBytes = 4;
    leftView.elementType = ImageElementType::UInt8;

    ImageView rightView = leftView;
    rightView.data = right.data();

    StripeFrameGroup frame;
    frame.frameId = 1;
    frame.leftStripes.push_back({CameraSide::Left, 0, 0, 0, leftView});
    frame.rightStripes.push_back({CameraSide::Right, 0, 0, 0, rightView});
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
        if (arg == "--calib") {
            if (!readOptionValue(argc, argv, i, options.calibrationPath)) {
                std::cerr << "--calib requires a path\n";
                return EXIT_FAILURE;
            }
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

    LogSession logSession;
    if (!logSession.start("reconstructSample", "logs", LogLevel::Info, 10)) {
        std::cerr << "failed to initialize log session\n";
        return EXIT_FAILURE;
    }

    ReconstructEngine engine;
    Status status = engine.init(options);
    if (!status.ok()) {
        logError(std::string("init failed: ") + statusCodeName(status.code) + " " + status.message);
        std::cout << "status=" << statusCodeName(status.code) << "\n"
                  << "module=" << status.module << "\n"
                  << "message=" << status.message << "\n";
        return statusToExitCode(status.code);
    }

    FrameResult result = engine.run(makeSyntheticFrame());
    std::cout << "status=" << statusCodeName(result.status.code) << "\n";
    for (const StageStats& stat : result.stats) {
        std::cout << "stage=" << stat.stageName
                  << ", status=" << statusCodeName(stat.status.code)
                  << ", validImages=" << stat.validImageCount
                  << ", rejectedImages=" << stat.rejectedImageCount
                  << ", notComputed=" << (stat.notComputed ? "true" : "false")
                  << "\n";
    }
    std::cout << "depthComputed=" << (result.depthComputed ? "true" : "false") << "\n"
              << "normalComputed=" << (result.normalComputed ? "true" : "false") << "\n"
              << "qualityComputed=" << (result.qualityComputed ? "true" : "false") << "\n"
              << "log=" << logSession.path().string() << "\n";

    return statusToExitCode(result.status.code);
}
