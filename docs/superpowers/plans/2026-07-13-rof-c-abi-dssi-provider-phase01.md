# ROF C ABI and DSSI Provider Phase 0/1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace DSSI's Res1F use of the Legacy C++ DLL ABI with a versioned pure C ABI, keep the Legacy provider as rollback, and make scanner startup fail closed when reconstruction or SLAM initialization fails.

**Architecture:** `reconstructOneFrame.dll` adds one stable `rof_get_api` export backed by an opaque session and caller-owned output buffers. DSSI introduces an internal `IReconstructionProvider`; `RofPluginProvider` consumes the new ABI, while `LegacyThreeScanProvider` isolates the old `threeScan_*` contract. This plan deliberately keeps the current CUDA algorithms and DSSI worker topology unchanged so ABI/lifecycle behavior can be verified independently of numerical and scheduling refactors.

**Tech Stack:** C11-compatible ABI header, C++17/MSVC, CUDA 12 runtime, OpenCV 4.5.3, CMake/CTest, Windows `LoadLibraryExW`.

**Execution choice:** Inline execution with `superpowers:executing-plans`. The user already requested implementation and did not request subagents. No commit or push is authorized.

---

## File Structure

### reconstructOneFrame

- Create `include/reconstruct_one_frame/rof_c_api.h`: C-compatible ABI constants, POD descriptors, function table and `rof_get_api` declaration.
- Create `src/plugin/RofCApi.cpp`: opaque session, exception firewall, capture plan, camera model, frame adapter and caller-owned output writes.
- Modify `include/reconstruct_one_frame/reconstructInterface.h`: add internal C++ `EngineDescriptor` queried after successful initialization.
- Modify `src/pipeline/ReconstructEngine.cpp`: expose the already-loaded config/calibration as an immutable descriptor.
- Replace `src/dssi/DssiThreeScanCompat.cpp`: implement the Legacy shim by calling the new C API instead of owning a second engine/config/calibration path.
- Create `tests/rof_c_header_compile_test.c`: prove the public header compiles as C.
- Create `tests/rof_plugin_contract_test.cpp`: verify ABI negotiation, init failure, ready capture plan/camera model and bounded error copying.
- Create `tests/rof_dll_exports_test.cpp`: load the built DLL and resolve both stable and compatibility exports.
- Modify `CMakeLists.txt`: enable C, compile plugin source, register tests, explicitly export symbols and stop linker-wide auto-export.

### DSSI worktree

- Create `ServiceInterface/src/reconstruction_provider/ReconstructionProvider.h`: DSSI-internal provider interface, init result, frame request and factory options.
- Create `ServiceInterface/src/reconstruction_provider/RofPluginProvider.cpp`: stable ABI loader and OpenCV buffer adapter.
- Create `ServiceInterface/src/reconstruction_provider/LegacyThreeScanProvider.cpp`: old ABI adapter for rollback.
- Create `ServiceInterface/src/reconstruction_provider/ReconstructionProviderFactory.cpp`: fail-closed provider selection.
- Modify `ServiceInterface/src/ScanService.h`: own `IReconstructionProvider`, ready state and boolean `StartSlam()`.
- Modify `ServiceInterface/src/ScanService.cpp`: initialize provider transactionally, reject scanner start when not ready, use provider frame processing and propagate SLAM startup failure.
- Modify `ServiceInterface/src/ScanServiceIo.cpp`: stop LocalFrame startup when SLAM initialization fails.
- Modify `ServiceInterface/CMakeLists.txt`: build a focused provider-host static target and contract test.
- Create `ServiceInterface/tests/ReconstructionProviderContractTest.cpp`: load Res1F through the stable ABI and validate init/failure contracts.

## Task 1: Add the C-Compatible ABI Contract

**Files:**
- Create: `include/reconstruct_one_frame/rof_c_api.h`
- Create: `tests/rof_c_header_compile_test.c`
- Modify: `CMakeLists.txt:3-6,122-140`

- [x] **Step 1: Write the C header compile test**

```c
#include "reconstruct_one_frame/rof_c_api.h"

int main(void)
{
    RofApiV1 api = {0};
    api.struct_size = (uint32_t)sizeof(api);
    return api.struct_size == 0U;
}
```

- [x] **Step 2: Register the test and verify the missing header fails**

Run:

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target rof_c_header_compile_test
```

Expected: compilation fails because `rof_c_api.h` does not exist.

- [x] **Step 3: Create the pure C contract**

The header must define exact `struct_size`-prefixed types:

```c
#define ROF_ABI_MAJOR_V1 1U
#define ROF_ABI_MINOR_V1 0U
#define ROF_OUTPUT_DEPTH_V1   (1U << 0U)
#define ROF_OUTPUT_NORMAL_V1  (1U << 1U)
#define ROF_OUTPUT_COLOR_V1   (1U << 2U)
#define ROF_OUTPUT_QUALITY_V1 (1U << 3U)

typedef void* RofSessionHandle;

typedef struct RofStatusV1 {
    uint32_t struct_size;
    int32_t code;
    uint32_t severity;
    uint32_t flags;
} RofStatusV1;

typedef struct RofSessionConfigV1 {
    uint32_t struct_size;
    const char* config_path;
    size_t config_path_size;
    const char* calibration_path;
    size_t calibration_path_size;
    uint32_t output_mask;
} RofSessionConfigV1;
```

It must also define `RofCapturePlanV1`, `RofCameraModelV1`, `RofImageStackViewV1`, `RofMutableImageViewV1`, `RofFrameInputV1`, `RofFrameOutputV1`, `RofFrameMetricsV1`, function pointer typedefs and `RofApiV1`. No C++ keyword, STL type, OpenCV type or compiler-dependent enum may appear.

- [x] **Step 4: Build the C header test**

Run the command from Step 2.

Expected: target builds successfully as a C translation unit.

## Task 2: Expose an Initialized Engine Descriptor

**Files:**
- Modify: `include/reconstruct_one_frame/reconstructInterface.h:153-187`
- Modify: `src/pipeline/ReconstructEngine.cpp:11-76`
- Create: `tests/engine_descriptor_test.cpp`
- Modify: `CMakeLists.txt:130-140`

- [x] **Step 1: Write the descriptor regression test**

The test initializes with `config/reconsAlgPara.json` and `tests/data/phase2_valid_calibResult.json`, then asserts:

```cpp
EngineDescriptor descriptor;
const Status describeStatus = engine.describe(descriptor);
require(describeStatus.ok(), "initialized engine must provide a descriptor");
require(descriptor.imageWidth == 424 && descriptor.imageHeight == 400,
        "descriptor image size must match config");
require(descriptor.liveImageCount == 18U,
        "descriptor must include phase and color projector images");
require(descriptor.cameraModelRows == 4 && descriptor.cameraModelCols == 4,
        "Q calibration must be exposed explicitly");
```

It also calls `describe()` before `init()` and expects `InternalError`.

- [x] **Step 2: Verify the test fails before the method exists**

Run:

```powershell
cmake --build build --config Release --target engine_descriptor_test
```

Expected: compile failure for missing `EngineDescriptor`/`describe`.

- [x] **Step 3: Implement `EngineDescriptor` and `ReconstructEngine::describe()`**

`EngineDescriptor` contains image dimensions, live image count, public stripe requirement descriptors, color projector indices, explicit camera model shape and `std::array<double, 16>` values. `describe()` reads only `impl_->config` and `impl_->calibration`; it performs no file I/O.

- [x] **Step 4: Run descriptor and existing engine tests**

```powershell
cmake --build build --config Release --target engine_descriptor_test pipeline_smoke_test config_config_load_test calibration_model_test
ctest --test-dir build -C Release --output-on-failure -R "engine_descriptor|pipeline_smoke|config_config_load|calibration_model"
```

Expected: four tests pass.

## Task 3: Implement `rof_get_api` and Opaque Sessions

**Files:**
- Create: `src/plugin/RofCApi.cpp`
- Create: `tests/rof_plugin_contract_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write ABI negotiation and initialization tests**

The test must cover:

```cpp
RofApiV1 api {};
api.struct_size = sizeof(api);
require(rof_get_api(ROF_ABI_MAJOR_V1, ROF_ABI_MINOR_V1, sizeof(api), &api) == 0,
        "ABI v1 negotiation must succeed");
require(api.create_session && api.get_capture_plan && api.get_camera_model &&
        api.process_frame && api.copy_last_error && api.drain && api.destroy_session,
        "all v1 function pointers must be populated");
```

It must reject ABI major 2, missing config, undersized structs and null output pointers. A valid session must report `live_image_count=18`, output `424x400` and a 4x4 camera model.

- [x] **Step 2: Verify the contract test fails before implementation**

```powershell
cmake --build build --config Release --target rof_plugin_contract_test
```

Expected: link failure for missing `rof_get_api`.

- [x] **Step 3: Implement a no-throw ABI facade**

Every function uses this boundary pattern:

```cpp
template <typename Fn>
RofStatusV1 guardAbi(RofSessionImpl* session, Fn&& fn) noexcept
{
    try {
        return fn();
    } catch (const std::exception& ex) {
        if (session != nullptr) {
            session->lastError = ex.what();
        }
        return makeAbiStatus(StatusCode::InternalError);
    } catch (...) {
        if (session != nullptr) {
            session->lastError = "unknown exception";
        }
        return makeAbiStatus(StatusCode::InternalError);
    }
}
```

`RofSessionImpl` owns one `ReconstructEngine`, one `EngineDescriptor`, error/summary strings and a frame counter. `create_session` passes paths directly to `ReconstructEngine::init`; it does not use environment variables.

- [x] **Step 4: Implement frame input adaptation and bounded output writes**

`process_frame` validates image count, dimensions, strides and byte sizes. It accepts `UInt8` and `Float32` host stacks, converts only the required projector images to owned `UInt8`, builds `StripeFrameGroup`, calls the engine, and copies requested outputs row-by-row into caller-owned buffers. Capacity failure returns `InputStrideInvalid` or `InputSizeMismatch` without writing past capacity.

- [x] **Step 5: Run the ABI tests**

```powershell
cmake --build build --config Release --target rof_c_header_compile_test engine_descriptor_test rof_plugin_contract_test
ctest --test-dir build -C Release --output-on-failure -R "rof_c_header|engine_descriptor|rof_plugin_contract"
```

Expected: three tests pass.

## Task 4: Rebuild the Legacy Shim on the Stable ABI

**Files:**
- Replace: `src/dssi/DssiThreeScanCompat.cpp`
- Create: `tests/rof_dll_exports_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write a dynamic export smoke test**

The test receives `$<TARGET_FILE:reconstructOneFrame>` and verifies `LoadLibraryExW` plus these symbols:

```text
rof_get_api
threeScan_create
threeScan_init
threeScan_startScan
threeScan_getLastError
threeScan_getLastSummary
threeScan_destroy
threeScan_delete
```

It creates and deletes a Legacy object without throwing across the boundary.

- [x] **Step 2: Replace duplicate shim ownership with `RofApiV1`**

The compatibility object stores `RofApiV1`, `RofSessionHandle`, `RofCapturePlanV1` and `RofCameraModelV1`. `init()` builds `RofSessionConfigV1` from the existing environment fallback paths, then calls `create_session/get_capture_plan/get_camera_model`.

`startScan()` preallocates exact OpenCV matrices and passes their data as caller-owned output buffers. It no longer loads config/calibration twice, converts inputs itself or clones vector-backed matrices.

- [x] **Step 3: Add exception firewalls to every old export**

`threeScan_create` catches allocation failures and returns null. All other exports catch every exception; start returns `-1`, getters return static fallback text, and destroy/delete never throw.

- [x] **Step 4: Disable linker-wide symbol export**

Remove `WINDOWS_EXPORT_ALL_SYMBOLS ON`. Keep explicit `ROF_API` for the public C++ surface and explicit export macros on `rof_get_api` and `threeScan_*`.

- [x] **Step 5: Build and run all Res1F tests**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all existing 10 tests plus the new ABI/descriptor/export tests pass.

## Task 5: Add the DSSI Reconstruction Provider Host

**Files:**
- Create: `ServiceInterface/src/reconstruction_provider/ReconstructionProvider.h`
- Create: `ServiceInterface/src/reconstruction_provider/RofPluginProvider.cpp`
- Create: `ServiceInterface/src/reconstruction_provider/LegacyThreeScanProvider.cpp`
- Create: `ServiceInterface/src/reconstruction_provider/ReconstructionProviderFactory.cpp`
- Modify: `ServiceInterface/CMakeLists.txt`

- [x] **Step 1: Define the narrow DSSI-internal interface**

```cpp
struct ProviderInitResult {
    bool ready = false;
    unsigned int liveImageCount = 0;
    cv::Mat cameraModel;
    std::string providerId;
    std::string version;
    std::string error;
};

class IReconstructionProvider {
public:
    virtual ~IReconstructionProvider() = default;
    virtual ProviderInitResult initialize() = 0;
    virtual int process(const float* left, const float* right, int& exposure,
                        cv::Mat& depth, cv::Mat& color, cv::Mat& normal,
                        cv::Mat& quality) = 0;
    virtual std::string lastError() const = 0;
    virtual std::string lastSummary() const = 0;
};
```

- [x] **Step 2: Implement `RofPluginProvider`**

It loads only `rof_get_api`, negotiates v1, passes config/calibration paths in `RofSessionConfigV1`, queries plan/camera model, preallocates OpenCV outputs and calls `process_frame`. It keeps DLL directory cookies and unloads only after session destruction.

- [x] **Step 3: Implement `LegacyThreeScanProvider`**

It owns the existing `ThreeScanLoader`, calls the old init/start functions, and maps failed version/image count into `ProviderInitResult.ready=false`. This is the only DSSI component allowed to include `threeScan.hpp`.

- [x] **Step 4: Implement fail-closed factory selection**

`CreateReconstructionProvider(options)` accepts only normalized `res1f` or `legacy/legacythreescan`. Unsupported values return an error instead of silently falling back.

- [x] **Step 5: Build the focused host target**

`ServiceInterface/CMakeLists.txt` creates `ReconstructionProviderHost` from the four files, links OpenCV, and removes those sources from the `ServiceInterface` glob before linking the host target into `ServiceInterface`.

## Task 6: Integrate Provider Readiness into ScanService

**Files:**
- Modify: `ServiceInterface/src/ScanService.h:397-410,617-624`
- Modify: `ServiceInterface/src/ScanService.cpp:218-320,748-781,902-1013,1158-1215,1466-1495,1796-1835`
- Modify: `ServiceInterface/src/ScanServiceIo.cpp:447-480`

- [x] **Step 1: Replace `_scanLoad` with provider ownership**

Store `std::unique_ptr<IReconstructionProvider> _reconstructionProvider` and `_reconstructionProviderReady=false`. Constructor initialization succeeds only when `ProviderInitResult.ready`, image count, camera model and version are valid.

- [x] **Step 2: Fail scanner startup before device effects**

At the start of `StartScanner()`:

```cpp
if (!_reconstructionProviderReady || !_reconstructionProvider) {
    LogError("[Scanner] reconstruction provider is not ready");
    return false;
}
```

This check occurs before `StartSlam`, `PROJECTOR_SWITCH`, `PIC_NUM_CONTORL` and capture worker creation.

- [x] **Step 3: Route frames and diagnostics through the provider**

Replace `_scanLoad->startScan/lastError/lastSummary` with provider calls. Keep decoded output types and downstream `InputFrameMat` behavior unchanged.

- [x] **Step 4: Make `StartSlam()` return bool**

Missing/empty `algPara.json`, null SLAM interface, failed `Init` when detectable, or failed `Start()` returns false. `StartScanner()` and `StartLocalScanner()` stop before hardware/capture work when it returns false.

- [x] **Step 5: Build DSSI**

```powershell
cmake --build build --config Release --target DentalScanSystem
```

Expected: target builds with no new compiler/linker errors.

## Task 7: Add a DSSI Stable-ABI Contract Test

**Files:**
- Create: `ServiceInterface/tests/ReconstructionProviderContractTest.cpp`
- Modify: `ServiceInterface/CMakeLists.txt`

- [x] **Step 1: Write provider initialization assertions**

The test accepts DLL/config/calibration arguments, constructs `res1f` provider options and asserts ready, 18 images, non-empty 4x4 camera model and provider id `res1f`. It also supplies a missing config path and asserts not ready with non-empty error.

- [x] **Step 2: Register the test with explicit runtime paths**

Use CMake cache variable `RECONSTRUCT_ONE_FRAME_ROOT` defaulting to `D:/code/reconstructOneFrame`, and register:

```cmake
add_test(NAME reconstruction_provider_contract_test
    COMMAND reconstruction_provider_contract_test
        "${RECONSTRUCT_ONE_FRAME_ROOT}/build/Release/reconstructOneFrame.dll"
        "${RECONSTRUCT_ONE_FRAME_ROOT}/config/reconsAlgPara.json"
        "D:/Data/Calib/2607011016_mach6/calibResult.json")
```

- [x] **Step 3: Build and run the DSSI contract test**

```powershell
cmake --build build --config Release --target reconstruction_provider_contract_test
ctest --test-dir build -C Release --output-on-failure -R reconstruction_provider_contract
```

Expected: one test passes; DSSI no longer reports zero CTest tests.

## Task 8: Cross-Repository Verification and Documentation

**Files:**
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/progress.md`
- Modify: `docs/planning/task_plan.md`

- [x] **Step 1: Run Res1F Release verification**

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- [x] **Step 2: Run DSSI Release verification**

```powershell
cmake --build build --config Release --target DentalScanSystem reconstruction_provider_contract_test
ctest --test-dir build -C Release --output-on-failure -R reconstruction_provider_contract
```

- [x] **Step 3: Verify DLL exports and failure behavior**

```powershell
dumpbin /exports D:\code\reconstructOneFrame\build\Release\reconstructOneFrame.dll
```

Expected: explicit stable `rof_get_api` and required temporary `threeScan_*` exports are present.

- [x] **Step 4: Audit changes without committing**

```powershell
git diff --check
git status --short
git -C D:\code\_worktrees\dssi-adapt-res1f diff --check
git -C D:\code\_worktrees\dssi-adapt-res1f status --short
```

Expected: no whitespace errors; generated `output/` and `scripts/__pycache__/` remain excluded from implementation changes.

- [x] **Step 5: Record evidence and remaining phases**

Update planning files with exact test counts, build results, export list, known warnings and remaining Phase 2-4 work. Do not claim device-resident GPU or DSSI thread-pipeline completion in this phase.
