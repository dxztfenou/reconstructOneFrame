# Res1F / Legacy 相位匹配约束追齐方案

## 背景结论

量块 group 1 的诊断显示，Res1F 的 RMS 差距不是 final point-cloud filter 的根因。Res1F 在 final filter 前产生 `100379` 个 raw/reprojected 候选，而 Legacy 对应 `candidate_matches=89811`，多出的约 `10.6k` 个候选包含明显离面层状点。Res1F raw P95 约 `11479um`，Legacy raw P95 约 `55.8um`，问题已经前移到 phase/matching 候选生成阶段。

关于用户提到的 `0.4` 相位匹配阈值：

- Legacy 源码里 `EpipolarLsParams::phase_threshold` 的历史默认值是 `0.4f`。
- 当前 Legacy 生产配置 `config/reconsAlgPara.json` 实际使用 `phaseDiffThreshold=0.2`。
- Res1F 当前生产配置也是 `phaseDiffThreshold=0.2`。

因此当前差距不能归因于 Res1F 相位差阈值比 Legacy 更宽。真实问题是：`min_cost < phaseDiffThreshold` 只能证明存在相位接近候选，不能证明候选唯一、可靠、空间连续或光学质量足够。

## 已学到的 Legacy 约束

Res1F 已经实现或接入：

- `phaseDiffThreshold`
- `disparityWindowEnabled`
- `matchingLeftRightConsistencyEnabled`
- `matchingRightPhaseMonotonicEnabled`
- `disparitySubpixelEnabled`
- `disparityLocalConsistencyEnabled`
- final point-cloud filter 与 stage 点云诊断

Res1F 尚未完整吸收：

- `phaseInterpolationOrderGateEnabled`
- `phaseQualityGateEnabled`
- `phaseContinuityGateEnabled`
- `phaseDirectSupportFilterEnabled`
- `matchingCandidateQualityFilterEnabled`
- `matchingRejectOnSubpixelFailure`
- `disparityRowDp*`
- `epipolarConfidenceDp*`

其中当前最应优先实施的是 matching candidate quality filter 和 subpixel failure reject。原因是现有诊断已经显示候选歧义很高：`ambiguous_candidate_pixels=40621`，而 subpixel 本身几乎都成功，说明需要在候选进入 accepted match 前补充质量门控，而不是继续调低 `phaseDiffThreshold` 或启用 smoothing。

## 第一阶段实施范围

第一阶段只做低耦合、可验证、不会默认改变生产输出的约束补齐：

1. 配置层补齐并解析：
   - `matchingRejectOnSubpixelFailure`
   - `matchingCandidateQualityFilterEnabled`
   - `matchingCandidateMinModulation`
   - `matchingCandidateRejectSaturation`
   - `matchingCandidateRejectLowLight`
2. CUDA matching kernel 增加候选质量过滤：
   - 左像素进入搜索前检查高频 modulation / saturation / low-light。
   - 右候选遍历时跳过质量不合格候选。
   - 诊断计数记录 left-quality reject 和 right-candidate quality skipped。
3. CUDA matching kernel 增加 `matchingRejectOnSubpixelFailure`：
   - 当右相位局部单调性不足导致 subpixel fallback 时，可配置拒绝该 match。
   - 默认保持 `false`，与当前 Legacy 生产配置一致。
4. 诊断输出增加新计数：
   - `left_quality_rejected`
   - `right_candidate_quality_skipped`
   - `subpixel_failure_rejected`

## 暂不在第一阶段实施的内容

`phaseInterpolationOrderGateEnabled`、phase quality gate、phase continuity gate、row-DP 和 confidence-DP 仍然保留在后续阶段。原因：

- 它们跨 phase unwrap、matching、diagnostics 多个模块，直接搬运容易把配置解析、相位可靠性 map、matching mask 和 debug map 混成一团。
- 当前 Legacy 最新配置中 phase quality / continuity / row-DP / candidate quality 默认多为关闭，先补齐可配置能力和诊断计数，再用量块数据决定哪些应进入生产默认。
- `phaseInterpolationOrderGateEnabled=true` 虽然是当前 Legacy 配置，但其主要作用点在 epipolar/LS 采样路径和相位采样顺序，Res1F 当前主路径是 rectified disparity，需要单独设计契约，不能用一个局部 if 冒充追齐。

## 验收方法

第一阶段完成后必须验证：

- 默认 `config/reconsAlgPara.json` 中新增约束保持关闭，不改变默认输出。
- 显式打开 `matchingCandidateQualityFilterEnabled=true` 后，`matching-diagnostics.csv` 出现质量过滤计数。
- 显式打开 `matchingRejectOnSubpixelFailure=true` 后，subpixel fallback 可以被拒绝并计数。
- `cmake --build build --config Release -- /m` 通过。
- `ctest --test-dir build -C Release --output-on-failure` 通过。
- 用 `D:\Data\Calib\2607151356_mach6\single_stripe` group 1 做一次 smoke，确认诊断文件可读。

## 2026-07-16 实施结果

已完成第一阶段代码落地：

- `ReconsConfig` 已补齐并解析 matching 相关 Legacy 约束键。本阶段不再解析尚未实现的 phase quality / continuity / interpolation 开关，避免配置看起来生效、实际热路径无动作：
  - `matchingCandidateQualityFilterEnabled`
  - `matchingCandidateMinModulation`
  - `matchingCandidateRejectSaturation`
  - `matchingCandidateRejectLowLight`
  - `matchingRejectOnSubpixelFailure`
- `PointCloudReconstructorCuda.cu` 已在 disparity matching kernel 前后接入左右相机高频 modulation/light flags：
  - 左像素搜索前可按 quality gate 拒绝。
  - 右候选遍历时可跳过低质量、饱和或低光候选。
  - 默认配置关闭，不改变现有生产输出。
  - 显式打开 `matchingCandidateQualityFilterEnabled=true` 时采用 fail-closed：如果缺少配置的高频 `stripeRequirements` 或左右高频条纹，不会静默降级为关闭，而是返回 `InputMissing`。
- matching diagnostics 已补齐：
  - `left_quality_rejected_pixels`
  - `right_candidate_quality_skipped`
  - `subpixel_failure_rejected`
- `point_cloud_reconstructor_test` 已覆盖：
  - 默认开关关闭时新增计数保持 `0`。
  - 显式打开 candidate quality filter 后，低 modulation synthetic 输入会被拒绝并产生诊断计数。
  - 显式打开 candidate quality filter 但缺少右侧高频条纹时返回 `InputMissing`，避免误把约束当作关闭。
- `config_config_load_test` 已覆盖新增配置键的默认值、生产配置解析和 override 解析。

## 2026-07-16 验证结果

基础回归：

```powershell
cmake --build build --config Release -- /m
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

结果：

- Release 编译通过。
- `ctest` 15/15 通过。
- `git diff --check` 通过，仅有工作区 LF/CRLF 提示。

真实量块 group 1 smoke 使用：

```powershell
.\build\Release\reconstructSample.exe `
  --config-base config\reconsAlgPara.json `
  --config D:\code\reconstructOneFrame\output\matching_constraint_smoke\matching_quality_off.json `
  --calib D:\Data\Calib\2607151356_mach6\calibResult.json `
  --single-stripe-root D:\Data\Calib\2607151356_mach6\single_stripe `
  --group 1 `
  --output D:\code\reconstructOneFrame\output\matching_constraint_smoke\group1_quality_off
```

quality filter 关闭时：

- `accepted_match_pixels=100379`
- `ambiguous_candidate_pixels=40621`
- `left_quality_rejected_pixels=0`
- `right_candidate_quality_skipped=0`
- `subpixel_failure_rejected=0`
- final `pointCloudVertexCount=82482`

打开 `matchingCandidateQualityFilterEnabled=true` 后：

- `left_phase_valid_pixels=169598`
- `accepted_match_pixels=88386`
- `ambiguous_candidate_pixels=23501`
- `left_quality_rejected_pixels=62268`
- `right_candidate_quality_skipped=995765`
- `subpixel_failure_rejected=0`
- final `pointCloudVertexCount=82184`

这说明 candidate quality filter 主要削掉 raw matching 阶段的低质量候选，accepted match 数量从 `100379` 降到 `88386`，已经接近 Legacy group 1 的 `candidate_matches=89811`。最终点数只从 `82482` 降到 `82184`，说明它不是靠 final smoothing 或大范围删点制造平整，而是在候选进入重建前收紧质量入口。

打开 `matchingRejectOnSubpixelFailure=true` 后：

- `left_phase_valid_pixels=169598`
- `accepted_match_pixels=100376`
- `subpixel_success=100376`
- `subpixel_fallback=0`
- `subpixel_failure_rejected=3`

这证明 subpixel fallback 能被开关显式转成拒绝计数；默认仍保持关闭。

## 当前判断

本阶段没有把强 smoothing 当作 RMS 修复手段。更合理的方向是继续追查 raw 候选质量、相位残差和 Legacy 保留点/删除点差异。

当前已确认：

- `phaseDiffThreshold=0.2` 不是 Res1F/Legacy 当前差距来源。
- candidate quality filter 能把 Res1F raw accepted match 数量压到 Legacy 同量级。
- 该过滤默认关闭，可作为诊断和后续生产默认评估开关。

后续如果继续追 RMS，应优先做：

1. 对比 quality-on Res1F raw 点与 Legacy raw/filter-input 点的平面残差分布。
2. 在同一像素坐标上比较 Res1F 删除/保留点与 Legacy 删除/保留点。
3. 把 `phaseInterpolationOrderGateEnabled` 做成独立 phase/matching contract，而不是在 disparity kernel 中临时硬塞。
4. 评估是否把 `matchingCandidateQualityFilterEnabled=true` 纳入生产默认；纳入前必须跑完整量块组和牙颌数据，确认不牺牲边缘有效点。

## 2026-07-16 关键修正：matching 采样语义才是 RMS 主因

后续诊断推翻了“candidate quality filter 是 RMS 主因”的判断。candidate quality filter 仍保留为显式诊断/实验开关，但六组量块证明它不能显著降低 plane RMS。真正导致 Res1F 与 Legacy 大面积 disparity 不一致的是 matching kernel 对相位图的读取方式：

- Legacy 使用 CUDA 2D texture object 读取相位，`filterMode=cudaFilterModeLinear`，`addressMode=Clamp`，非归一化坐标。
- Res1F 之前直接按 `float*` 数组读取相位。
- 左右 wrapped phase、modulation、最终 absolute phase 主体均已接近 Legacy；直接数组读取会在 subpixel matching 阶段放大为 0.1~0.3px 级别的广泛 disparity 差异。

已实施：Res1F disparity matching 改为通过 RAII 管理的 2D texture object 读取左右 phase。该改动不改变 phase 阈值、不启用 smoothing、不默认打开 candidate quality filter。

验证结果：group 1 common-pixel disparity P95 从约 `0.318px` 降到约 `0.000061px`，`>0.05px` 比例从约 `72.3%` 降到约 `0.29%`。

因此，后续 matching 追齐的优先级调整为：

1. 已完成：Legacy texture sampling contract。
2. 保留：Bmin gate、abs23 median、candidate quality filter、subpixel failure reject，作为 parity 与诊断能力。
3. 待评估：少量 disparity outlier / whole-period phase jump 与 final 保留点差异。
4. 不采用：通过强 smoothing 或简单调 `phaseDiffThreshold` 追 RMS。

## 2026-07-16 RMS 收口补充

texture sampling 修复后，matching 层大面积 disparity mismatch 已消失，但六组 plane RMS 仍略输 Legacy。继续查 Legacy 生产链路发现：Legacy 在重投影后、final connected filter 前无条件执行 3x3 point Z blur。Res1F 之前生产 JSON 关闭 `pointCloudSmoothingEnabled`，所以虽然 disparity 已对齐，final plane RMS 仍偏高。

启用 Res1F 已有的 Legacy-compatible point Z blur 后，六组 plane RMS 从 `23.05 / 25.93um` 收敛到 `19.019 / 23.207um`，与 Legacy `19.033 / 23.170um` 基本一致。由此确认 RMS 主因链路为：

1. matching phase fetch 未采用 Legacy texture sampling；
2. final point-cloud blur 默认未纳入 Res1F production config。

这两个点都不是调参，也不是额外 smoothing 捷径，而是 Legacy final output 的实际生产语义。
