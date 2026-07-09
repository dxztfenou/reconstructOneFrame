# 第三阶段真实数据 CUDA 包裹相位实现计划

> 日期：2026-07-09
> 项目：`D:\code\reconstructOneFrame`
> 阶段定位：在第二阶段输入、标定与预处理 dry-run 之后，读取真实 `singleStripe` BMP 数据，并建立 CUDA 包裹相位计算和诊断闸门。

## 1. 目标

第三阶段目标不是迁移 Legacy 相位展开、匹配或重建主算法，而是在现有 pipeline 中建立一个可验证、可诊断的 CUDA 包裹相位阶段：

- 从 `D:\Data\Calib\2607011016_mach6\singleStripe` 读取真实 BMP 条纹输入，并保留 manifest 内存输入用于单元测试。
- 从 `reconsAlgPara.json` 派生每频率独立相移步数计划 `phaseStepCounts`，不再把全局 `STEP` 固定写死为每个频率的步数。
- 对每个相机侧、每个频率使用 CUDA kernel 计算包裹相位和调制强度，输出阶段统计和质量状态。
- 低调制或无法形成有效相位时返回明确状态码 `PhaseQualityInsufficient`。
- 成功路径只标记 `wrappedPhaseComputed=true`，继续明确 `unwrap/matching/reconstruction` 为 `NotComputed`。

## 2. 明确不做

- 不迁移 `GPUPhaseUnwrapper`。
- 不实现相位展开 unwrap。
- 不实现左右相位匹配、视差搜索或三维重建。
- 不接入 TensorRT 或 AI 分割。
- 不输出假 depth、normal、quality、PLY、EXR 或 PNG。
- 不修改 `D:\code\dentalscanserviceinterface`。

## 3. 实施步骤

| 步骤 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 扩展公共状态码和 `FrameResult`，加入 `PhaseQualityInsufficient` 与 `wrappedPhaseComputed` |
| 2 | complete | 新增 `src/phase/WrappedPhaseComputerCuda.cu`，实现 CUDA 包裹相位计算和低调制检查 |
| 3 | complete | 在 `SingleFramePipeline` 中接入 `wrapped_phase_compute_cuda` 阶段 |
| 4 | complete | 更新 `DiagnosticSummary` 输出 `wrappedPhaseComputed` |
| 5 | complete | 增加 `phaseStepCounts` 配置、真实 singleStripe BMP loader、sample 参数和 CUDA 阶段统计 |
| 6 | complete | 执行 Release 构建、CTest、manifest smoke 和真实 `singleStripe` CUDA smoke 验证 |

## 4. 数据契约

- 输入仍使用第二阶段的 `StripeFrameGroup`、`StripeImage` 与 `ImageView`。
- 频率和相移覆盖由 `ReconsConfig::stripeRequirements` 约束；`phaseStepCounts` 为空时兼容旧全局 `STEP`，非空时每个频率独立配置。
- `singleStripe` 真实数据入口读取 `<root>\<group>\L\*.bmp` 与 `<root>\<group>\R\*.bmp`，按每频率步数顺序映射到 `StripeImage`。
- 第三阶段 CUDA 路径当前支持 packed uint8 single-channel BMP；非 packed 或非 uint8 输入会返回明确输入状态码。
- 低调制判断是第三阶段诊断闸门，不代表最终生产相位质量算法。

## 5. 验证命令

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase3_low_modulation_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_missing_step_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --dry-run-no-calib
```

## 6. 验收标准

- Release 构建通过。
- CTest 中新增 `wrapped_phase_computer_test` 通过，总测试数从 8 项增加到 9 项。
- valid manifest 返回 `Ok`，包含 `wrapped_phase_compute_cuda` 阶段，且 `wrappedPhaseComputed=true`。
- low modulation manifest 返回 `PhaseQualityInsufficient`，且不进入 downstream not computed 阶段。
- missing step manifest 仍在输入契约阶段返回 `InputPhaseStepMissing`。
- 真实 `singleStripe` group 1 返回 `Ok`，`inputImages=26`，`wrapped_phase_compute_cuda` 的 `cudaPixels=1017600`。

## 7. 后续风险

- 当前 CUDA 包裹相位只覆盖 wrapped phase 与 modulation，未处理真实光学离焦、gamma 非线性和投影序列时序问题。
- 低调制阈值是基础工程阈值，后续需要基于真实数据统计重新标定。
- 第四阶段若进入 unwrap，应先定义 CUDA 输出与 CPU 参考/Legacy 输出的数值对比口径。
