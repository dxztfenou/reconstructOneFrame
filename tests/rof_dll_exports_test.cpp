#include <windows.h>

#include "reconstruct_one_frame/rof_c_api.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char** argv)
{
    require(argc == 4, "expected reconstructOneFrame DLL, config and calibration paths");
    const std::filesystem::path dllPath = std::filesystem::absolute(argv[1]);
    const std::string configPath = std::filesystem::absolute(argv[2]).string();
    const std::string calibrationPath = std::filesystem::absolute(argv[3]).string();
    const std::filesystem::path missingDllPath = dllPath.wstring() + L".missing";
    require(LoadLibraryExW(
                missingDllPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) == nullptr,
            "missing DLL must fail closed");

    for (int iteration = 0; iteration < 100; ++iteration) {
        HMODULE module = LoadLibraryExW(
            dllPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module != nullptr, "expected reconstructOneFrame DLL to load");
        require(GetProcAddress(module, "rof_get_api") != nullptr, "rof_get_api export is missing");
        require(GetProcAddress(module, "threeScan_create") == nullptr,
                "Legacy threeScan_create must not be exported by the breaking v2 DLL");
        require(GetProcAddress(module, "rof_symbol_that_must_not_exist") == nullptr,
                "unknown DLL symbol lookup must fail closed");

        const auto getApi = reinterpret_cast<decltype(&rof_get_api)>(
            GetProcAddress(module, "rof_get_api"));
        RofApi incompatible {};
        incompatible.struct_size = sizeof(incompatible);
        require(getApi(ROF_ABI_MAJOR + 1U, 0U, sizeof(incompatible), &incompatible) ==
                    ROF_STATUS_ABI_MISMATCH,
                "dynamic ABI major mismatch must fail closed");

        RofApi api {};
        api.struct_size = sizeof(api);
        require(getApi(ROF_ABI_MAJOR, ROF_ABI_MINOR, sizeof(api), &api) ==
                    ROF_STATUS_OK,
                "dynamic ABI negotiation must succeed");
        RofRuntimeInitOptions runtimeInit {};
        runtimeInit.struct_size = sizeof(runtimeInit);
        RofContextHandle context = nullptr;
        RofStatus status {};
        status.struct_size = sizeof(status);
        require(api.init(&runtimeInit, &context, &status) == ROF_STATUS_OK &&
                    context != nullptr,
                "dynamic runtime init must succeed");

        RofConfigOptions config {};
        config.struct_size = sizeof(config);
        config.config_path = configPath.data();
        config.config_path_size = configPath.size();
        config.calibration_path = calibrationPath.data();
        config.calibration_path_size = calibrationPath.size();
        config.output_mask = ROF_OUTPUT_ALL;
        status = {};
        status.struct_size = sizeof(status);
        require(api.set_config(context, &config, &status) == ROF_STATUS_OK,
                "dynamic set_config must succeed");
        api.destroy(context);

        require(FreeLibrary(module) != 0, "expected reconstructOneFrame DLL to unload");
    }
    return EXIT_SUCCESS;
}
