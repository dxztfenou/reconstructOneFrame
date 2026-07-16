# Res1F 量块平整度追齐 Legacy 方案与回归记录

日期：2026-07-16

## 背景

用户反馈 Res1F 重建点云“看起来不太平”。在 `D:\Data\Calib\2607151356_mach6` 量块数据上，最新 Res1F 与 latest Legacy `08c9e49a` 的差距不是台阶尺度错误，而是最终点云有效性语义没有完全对齐。

本轮有效组号为 `1,2,4,7,8,9`。Res1F 只读取 `calibResult.json`，Legacy 只读取 `calibParams.yml`。常规量块评估不启用 red/green good/bad diagnostic PLY。

## 证据

原始 Res1F 使用 `D:\Data\Calib\2607151356_mach6\reconsAlgPara_adaptive_metric.json` 直接运行。该 JSON 显式开启了 subpixel、左右一致性和右相位单调穿越，但没有 `pointCloudFilterEnabled`，所以 Res1F 按旧配置兼容语义关闭最终点云 filter。日志中可见 `smoothingApplied=false, filterApplied=false`。

对比 latest Legacy：

| 口径 | Step mean/max | Flatness mean/max | RMS mean | Good ratio | Extracted ratio | Quality |
|---|---:|---:|---:|---:|---:|---|
| Res1F 原始 | 15.64 / 25.70 um | 122.33 / 139.99 um | 25.90 um | 0.794 | 0.662 | 5 OK / 1 WARN |
| Legacy latest | 10.78 / 19.81 um | 114.59 / 127.85 um | 19.03 um | 0.995 | 0.973 | 6 OK / 0 WARN |

结论：Res1F 的台阶高度已经接近 Legacy；视觉不平主要来自最终点云里保留了更多局部不连续/难以归入双平面的点。

## 方案

1. 保留 `loadReconsConfig(path)` 的旧语义：缺少新 key 的历史 JSON 不静默改变行为。
2. 新增显式 base/override 配置入口：以 `config/reconsAlgPara.json` 作为 Res1F production defaults，再用数据集 JSON 覆盖标定、深度范围、phase/matching 等实验参数。
3. 在 sample 中提供 `--config-base <path>`，保证 benchmark 和现场复现时不会因为数据集 JSON 缺少 Res1F 专属 key 而关闭 production filter。
4. 后续复核 Legacy 源码后修正：Legacy 在 final filter 前无条件执行 3x3 point Z blur。因此 production 默认应启用 `pointCloudSmoothingEnabled=true` 与 `pointCloudFilterEnabled=true`，历史缺 key JSON 仍保持兼容关闭。

这条路径的核心是把“旧配置兼容”和“生产默认继承”拆开。旧配置继续可复现旧行为；新的 benchmark 明确表达自己需要 production defaults。

## 实施

已新增：

- `loadReconsConfigWithBase(basePath, overridePath, config)`
- `InitOptions::configBasePath`
- `reconstructSample --config-base <path>`
- 配置 overlay 回归测试：覆盖 base key 继承、override key 替换、旧单文件缺 key 仍保持关闭
- MSVC `/utf-8` 编译选项，避免中文维护注释触发代码页警告

关键维护边界已经写在 `src/config/ReconsConfig.cpp` 中：数据集 JSON 经常只描述标定/实验参数，只有显式 base 才继承 production defaults；单文件加载仍保持旧兼容语义。

## 回归

使用命令模式：

```powershell
.\build\Release\reconstructSample.exe `
  --config-base ".\config\reconsAlgPara.json" `
  --config "D:\Data\Calib\2607151356_mach6\reconsAlgPara_adaptive_metric.json" `
  --calib "D:\Data\Calib\2607151356_mach6\calibResult.json" `
  --single-stripe-root "D:\Data\Calib\2607151356_mach6\single_stripe" `
  --group <1|2|4|7|8|9> `
  --output ".\output\2607151356_gauge_block_nonmetal_08c9e49\catchup_filter_base\res1f\<group>"
```

结果：

| 口径 | Step mean/max | Flatness mean/max | RMS mean | Good ratio | Extracted ratio | Quality |
|---|---:|---:|---:|---:|---:|---|
| Res1F 原始 | 15.64 / 25.70 um | 122.33 / 139.99 um | 25.90 um | 0.794 | 0.662 | 5 OK / 1 WARN |
| Res1F base+filter | 11.30 / 24.93 um | 113.01 / 120.56 um | 25.77 um | 0.946 | 0.801 | 6 OK / 0 WARN |
| Legacy latest | 10.78 / 19.81 um | 114.59 / 127.85 um | 19.03 um | 0.995 | 0.973 | 6 OK / 0 WARN |

`base+filter` 输出路径：

- PLY：`output/2607151356_gauge_block_nonmetal_08c9e49/catchup_filter_base/res1f/<group>/depth_points.ply`
- 指标：`output/2607151356_gauge_block_nonmetal_08c9e49/catchup_filter_base/metrics_res1f/metric_summary.json`

## Smoothing 消融

额外测试 `pointCloudSmoothingEnabled=true` + filter：

| 口径 | Step mean | Flatness mean | RMS mean | Good ratio | Extracted ratio |
|---|---:|---:|---:|---:|---:|
| filter only | 11.30 um | 113.01 um | 25.77 um | 0.946 | 0.801 |
| smoothing + filter | 11.62 um | 111.19 um | 21.35 um | 0.991 | 0.949 |
| Legacy latest | 10.78 um | 114.59 um | 19.03 um | 0.995 | 0.973 |

smoothing 的表面残差更接近 Legacy，但点数在后三组降到 `40814..48300`，明显低于 Legacy `86119/89550/89197` 同量级输出。它更像把困难区域裁掉，而不是稳定追齐 Legacy 的完整输出语义，因此本轮不启用。

## 剩余差距

当前重大缺陷已经收敛：Res1F 不再出现 WARN，台阶高度和 flatness 与 Legacy 基本同量级，点数也从 raw 过密回到 final-output 口径。

仍未追平的部分是残差 RMS 与 extracted ratio。下一步若继续追 Legacy，应聚焦：

- Legacy 最终点云中哪些点被保留但 Res1F filter 删除；
- Res1F 保留点的相位残差是否来自强度域 rectification 后仍存在的 interpolation/noise 差异；
- 不要用整帧质量分或强 smoothing 掩盖点云几何差异。

## 2026-07-16 RMS 追因补充

本轮补了 Res1F stage 点云诊断，只在 `saveOutputs=true && saveStagePointClouds=true` 时生效，默认热路径不额外回传整帧点云。诊断输出包括：

- `raw-points.ply`：重投影后、final filter 前的 raw 点；
- `filter-input-points.ply`：真正进入三维连通性 filter 的点；
- `filter-deleted-points.ply`：filter 输入中被 final filter 删除的点；
- `depth_points.ply`：final 输出。

group 1 隔离输出位于 `output/2607151356_gauge_block_nonmetal_08c9e49/rms_diag_explicit_filter/res1f_group1`。显式运行 JSON 为 `reconsAlgPara_res1f_explicit_filter_stage.json`，其中 `pointCloudFilterEnabled=true`、`pointCloudSmoothingEnabled=false`、`saveOutputs=true`、`saveStagePointClouds=true`。

用 Res1F final 双平面作为参考，group 1 的 stage 残差显示：

| 点集 | 点数 | RMS | P95 | `>150um` |
|---|---:|---:|---:|---:|
| Res1F final | 82,482 | 36.3 um | 71.2 um | 0.047% |
| Res1F raw/filter-input | 100,379 | 5,411 um | 11,479 um | 12.18% |
| Res1F filter-deleted | 17,897 | 12,815 um | 31,654 um | 68.11% |
| Legacy final | 82,468 | 19.5 um | 37.9 um | 0.0049% |

结论：Res1F final filter 删除的点大多确实是离平面很远的坏点，但 filter 后仍比 Legacy 粗；因此 RMS 不能靠继续加大 final filter 解释完。

再用 Legacy final 双平面作为参考，Legacy stage/debug 复现输出在 `output/2607151356_gauge_block_nonmetal_08c9e49/rms_diag_explicit_filter/legacy_stage_debug/1`。关键计数：

- Legacy `left_phase_valid=107869`、`candidate_matches=89811`、`reproj_points=89811`、`filter_input=91340`、`filter_kept=82468`。
- Res1F filter-only `rawValid=100379`、`filteredValid=82482`。
- 同一 Legacy 平面参考下，Legacy raw P95 约 `55.8um`，Res1F raw P95 约 `11,479um`。

这说明差距已经前移到 phase/matching 生成候选阶段：Res1F 在 final filter 之前多放出了约 `10.6k` raw 候选，且这些候选包含明显离面层状点。后续追 RMS 应先对齐相位/匹配诊断，而不是直接把 `pointCloudSmoothingEnabled=true` 作为默认修复。

smoothing+filter 只作为消融保留：group 1 可把 final RMS 拉到约 `22.6um/23.2um`，但它把 filter-input 扩到 `141,706` 个临时点，又删除 `68,006` 个点，final 只剩 `73,700` 点；这更像孔洞扩散后过裁剪，不是稳定追齐 Legacy 的口径。

## 2026-07-16 分模块诊断开关

诊断后续不能再靠临时改代码打开。配置层新增三个 bool 开关，全部默认 `false`：

- `phaseDiagnosticsEnabled`：打开后，在 `saveOutputs=true` 且 CLI 有输出目录时保留 wrapped phase 的 modulation，并写出 `phase-diagnostics.csv`。该文件按左右相机和频率记录 valid pixel、mean/min/max modulation，以及 modulation 是否被 materialize。
- `matchingDiagnosticsEnabled`：打开后写出 `matching-diagnostics.csv`，记录 left phase valid、threshold/uniqueness/LR/monotonic reject、accepted match、subpixel success/fallback、候选歧义数量和 accepted match cost。默认不开，避免 matching kernel 为诊断额外 atomic 计数和 D2H 拷贝。
- `pointCloudStageDiagnosticsEnabled`：打开后写出 `raw-points.ply`、`filter-input-points.ply`、`filter-deleted-points.ply`。历史 `saveStagePointClouds` 仍作为兼容 alias 保留，两者任一为 true 即启用点云阶段诊断。

所有诊断仍受 `saveOutputs` 主开关约束：生产默认配置不写内部阶段产物，不额外回传整帧调试数组。RMS 追齐下一步应基于这些分模块产物比较 Legacy/Res1F 的相位质量、匹配候选接受/拒绝结构和 final filter 保留差异。

## 2026-07-16 texture sampling 根因修复与六组回归

继续对比 Legacy `08c9e49` 后，前一版“candidate quality filter 是主方向”的判断被修正：candidate quality filter、abs23 median 和 Bmin gate 都是必要的 Legacy parity/诊断补齐，但它们没有解释 RMS 主差距。真正能解释 group 1 大面积 disparity 差异的是 matching kernel 的相位读取语义。

Legacy disparity 热路径通过 CUDA 2D texture 读取左右绝对相位：`cudaFilterModeLinear`、`cudaAddressModeClamp`、`normalizedCoords=0`。Res1F 之前直接读 `float*` 数组。相位图本身已经与 Legacy 基本一致，但 matching 阶段的采样语义不同会把同一相位输入转换成不同 subpixel disparity。

已实施：Res1F `PointCloudReconstructorCuda.cu` 在 matching 阶段改用 Legacy 同款 2D texture 采样；不启用 point-cloud smoothing，不调宽/调窄 `phaseDiffThreshold`，也不把 candidate quality filter 作为默认 RMS 修复。

### group 1 debug map 对齐结果

对比路径：

- Legacy：`output/2607151356_gauge_block_nonmetal_08c9e49/rms_diag_explicit_filter/legacy_stage_debug/1/debug-maps.yml.gz`
- Res1F：`output/2607151356_texture_sampling_eval/res1f/1/pointcloud-debug-maps.yml.gz`

修复后 disparity common-pixel 差异：

| 指标 | 修复前 | texture sampling 修复后 |
|---|---:|---:|
| disparity P50 | 0.09596 px | 0.00003 px |
| disparity P95 | 0.31799 px | 0.00006 px |
| disparity `>0.05px` | 72.32% | 0.29% |
| depth Z P95 | 54.94 um | 30.40 um |

结论：原先的大面积 subpixel disparity mismatch 已经解决。剩余少量离群仍存在，主要表现为少量 Res1F-only/Legacy-only final 点和稀疏 phase whole-period jump，不应靠 smoothing 抹平。

### 六组量块回归

数据：`D:\Data\Calib\2607151356_mach6\single_stripe`，组号 `1,2,4,7,8,9`。Res1F 使用 `calibResult.json`，Legacy 使用 `calibParams.yml`。Res1F 输出：`output/2607151356_texture_sampling_eval/full/res1f/<group>/depth_points.ply`。

| 口径 | Step abs mean/max | Flatness mean/max | Plane RMS mean/max | Good ratio | Extracted ratio | Quality |
|---|---:|---:|---:|---:|---:|---|
| Res1F texture sampling | 10.79 / 24.66 um | 113.88 / 127.06 um | 23.05 / 25.93 um | 0.984 | 0.915 | 6 OK |
| Legacy `08c9e49` | 10.78 / 19.81 um | 114.59 / 127.85 um | 19.03 / 23.17 um | 0.995 | 0.973 | 6 OK |
| Res1F before texture fix | 15.64 / 25.70 um | 122.33 / 139.99 um | 25.90 um | 0.794 | 0.662 | 5 OK / 1 WARN |

结论：用户关心的“Res1F 看起来点云不太平 / flatness 明显更差”已经基本解决；flatness 已与 Legacy 持平，台阶高度也追到 Legacy 同量级。严格说，plane residual RMS 尚未完全追平 Legacy，仍高约 `4.0um` mean RMS、约 `3.8um` MAD；这属于剩余精修项，不再是原先的大缺陷。

### 当前不可再采用的错误方向

- 已修正：`pointCloudSmoothingEnabled=true` 不是额外捷径，而是 Legacy final output 的生产语义；默认启用后 RMS 才与 Legacy 对齐。
- 不应把 `matchingCandidateQualityFilterEnabled=true` 直接作为 RMS 修复默认。它减少 raw candidates，但真实主因已经证明是 texture sampling。
- 不应继续围绕 `phaseDiffThreshold=0.4` 调参。当前 Legacy 和 Res1F 生产配置均为 `phaseDiffThreshold=0.2`。

### 后续若继续追齐 Legacy RMS

剩余工作应聚焦少量保留点差异：

1. 对 Res1F-only / Legacy-only final pixels 做平面残差统计，判断 extra points 是否主导剩余 RMS。
2. 追稀疏 whole-period phase jump 与 `>0.05px` disparity outlier 的空间重合。
3. 如要进一步收敛，应设计 edge-aware / phase-continuity 约束，而不是开启强 smoothing。


## 2026-07-16 RMS 最终追齐修正：Legacy final blur 必须纳入生产语义

用户指出六组 plane RMS mean/max 仍输给 Legacy 后，继续复核 Legacy `calcDepthImage.cu`，发现前一版判断仍漏了一段关键生产逻辑：Legacy 在 `reprojectTo3DKernel` 后、`nonConnectedComponentFilterKernel` 前无条件执行：

```cpp
gaussianBlurKerneltoPt<<<...>>>(d_point_cloud, d_point_cloud_new, width, height, KERNEL_SIZE_S);
nonConnectedComponentFilterKernel<<<...>>>(d_point_cloud_new, d_point_cloud_new1, ...);
```

也就是说 Legacy final output 本来就是“texture-sampled disparity + 3x3 point Z blur + final connected filter”。这不是额外强 smoothing，也不是为了指标临时抹平；它是 Legacy 的生产 final point-cloud filter 前置步骤。Res1F 之前把 `pointCloudSmoothingEnabled=false` 当作生产默认，因此在 texture sampling 修复后仍保留了约 `4um` mean RMS 差距。

已修正：`config/reconsAlgPara.json` 生产默认改为 `pointCloudSmoothingEnabled=true`；`ReconsConfig` 缺 key 默认仍保持 `false`，保证历史单文件 JSON 不静默改变行为。

代码审查补充：未接入热路径的 phase quality / continuity / interpolation 开关已从 production JSON 和 `ReconsConfig` 解析结构中移除。它们仍作为后续设计项保留在 matching 约束文档里，但不能以“已配置”的形式进入生产默认，否则会给下游留下开关已生效的错误信号。

### texture sampling + Legacy blur 六组回归

输出：`output/2607151356_texture_sampling_eval/smoothing_on_full/res1f/<group>/depth_points.ply`

| 口径 | Points mean/max | Step abs mean/max | Flatness mean/max | Plane RMS mean/max | MAD mean/max | Good ratio | Extracted ratio | Quality |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Res1F texture + blur | 83,229 / 89,629 | 9.39 / 18.28 um | 110.42 / 121.55 um | 19.019 / 23.207 um | 13.199 / 17.182 um | 0.99499 | 0.97335 | 6 OK |
| Legacy `08c9e49` | 83,353 / 89,550 | 10.78 / 19.81 um | 114.59 / 127.85 um | 19.033 / 23.170 um | 13.194 / 17.162 um | 0.99500 | 0.97313 | 6 OK |

结论：这次 RMS 问题已经闭合到 Legacy 同量级。Res1F plane RMS mean 略低于 Legacy `0.014um`，max 高约 `0.037um`，属于数值噪声级别；点数、good ratio、extracted ratio 也基本一致。

当前最终判断：

1. 大面积 disparity mismatch：由 matching texture sampling 语义缺失导致，已修复。
2. plane RMS 剩余差距：由 Legacy final blur 没纳入生产默认导致，已修复。
3. candidate quality filter、Bmin gate、abs23 median、subpixel failure reject：保留为 Legacy parity 与诊断能力，但不是本轮 RMS 主因。
