# 第四阶段真实数据 CUDA 相位展开实施计划

> 日期：2026-07-09
> 项目：`D:\code\reconstructOneFrame`
> 阶段定位：在第三阶段真实 BMP 输入与 CUDA 包裹相位之后，迁移 Legacy 两级 CUDA 相位展开公式，形成真实数据可运行的 CUDA absolute phase 诊断阶段。

## 1. 目标

第四阶段目标是把 pipeline 从 `wrapped_phase_compute_cuda` 推进到 `phase_unwrap_cuda`：

- 继续使用真实 `D:\Data\Calib\2607011016_mach6\singleStripe` BMP 输入验证。
- 保留 `phaseStepCounts=[3,5,5]` 的每频率独立相移步数契约，不退回 Legacy 全局 `STEP=5` 假设。
- 参考 Legacy `GPUPhaseUnwrapper::phaseUnwrapping()` 的两级公式：
  - `PH12 = phi1 - phi0`
  - `PH23 = phi2 - phi1`
  - `PH123 = PH23 - PH12`
  - `abs23 = unwrap(PH23, PH123, freq23)`
  - `absolute = unwrap(phi2, abs23, freqSeries[2] / freq23)`
- 新增 CUDA kernel 完成 phase difference、absolute phase round unwrap 和可选 final median filter。
- 输出 `unwrappedPhaseComputed=true`、`phase_unwrap_cuda` 阶段统计和 CUDA pixel 计数。

## 2. 明确不做

- 不实现左右匹配、disparity、三维点云、法向或质量图。
- 不输出 PLY/EXR/PNG。
- 不迁移 Legacy remap、金属扫描过曝/过暗统计、direct three-frequency LS 或 debug map。
- 不接入 TensorRT 或 AI 分割。
- 不修改 `D:\code\dentalscanserviceinterface`。

## 3. 实施步骤

| 步骤 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 调研 Legacy `calcPhaseUnwrap.cu` 的 `computePhaseDifferences()`、`getAbsPhaseGPU()`、`phaseUnwrapping()` |
| 2 | complete | 扩展 `ReconsConfig`，读取 `freq23`、`Bmin`、`winSizeMedian` 和 unwrap residual/median 配置 |
| 3 | complete | 新增 `src/phase/PhaseUnwrapper.h` 和 `src/phase/PhaseUnwrapperCuda.cu` |
| 4 | complete | 将 `phase_unwrap_cuda` 接入 `SingleFramePipeline`，并更新 `FrameResult::unwrappedPhaseComputed` |
| 5 | complete | 更新 sample help、diagnostic summary、CTest 和新增 `phase_unwrapper_test` |
| 6 | complete | 执行 Release 构建、10 项 CTest、manifest smoke、低调制负例和真实 `singleStripe` CUDA smoke |

## 4. 数据契约

- 输入为第三阶段 `WrappedPhaseResult`，要求左右相机均有 3 个频率的 wrapped phase。
- 频率顺序沿用 `freqSeries`，最终高频使用 `freqSeries[2]`。
- `freq23` 从 `reconsAlgPara.json` 读取，当前为 `7`。
- 每频率相移图数量仍由 `phaseStepCounts` 决定；真实数据 group 1 当前读取 26 张 BMP：
  - frequency 0：3 步，projector 1..3
  - frequency 1：5 步，projector 4..8
  - frequency 2：5 步，projector 9..13
  - 左右相机合计 26 张
- 输出为左右相机各一张 absolute phase float vector；无效值使用 `NaN`。

## 5. 验证命令

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\reconstructSample.exe --help
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase3_low_modulation_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --dry-run-no-calib
```

## 6. 验收标准

- Release 构建通过。
- CTest 从 9 项增加到 10 项，新增 `phase_unwrapper_test` 通过。
- valid manifest 返回 `Ok`，阶段包含 `phase_unwrap_cuda`，`unwrappedPhaseComputed=true`。
- low modulation manifest 仍在 `wrapped_phase_compute_cuda` 返回 `PhaseQualityInsufficient`，不进入 unwrap。
- 真实 `singleStripe` group 1 返回 `Ok`，包含：
  - `wrapped_phase_compute_cuda`：`cudaPixels=1017600`
  - `phase_unwrap_cuda`：`cudaPixels=339200`
  - `wrappedPhaseComputed=true`
  - `unwrappedPhaseComputed=true`

## 7. 风险与后续

- 当前迁移的是 Legacy two-stage unwrap 核心 CUDA 数学公式，不包含 Legacy remap 和标定几何映射。
- `phaseUnwrapResidualGateEnabled=false` 沿用当前配置；打开 residual gate 后需要重新统计真实数据有效率。
- CUDA 内存仍按阶段临时分配，后续实时路径应改成 pipeline 初始化时复用 buffer。
- 第五阶段应进入左右匹配/视差或先补 absolute phase 数值诊断输出与 Legacy 对比脚本，不能直接声称完成三维重建。
