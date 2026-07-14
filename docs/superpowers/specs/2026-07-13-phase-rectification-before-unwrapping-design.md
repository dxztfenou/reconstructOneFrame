# Phase Rectification Before Unwrapping Design

## Problem

Res1F currently computes wrapped and absolute phase in the distorted sensor image, then bilinearly remaps the final absolute phase. Legacy rectifies every stripe intensity image before trigonometric phase computation and unwrapping. Bilinear remap does not commute with `atan2`, cross-frequency phase differences, or integer fringe-order selection.

The 07131838 evidence isolates this as a major defect:

- frame 26: current `filteredValid=2655`; pre-rectified stripe probe `filteredValid=47713`;
- frame 311: current `filteredValid=23`; pre-rectified stripe probe `filteredValid=6205`;
- frame 2: current `filteredValid=107020`; pre-rectified stripe probe `filteredValid=122458`;
- JSON and Legacy YAML `K/D/R/T/R1/R2/P1/P2/Q` matrices are element-identical.

Changing the final quality score, disabling the continuity filter, or enabling Z-only smoothing is not acceptable. Those options either hide the defect, retain fragmented raw points, or reduce valid points in the tested frames.

## Target Data Flow

```text
raw stripe images
  -> rectified bilinear sampling per phase step
  -> wrapped phase in RectifiedLeft/RectifiedRight domain
  -> absolute phase in the same domain
  -> disparity matching without a second phase remap
  -> Q reprojection/filter/normal/quality
```

Auxiliary color and clear255 sampling remain in the point-cloud stage for this change. They already use the same rectification geometry and do not affect fringe-order computation.

## Components

1. Introduce a small CUDA rectification mapping helper shared by wrapped-phase and point-cloud kernels. It owns only POD calibration coefficients and the rectified-to-raw pixel transform.
2. Extend wrapped-phase CUDA entry points with `CalibrationModel`. When full K/D/R/P data is available, each output pixel samples every phase-step image at its rectified source coordinate before computing sin/cos sums.
3. Add an explicit phase coordinate domain to wrapped and unwrapped results. Phase unwrapping propagates the domain unchanged.
4. Point-cloud reconstruction skips `remapPhaseKernel` for already-rectified phase results. Raw-domain direct test/tool calls retain the existing compatibility path.
5. The production `SingleFramePipeline` always passes its validated calibration to wrapped-phase computation.

## Error Handling

- Complete rectification calibration produces rectified-domain phase.
- Calibration without optional R/P fields remains supported for focused tests and older direct callers; phase remains raw-domain and the existing downstream remap behavior applies when possible.
- Left/right domain mismatch returns `MatchingFailed` rather than silently combining incompatible phase maps.

## Verification

- Add a synthetic CUDA regression proving wrapped phase is calculated from rectified intensity samples and that the domain propagates through unwrapping.
- Preserve direct raw-domain reconstruction tests.
- Re-run frames 2, 26, and 311 with the production pipeline; require clear improvement over the recorded baseline without disabling filtering.
- Run a representative multi-frame replay, Release CTest, DSSI provider contract test, and `git diff --check`.

## Non-Goals

- Do not change quality score formulas, continuity thresholds, matching gates, metal hole filling, or YAML input policy.
- Do not remove the raw-domain compatibility path in this change.
- Do not claim full Legacy equivalence from three probe frames; full replay remains the result-level gate.
