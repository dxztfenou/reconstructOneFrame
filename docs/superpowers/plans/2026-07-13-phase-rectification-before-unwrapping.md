# Phase Rectification Before Unwrapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compute wrapped/absolute phase from rectified stripe intensities and prevent the point-cloud stage from remapping an already-rectified phase map.

**Architecture:** A shared CUDA rectification helper provides one rectified-to-raw transform to both wrapped-phase and auxiliary image kernels. Phase results carry an explicit coordinate domain; production uses rectified-domain phase, while direct legacy-style test calls retain the raw-domain compatibility path.

**Tech Stack:** C++20, CUDA, CMake/CTest, MSVC Release, OpenCV-based diagnostic replay.

---

### Task 1: Add the phase coordinate-domain contract

**Files:**
- Modify: `src/phase/WrappedPhaseComputer.h`
- Modify: `src/phase/PhaseUnwrapper.h`
- Modify: `src/phase/PhaseUnwrapperCuda.cu`
- Test: `tests/phase_unwrapper_test.cpp`

- [x] Add `enum class PhaseCoordinateDomain { Sensor, Rectified };` and a `coordinateDomain` field to `WrappedPhaseResult` and `UnwrappedPhaseResult`.
- [x] In `computeUnwrappedPhaseCuda`, assign `result.coordinateDomain = wrappedPhase.coordinateDomain` before processing either camera.
- [x] Extend `phase_unwrapper_test` to set `wrapped.coordinateDomain = PhaseCoordinateDomain::Rectified` and require the unwrapped result to preserve it.
- [x] Build and run `phase_unwrapper_test`; before propagation it must fail, then pass after the minimal implementation.

Run:

```powershell
cmake --build build --config Release --target phase_unwrapper_test
ctest --test-dir build -C Release -R '^phase_unwrapper_test$' --output-on-failure
```

Expected: `1/1` passed after implementation.

### Task 2: Share the CUDA rectification transform

**Files:**
- Create: `src/calibration_model/CudaRectification.cuh`
- Modify: `src/reconstruction/PointCloudReconstructorCuda.cu`
- Test: `tests/point_cloud_reconstructor_test.cpp`

- [x] Move the POD remap coefficients, full-calibration predicate, 3x3 inverse, host builder, and `rectifiedToRawPixel` device function from `PointCloudReconstructorCuda.cu` into `CudaRectification.cuh`.
- [x] Keep the same float coefficient layout and Brown-Conrady 4/5/8-coefficient behavior; do not alter color/clear255 interpolation.
- [x] Replace local calls with the shared helper and run `point_cloud_reconstructor_test` to prove behavior remains unchanged.

Run:

```powershell
cmake --build build --config Release --target point_cloud_reconstructor_test
ctest --test-dir build -C Release -R '^point_cloud_reconstructor_test$' --output-on-failure
```

Expected: existing point-cloud assertions pass without baseline changes.

### Task 3: Rectify each phase-step sample inside wrapped-phase CUDA

**Files:**
- Modify: `src/phase/WrappedPhaseComputer.h`
- Modify: `src/phase/WrappedPhaseComputerCuda.cu`
- Test: `tests/wrapped_phase_computer_test.cpp`

- [x] Add overloads of `computeWrappedPhaseCuda` that accept `const CalibrationModel&`; keep current overloads as sensor-domain compatibility calls.
- [x] Extend `computeWrappedPhaseKernel` with `CudaRemapCalibration` plus an enable flag. For a rectified output pixel, calculate one source coordinate and bilinearly sample every required phase-step image before accumulating sin/cos values.
- [x] Mark the result `Rectified` only when complete left/right K/D/R/P calibration is available; otherwise run the existing indexed sampling and mark `Sensor`.
- [x] Add a 4x4 synthetic CUDA test with an identity rotation and a one-pixel principal-point shift. Require rectified phase/modulation at `(x,y)` to equal sensor-domain phase/modulation at `(x+1,y)`, and require the result domain to be `Rectified`.
- [x] Run the targeted wrapped-phase test red/green cycle.

Run:

```powershell
cmake --build build --config Release --target wrapped_phase_computer_test
ctest --test-dir build -C Release -R '^wrapped_phase_computer_test$' --output-on-failure
```

Expected: the new shift assertion fails before rectified sampling and passes afterward.

### Task 4: Wire the production pipeline and suppress double remap

**Files:**
- Modify: `src/pipeline/SingleFramePipeline.cpp`
- Modify: `src/reconstruction/PointCloudReconstructorCuda.cu`
- Test: `tests/pipeline_smoke_test.cpp`
- Test: `tests/point_cloud_reconstructor_test.cpp`

- [x] Pass `calibration_` into wrapped-phase CUDA from `SingleFramePipeline`.
- [x] In point-cloud reconstruction, remap left/right phase only for `PhaseCoordinateDomain::Sensor`; directly match `Rectified` phase.
- [x] Add a point-cloud assertion that a rectified-domain synthetic phase is not shifted a second time, while the existing sensor-domain path still remaps.
- [x] Run pipeline and point-cloud targeted tests.

Run:

```powershell
cmake --build build --config Release --target pipeline_smoke_test point_cloud_reconstructor_test
ctest --test-dir build -C Release -R '^(pipeline_smoke_test|point_cloud_reconstructor_test)$' --output-on-failure
```

Expected: `2/2` passed.

### Task 5: Validate real frames and the complete contract

**Files:**
- Modify: `docs/benchmarks/2026-07-13-07131838-res1f-vs-latest-legacy-quality-benchmark.md`
- Modify: `docs/design/2026-07-13-res1f-dssi-architecture-assessment-and-refactoring.md`
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/progress.md`
- Modify: `docs/planning/task_plan.md`

- [x] Rebuild Release and run frames 2, 26, and 311 using production JSON calibration/config with filtering enabled.
- [x] Require frame 2 not to regress below `107020`; require frame 26 to improve materially over `2655`; require frame 311 to improve materially over `23`.
- [x] Run a representative contiguous replay first. If status/point counts are stable, run the full `0..478` Res1F replay; treat process timing as contaminated while background SLAM remains active.
- [x] Run all 15 Res1F CTests, build DSSI provider/replay/system targets, run DSSI `1/1`, and run both repositories' `git diff --check`.
- [x] Update documents with exact result numbers and state whether a major defect remains.

Run:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build D:\code\_worktrees\dssi-adapt-res1f\build --config Release --target reconstruction_provider_contract_test reconstruction_provider_replay DentalScanSystem
ctest --test-dir D:\code\_worktrees\dssi-adapt-res1f\build -C Release --output-on-failure
git diff --check
git -C D:\code\_worktrees\dssi-adapt-res1f diff --check
```

Expected: Res1F `15/15`, DSSI `1/1`, and both diff checks exit 0.

No commit or push is part of this plan; the user requested preservation of the current uncommitted workspaces.
