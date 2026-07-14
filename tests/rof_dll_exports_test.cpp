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

    constexpr const char* requiredSymbols[] = {
        "rof_get_api",
        "threeScan_create",
        "threeScan_init",
        "threeScan_startScan",
        "threeScan_getLastError",
        "threeScan_getLastSummary",
        "threeScan_destroy",
        "threeScan_delete"
    };
    for (int iteration = 0; iteration < 100; ++iteration) {
        HMODULE module = LoadLibraryExW(
            dllPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module != nullptr, "expected reconstructOneFrame DLL to load");
        for (const char* symbol : requiredSymbols) {
            require(GetProcAddress(module, symbol) != nullptr, "required DLL export is missing");
        }
        require(GetProcAddress(module, "rof_symbol_that_must_not_exist") == nullptr,
                "unknown DLL symbol lookup must fail closed");

        const auto getApi = reinterpret_cast<decltype(&rof_get_api)>(
            GetProcAddress(module, "rof_get_api"));
        RofApiV1 incompatible {};
        incompatible.struct_size = sizeof(incompatible);
        require(getApi(ROF_ABI_MAJOR_V1 + 1U, 0U, sizeof(incompatible), &incompatible) ==
                    ROF_STATUS_ABI_MISMATCH_V1,
                "dynamic ABI major mismatch must fail closed");

        RofApiV1 api {};
        api.struct_size = sizeof(api);
        require(getApi(ROF_ABI_MAJOR_V1, ROF_ABI_MINOR_V1, sizeof(api), &api) ==
                    ROF_STATUS_OK_V1,
                "dynamic ABI negotiation must succeed");
        RofSessionConfigV1 config {};
        config.struct_size = sizeof(config);
        config.config_path = configPath.data();
        config.config_path_size = configPath.size();
        config.calibration_path = calibrationPath.data();
        config.calibration_path_size = calibrationPath.size();
        config.output_mask = ROF_OUTPUT_ALL_V1;
        RofSessionHandle session = nullptr;
        RofStatusV1 status {};
        status.struct_size = sizeof(status);
        require(api.create_session(&config, &session, &status) == ROF_STATUS_OK_V1 &&
                    session != nullptr,
                "dynamic session initialization must succeed");
        api.destroy_session(session);

        using CreateFn = void* (*)();
        using DeleteFn = void (*)(void*);
        const auto create = reinterpret_cast<CreateFn>(GetProcAddress(module, "threeScan_create"));
        const auto destroyObject = reinterpret_cast<DeleteFn>(GetProcAddress(module, "threeScan_delete"));
        void* object = create();
        require(object != nullptr, "Legacy compatibility object creation must not throw");
        destroyObject(object);

        require(FreeLibrary(module) != 0, "expected reconstructOneFrame DLL to unload");
    }
    return EXIT_SUCCESS;
}
