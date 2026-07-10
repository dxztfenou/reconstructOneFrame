# Legacy 单帧逐阶段执行链与 frame 0/100 乱点根因

## 1. 结论摘要

- Legacy 并不是在相位或匹配阶段完全避免乱点。当前可执行文件重跑 frame 0 时，仍先生成 `31086` 个 raw 三维点。
- Legacy frame 0 最终几乎无点，主要依赖重投影后的固定点云平滑和局部三维连续性过滤：`31086 -> 87014 filter input -> 29 final`。
- frame 100 存在真实连续表面，因此 `131540` 个 raw 点最终保留 `89900` 点。
- 2026-07-07 历史 EXR 的最终点数为 frame 0=`50`、frame 100=`124032`。当前二进制与历史输出版本并不完全一致，但最终强过滤的机制一致。
- 当前新算法盯帧配置关闭了 `pointCloudSmoothingEnabled` 和 `pointCloudFilterEnabled`，所以展示的是 raw 匹配结果；这与 Legacy 历史 final `0.exr` 不是同一阶段口径。

## 2. 版本与数据

- Legacy 工作区：`D:\code\teethscanalgorithm3x4`
- 当前分支：`feature/single-frame-quality@27613a3`
- 最新远程主线：`origin/main@5410c14`
- 当前 App 运行版本日志：`v20251111.1`
- 输入数据：`D:\Data\output260707_gypsum\Upper`
- 标定：`D:\Data\Calib\2607011016_mach6\calibParams.yml`
- 诊断产物：`D:\code\reconstructOneFrame\build\legacy_step_trace\baseline_debug`

当前 Release 二进制生成时间晚于 2026-07-07 历史输出，因此当前重跑用于解释执行机制，不声称逐点复现历史 EXR。

## 3. Legacy 每一步做什么

### 3.1 App 输入

`TeethScanAlgorithmApp` 对每个数字帧目录读取 `SourceImg/L0.bmp..L17.bmp` 和 `R0.bmp..R17.bmp`。

- 前 15 张：3 个频率，每个频率 5 步相移。
- 左相机最后 3 张：B/G/R 颜色辅助图。
- 右相机 remap 时不处理最后 3 张颜色图。

### 3.2 图像上传和强度域校正

左右图像先上传 GPU，然后 `remapKernel` 使用标定 `mapx/mapy` 对每一张条纹强度图做双线性重采样。

- 超出 remap 范围的像素写 NaN。
- 这是 **intensity remap before phase**。
- 当前新算法仍使用 post-unwrap phase remap 近似，两者时序不同。

### 3.3 每频率 wrapped phase

每个频率对 5 步图计算：

```text
sinSum = sum(I_k * sin(2*pi*k/STEP))
cosSum = sum(I_k * cos(2*pi*k/STEP))
phase = -direction * atan2(sinSum, cosSum)
modulation = sqrt(sinSum^2 + cosSum^2) * 2 / STEP
```

当 modulation `< Bmin` 时，该频率 wrapped phase 写 NaN。

实际配置：

```text
Bmin = 1
phaseQualityMinModulation = 8
phaseQualityGateEnabled = false
```

因此 modulation 在 `1..8` 的弱信号像素仍可继续重建。

### 3.4 two-stage absolute phase

默认 `legacyTwoStage`：

1. 计算 `PH12`、`PH23`、`PH123`。
2. 使用 `PH23/PH123` 计算 `abs23`。
3. `abs23` 经过 5×5 中值滤波。
4. 使用高频 wrapped phase 和 `abs23` 计算最终 absolute phase。

任一级输入 NaN 都继续传播 NaN。配置开启 residual gate 时，整数级次舍入残差过大也会写 NaN；实际配置关闭 residual gate。

### 3.5 整帧 unwrap gate

最终 absolute phase 检查相邻有效像素的相位跳变：

- 单条边跳变阈值：`1rad`
- 整帧失败阈值：跳变边比例 `>80%`

该规则只能拦截极端全帧崩坏，不能识别“相位连续但匹配到错误周期”的深度幕布。

### 3.6 phase quality 和 semantic gate

默认 disparity 路径中，原图 phase-quality gate 被明确跳过。

- `phaseQualityGateEnabled=false`
- 非 AI 石膏扫描的 semantic mask 全零
- 高频饱和/低亮 flags 只进入后置 quality map，不直接清零 absolute phase

### 3.7 disparity 匹配

当前质量分支的执行顺序：

1. 左 absolute phase 必须非零、非 NaN。
2. 根据 Q 和工作深度计算物理 disparity window。
3. 在右图同一行搜索最小 absolute-phase 差。
4. 最小 cost 必须 `< phaseDiffThreshold=0.2`。
5. 可选 uniqueness。
6. 可选 LR consistency。
7. 可选 subpixel。
8. 写 disparity。

历史配置关闭：

```text
matchingUniquenessEnabled = false
matchingLeftRightConsistencyEnabled = false
disparitySubpixelEnabled = false
disparityLocalConsistencyEnabled = false
```

最新 `origin/main` 默认增加 LR、右相位单调穿越和 subpixel，但仍保留后续点云过滤。

### 3.8 Q 重投影和 Z gate

disparity 通过 Q 矩阵转换为 XYZ。

- `W` 必须非零。
- `Z` 必须位于 `minZ..maxZ`。
- 无效点写 `(0,0,0)`。

大比拼临时配置使用 `minZ=30/maxZ=180`，比仓库默认 `50/160` 更宽。

### 3.9 3×3 点云 Z 平滑

Legacy 的 `gaussianBlurKerneltoPt`：

- 仅对 3×3 邻域的 `z` 做高斯平均。
- 中心像素的 `x/y` 保持不变。
- 空洞中心可能临时变为 `(0,0,smoothedZ)`。

这些临时点随后通常因三维邻域不一致被过滤。

### 3.10 15×15 局部三维连续性过滤

每个非零点必须同时满足：

1. 3×3 内至少 5 个距离 `<0.1mm` 的点。
2. 15×15 内至少约 177 个有效点。
3. 距离 `<1mm` 的邻点平均距离 `<0.8mm`。

该过滤不是全局 connected component，而是强局部稠密/连续性 gate。

- 真实物体表面通常能通过。
- 稀疏随机点会被删除。
- 大面积规则错误幕布也可能通过，因此匹配约束仍然必要。

### 3.11 法向、金属补洞、质量图

- 过滤后计算法向。
- 只有金属扫描才执行 normal-guided hole filling。
- 非金属石膏 frame 0/100 不补洞。
- quality map 和 frame sentinel 在最终点云生成后计算，不反向删除 depth。

### 3.12 最终输出

最终点云只排除精确 `(0,0,0)`，不会根据 normal、quality score 或 frame sentinel 再删点。

输出语义：

- `0.exr`：最终 filtered depth/XYZ
- `1.exr`：normal
- `2.png`：color
- `3.png`：quality info

## 4. frame 0 与 frame 100 阶段数据

| 阶段 | frame 0 | frame 100 |
|---|---:|---:|
| 左相位有效 | 130975 | 161485 |
| 匹配候选通过 | 31195 | 135089 |
| Q 重投影有效 raw 点 | 31086 | 131540 |
| 平滑后进入 filter | 87014 | 145286 |
| 当前 Legacy final | 29 | 89900 |
| 2026-07-07 历史 EXR final | 50 | 124032 |

信号质量：

| 指标 | frame 0 | frame 100 |
|---|---:|---:|
| 左高频 modulation P50 | 5.48 | 32.97 |
| 右高频 modulation P50 | 4.52 | 30.94 |
| 匹配 cost P50 | 0.0761 | 0.0787 |
| 匹配 cost P95 | 0.1856 | 0.1612 |

phase cost 分布高度重叠，因此只降低 `phaseDiffThreshold` 很难同时保留 frame 100 并删除 frame 0。

## 5. 与当前新算法的直接对照

当前 monotonic+LR 配置：

| 输出口径 | frame 0 | frame 100 |
|---|---:|---:|
| 新算法 raw，filter off | 27430 | 118748 |
| 新算法 filter-only | 0 | 84833 |
| 新算法现有 XYZ smooth+filter | 3793 | 94529 |
| 当前 Legacy final | 29 | 89900 |

说明：

- `filter-only` 已能把 frame 0 压到 0 点，同时 frame 100 保留 `84833` 点，行为接近当前 Legacy final。
- 新算法现有 smoothing 会对有效邻点的完整 XYZ 做加权平均并填入空洞，不等价于 Legacy Z-only smoothing。
- 现有 XYZ smoothing 使 frame 0 从 0 点重新增加到 `3793` 点，因此不能直接作为生产默认开启。

## 6. 根因排序

1. **比较口径错误**：当前查看的是新算法 raw PLY，历史 Legacy `0.exr` 是 final filtered depth。
2. **frame 0 信号弱但 Bmin 太低**：高频 modulation 中位数仅 4–5，但 `Bmin=1` 且 phase-quality gate 关闭。
3. **phase cost 不具区分力**：frame 0/100 的匹配 cost 分布重叠。
4. **Legacy 依赖强后过滤**：frame 0 raw 也有约 3.1 万点，最终靠局部三维连续性过滤降到几十点。
5. **新 smoothing 语义不正确**：完整 XYZ 插值会重新制造无效点。

## 7. 下一步建议

1. 先将生产输出明确区分为 `rawPointCloud` 和 `filteredPointCloud`，盯帧默认查看 filtered 结果。
2. 默认启用现有 `pointCloudFilterEnabled`，但暂不启用当前 XYZ smoothing。
3. 单独实现 Legacy-compatible Z-only 3×3 smoothing，作为可消融开关，与 `filter-only` 比较。
4. 用 frame `0/100/447` 和全量 466 帧验证：正常帧点数、sparse 帧误点、Z 分布和耗时。
5. 后续再处理 intensity-remap-before-phase 和 modulation/phase-validity gate；这些用于减少 raw 错点，不能替代 final 三维连续性过滤。
