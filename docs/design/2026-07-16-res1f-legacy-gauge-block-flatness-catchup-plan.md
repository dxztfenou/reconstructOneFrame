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
4. 本轮只启用 production 已验证的 `pointCloudFilterEnabled=true`，保持 `pointCloudSmoothingEnabled=false`。smoothing 作为消融记录，不进入默认路径。

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
