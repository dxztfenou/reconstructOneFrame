# 07131838 Res1F 与最新 Legacy 效率/帧质量大比拼

> 日期：2026-07-13
> Legacy：GitLab `origin/main@46375a0e4f89252869aa82a6028a290c25b85c6e`
> 结论：默认新增能力已移植并通过真实数据消融。2026-07-14 进一步修复了“phase 计算后才 rectification”的重大时序缺陷；修复后的 non-metal 核心结果已接近 Legacy，metal 剩余差距主要是 Legacy 独有的法向引导补洞。后台 SLAM 污染期间的耗时不再作为正式性能结论。

## 1. 本轮移植

- ABI 从 1.1 升至 1.2，在 `RofFrameInputV1` 尾部增加 AI/metal frame flags；旧 136-byte 结构前缀继续可用。
- DSSI `RofPluginProvider` 和迁移期 `threeScan_*` shim 真实转发 metal/AI；插件声明 metal capability，AI 未实现时 fail closed。
- `clear255` 使用 session-owned CUDA mask/scratch/counter：第一张左辅助帧四邻域局部最大值 `>=250` 时无效，按配置半径膨胀，并在 disparity matching 前 gate。
- non-metal 三张左辅助彩色帧在 rectification 采样后、颜色矩阵/gamma 前执行 Legacy 余弦高光压缩。
- 质量 modulation/light flags 改为 GPU 上按 rectified 高频五步图计算；clear mask 使用 Legacy semantic-background 汇总语义。
- production JSON 显式启用 `clear255=true`、`clear255DilateRadius=3` 和 `colorHighlightCompressionEnabled=true`；缺 key 的旧配置保持关闭。

未迁移 Legacy 中默认关闭的 uniqueness、candidate filter、row-DP 和 matching diagnostic maps。

## 2. 输入和运行口径

- 原始帧：`D:\code\dentalscanserviceinterface\build\Release\output_test\output 07131838_teeth\Upper`。
- frame `0..478`，共 479 帧；每帧左右各 18 张 `424x400` BMP。
- scan mode：双方均为 `metal`。
- Res1F 标定：`calibResult.json`，SHA256 `6EF614F5674B55C4EED135F5DA195DF951C60206CA7CDB0A1ED5A5565334C1BF`。
- Legacy 标定：同目录 `calibParams.yml`，SHA256 `E48D3E77FE26AB76D7ED0569C87AF2F0A030F50E86D2C4BCCFFDD5B625FC605B`。
- Res1F DLL SHA256：`A275513BF3B58297238B5B7DFD87539B616F26993EF7B017BAAA203A1BCE1BCE`。
- Legacy DLL SHA256：`66A999AEF3B1070AB6B1C40335BE5022E61E1FEB735CCD8EA11606121E57A68C`。

双方共用 DSSI `reconstruction_provider_replay.exe` 的 BMP loader、provider adapter、完整 depth/color/normal/quality 输出和计时。两边强制开启质量图与 reason channel；Res1F 的 `CV_16UC3` sentinel 和 Legacy 的 `CV_64FC3` sentinel 统一归一化为 `score 0..1 + reason bits`。

最新 Legacy build tree 缺少自动复制的一级依赖。脚本从受控 ThirdParty 路径把 `opencv_world453.dll`、`tbb12.dll`、`nvinfer.dll` 和 `Log.dll` 放入隔离 runtime；没有借用 feature 分支旧 DLL。

## 3. 效率

最终 Release DLL 使用 Legacy-first v5 完整运行。该轮 Legacy 的 BMP load 受冷缓存影响，因此算法结论以 `processMs` 为主，不用 wall/FPS 排名。

| 指标 | Res1F | 最新 Legacy | 对比 |
|---|---:|---:|---:|
| 成功/质量帧 | 479/479 | 479/479 | 相同 |
| process p50 | 37.013 ms | 29.319 ms | Res1F 慢 26.2% |
| process p90 | 46.209 ms | 33.892 ms | Res1F 慢 36.3% |
| process p99 | 51.947 ms | 39.886 ms | Res1F 慢 30.2% |
| 最小 process | 25.545 ms | 19.281 ms | Legacy 更低 |

GPU clear mask 本身不是主要瓶颈；完整质量输出仍包含 stage host 往返、五张质量图 H2D、grid materialization、CPU frame summary 和 ABI output copy。

## 4. 点数与帧质量

| 指标 | Res1F | 最新 Legacy | 对比 |
|---|---:|---:|---:|
| 总有效点 | 24,359,856 | 50,814,111 | Res1F/Legacy=`0.479392` |
| 质量均值 | 0.501021 | 0.886948 | Res1F 低 0.385927 |
| 质量 p50 | 0.467857 | 0.964025 | 明显不一致 |
| 质量 p90 | 0.920089 | 0.981166 | 高质量尾段较接近 |
| 质量 Pearson | - | - | `0.578322` |
| 绝对分差 mean/p50/p90 | - | - | `0.385928/0.378180/0.712623` |
| reason 组合完全相同 | - | - | 281/479=`58.66%` |

reason bit 出现帧数：

| Reason | Bit | Res1F | Legacy |
|---|---:|---:|---:|
| LowModulation | 1 | 479 | 479 |
| Saturation | 2 | 12 | 11 |
| DepthRange | 8 | 479 | 479 |
| LocalDiscontinuity | 9 | 441 | 255 |
| HoleEdge | 10 | 479 | 479 |

帧质量公式和 sentinel 契约已基本对齐，但分数结果没有对齐。Res1F 质量分与自身有效点数 Pearson=`0.976254`，Legacy 为 `0.865676`；最大分差帧也都是 Res1F 仅剩数千点而 Legacy 仍有约十万点的帧。因此低分主要是对当前稀疏/碎片化输出的真实评价，不能通过缩放分数掩盖。

Res1F 的点数、质量分和 reason bits 在两轮最终算法运行中逐帧完全一致。Legacy 曾在 frame 87 出现 `134821/134823` 的 2 点差异，质量分仅变化 `0.000003`。

## 5. clear255 消融

使用相同 479 帧、metal 和质量配置，只关闭 `clear255`：

| Provider | clear255 off | clear255 on | 删除点数 | 删除比例 |
|---|---:|---:|---:|---:|
| Res1F | 25,139,623 | 24,359,856 | 779,767 | 3.10% |
| Legacy | 51,666,720 | 50,814,111 | 852,609 | 1.65% |

两边删除点数的绝对量接近，证明新 mask 已真实作用于生产数据。即使关闭 mask，Res1F/Legacy 点数比也只有 `0.486573`；剩余密度差主要来自既有 rectification、matching、filter 和 Legacy metal hole filling，不应归因于本次移植。

clear-off 轮次受到明显主机调度抖动，只用于点数/质量消融，不用于效率排名。

## 6. 结论和后续边界

1. `46375a0e` 中默认开启且缺失的 clear255/高光压缩已完成架构化移植，不包含 Legacy 的逐帧 CUDA allocation 或 `cv::Mat` mask 往返。
2. ABI mode 不再被静默忽略，DSSI metal 数据现在真实进入 Res1F。
3. 帧质量“评价契约”已基本一致，尤其 saturation reason 已从 `143 vs 11` 收敛到 `12 vs 11`；但“评价结果”因点云覆盖差异仍明显不一致。
4. 当前 Res1F 完整质量路径仍慢于最新 Legacy，不能宣称性能已追平。
5. 后续若要追平结果，应独立评估 Legacy metal hole filling、matching/rectification 密度和低点帧状态门禁；不应通过关闭 LR/monotonic 约束或修改质量分公式制造表面一致。

## 7. 复现

```powershell
./scripts/run_07131838_quality_battle.ps1 `
  -FirstFrame 0 -LastFrame 478 -RunLabel full_0_478_v5_release `
  -ProviderOrder legacy-first -ScanMode metal

./scripts/summarize_07131838_quality_battle.ps1 -RunLabel full_0_478_v5_release
```

逐帧 CSV、日志、hash metadata 和 summary 位于 `output/07131838_quality_battle/`。

## 8. 2026-07-14 phase rectification 时序修复

### 8.1 根因

旧 Res1F 先在畸变图上计算 wrapped/absolute phase，最后才对 absolute phase 做双线性 remap。Legacy 则先 remap 每张条纹强度图，再执行 `atan2`、跨频相位差和条纹级次取整。这些非线性操作与 remap 不可交换。

排除项：

- JSON 与 YAML 中 `K/D/R/T/R_L/R_R/P_L/P_R/Q` 逐元素完全一致，不是标定格式差异；
- 关闭点云过滤虽增加点数，但保留高碎片 raw 点，不是修复；
- 打开 Legacy Z-only smoothing 在 frame 2/26 上都进一步减少最终点数，不是修复。

修复后，wrapped-phase CUDA kernel 在求 sin/cos 之前按同一 K/D/R/P 映射双线性采样每张条纹，并通过 `PhaseCoordinateDomain::Rectified` 阻止 point-cloud 阶段二次 remap。关键分支增加了面向维护者的原因注释。

### 8.2 三帧聚焦验证

| frame | 旧 Res1F | 修复后 Res1F | Legacy | 变化 |
|---:|---:|---:|---:|---:|
| 2 | 107,020 | 122,531 | 134,659 | +14.5% |
| 26 | 2,591 | 48,527 | 108,599 | 18.7x |
| 311 | 23 | 6,288 | 36,154 | 273.4x |

### 8.3 479 帧 metal 全量

| 指标 | 旧 Res1F | 修复后 Res1F | Legacy |
|---|---:|---:|---:|
| 成功/质量帧 | 479/479 | 479/479 | 479/479 |
| 总有效点 | 24,359,856 | 36,421,770 | 50,814,113 |
| Res1F/Legacy 点数比 | 0.479392 | 0.716765 | 1.0 |
| 质量均值 | 0.501021 | 0.710554 | 0.886948 |
| 质量 Pearson | 0.578322 | 0.861027 | 1.0 |
| LocalDiscontinuity 帧数 | 441 | 406 | 255 |

### 8.4 non-metal 边界对照

同一修复工作树在 non-metal `0..478` 上得到：

- Res1F/Legacy 总点数 `37,885,889/39,830,065`，比例 `0.951188`；
- 质量均值 `0.722236/0.769732`；
- 质量 Pearson `0.971625`；
- 质量绝对分差 mean/p50/p90=`0.057909/0.049244/0.116190`。

这证明基础 rectification/phase/matching/filter 链已基本对齐。metal 从 `95.1%` 降到 `71.7%` 的主要原因是 Legacy 在 metal 模式额外执行法向引导 hole filling；该能力应作为独立算法变更审查，不能通过关闭过滤或抬高质量分伪装。

本轮运行期间可能有后台 SLAM 占用 CPU/GPU，因此新增 run 的 process/wall 数据只作运行证据，不更新正式性能排名。
