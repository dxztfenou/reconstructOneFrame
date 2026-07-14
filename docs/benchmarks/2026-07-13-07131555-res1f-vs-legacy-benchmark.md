# 07131555 Res1F 与 Legacy 性能大比拼

> 日期：2026-07-13
> 结论：两边均完成 `0..294` 全量运行；当前 Legacy 在完整输出延迟和有效点数上领先，Res1F 的新架构边界已建立，但 metal 能力、低点状态语义和 p99 性能门禁尚未达标。

## 1. 输入和二进制

- 原始帧：`D:\code\dentalscanserviceinterface\build\Release\output_test\output 07131555_teeth\Upper`，frame `0..294`，共 295 帧。
- 每帧：左右相机各 18 张 `424x400` 单通道 BMP。
- Res1F 标定：`D:\Data\Calib\2607021545_mach6\calibResult.json`，SHA256 `6EF614F5674B55C4EED135F5DA195DF951C60206CA7CDB0A1ED5A5565334C1BF`。
- Legacy 标定：同目录 `calibParams.yml`，SHA256 `E48D3E77FE26AB76D7ED0569C87AF2F0A030F50E86D2C4BCCFFDD5B625FC605B`。
- Res1F DLL SHA256：`CC174A128887E8E37BCAB2D9C3C750E17E53920A6591FC3A748875D9AD62EFFE`。
- Legacy DLL SHA256：`93A67CE210A1A10030815CD156CD56572F15E9B06EC07D9DBC7FC40397A3520B`。

JSON 与 YAML 的 `KK_L/KK_R/R/T` 数值一致，T 为 `[-17.63054248, 0.20546080, 1.30489004]`；两者是同一标定会话的不同消费格式。Res1F 按项目约束只读取 JSON，Legacy 按旧实现读取 YAML。

全量运行完成后，该输入根目录在本机变为不存在。RawCaptureReplay 不包含删除输入的代码，且两轮 CSV 都已在目录消失前完整写出；后续复跑需要先恢复原始数据。本报告只使用已保存的两轮逐帧 CSV 和 metadata。

## 2. 公平口径

两边均通过 DSSI 的 `reconstruction_provider_replay.exe` 运行：

- 共用同一 OpenCV BMP loader、尺寸检查、逐帧计时和有效 depth 点计数。
- `processMs` 包含 provider adapter 和完整 depth/color/normal/quality 输出物化，不用 Res1F count-only 代替。
- scan mode 显式传 `metal`；Legacy provider 已修正为真实转发该标志。
- 每个 provider 单进程初始化一次，连续处理 295 帧；分位数排除 frame 0 warmup。
- 做两轮反向顺序：v1 为 Res1F-first，v2 为 Legacy-first，以识别 Windows 文件缓存对 load/wall 的影响。

Legacy 导出 `threeScan_init` 没有 config 参数，也不消费 Res1F 的 `ROF_CONFIG_PATH` 扩展。基准使用隔离工作目录 `output/07131555_architecture_battle/legacy_runtime_2607021545/config/reconsAlgPara.json`，其中 `calibParamsPath` 是目标 YAML 绝对路径。Legacy App 探针日志确认：

```text
[CalibLoad] path=D:/Data/Calib/2607021545_mach6/calibParams.yml
[AppStart] ... isMetalScan=true
```

这项 staging 是旧 ABI 的兼容措施，不代表 Legacy provider 已获得真正的显式 config 契约。

## 3. 主要结果

以下以缓存已预热且顺序反转的 v2 为主要性能口径。

| 指标 | Res1F | Legacy metal | 对比 |
|---|---:|---:|---:|
| 成功帧 | 295/295 | 295/295 | 相同 |
| warm process p50 | 28.7615 ms | 24.4501 ms | Res1F 慢 17.63% |
| warm process p90 | 31.2779 ms | 27.9776 ms | Res1F 慢 11.80% |
| warm process p99 | 35.1074 ms | 32.6854 ms | Res1F 慢 7.41% |
| warm load p50 | 6.4719 ms | 6.1556 ms | 同量级 |
| wall throughput | 26.874 FPS | 30.050 FPS | Res1F 为 89.43% |
| 总有效点 | 17,438,437 | 34,823,883 | Res1F 为 50.08% |
| 逐帧更快 | 24 | 271 | Legacy 占优 |
| 逐帧点数更多 | 0 | 295 | Legacy 每帧均更多 |

v1/v2 两轮逐帧 status 和点数 mismatch 均为 0，说明输出在本次运行中确定。两轮延迟范围：

| Provider | p50 | p90 | p99 |
|---|---:|---:|---:|
| Res1F | 27.4149 - 28.7615 ms | 30.4551 - 31.2779 ms | 35.1074 - 37.8691 ms |
| Legacy | 24.4501 - 24.4750 ms | 27.9776 - 28.1478 ms | 32.6674 - 32.6854 ms |

v1 先跑 Res1F 时，其 cold-cache load mean 为 27.33 ms，而后跑 Legacy 为 6.53 ms，因此不使用 v1 wall/FPS 排名。v2 两边 load 已收敛到 6-7 ms。

## 4. 点数控制组

Legacy non-metal 控制组同样 `295/295` 成功，总点数 `27,662,610`：

- Legacy metal/non-metal 点数比：`1.258879`；metal 模式使每一帧点数增加。
- Res1F/Legacy non-metal 点数比：`0.630397`。
- Res1F/Legacy metal 点数比：`0.500761`。

因此“Res1F 没有实现 metal 语义”只能解释部分差距。Res1F 还包含更严格 matching、point-cloud filter 和不同 rectification 消费路径。

差距并非均匀缩放：逐帧 ratio 中位数约 `0.4716`，最高 `0.9531`；frame 197 仅 `10 vs 49,395` 点却双方都返回成功。当前 Res1F 只有零点才返回 `ReconstructionInsufficient`，状态语义对近空帧过宽。

点数不等于几何质量。本轮没有保存 295 帧完整 PLY，也没有做最近邻、平面误差、跨帧支持或 SLAM 融合比较，因此不能仅凭点数宣称 Legacy 几何更准确；但 Res1F 密度和状态门禁显然未达到“功能等价”的证明标准。

## 5. 架构性能诊断

Res1F count-only 连续 frame 0/1/2 显示：

- point-cloud `deviceAllocationCount=13,0,0`，steady-state device allocation 已消除；
- frame 1 point-cloud kernel `0.7857 ms`，H2D `0.1395 ms`；
- frame 1 总 `runElapsedMs=9.1858`，远低于 provider 完整输出约 28 ms。

在全量 provider v2 中，Res1F 内部 engine elapsed 到 provider `processMs` 的额外开销为 mean `2.8596 ms`、p90 `3.9126 ms`、p99 `4.4104 ms`。剩余差距主要来自完整结果物化、stage host 往返和 caller-owned output copy，而不是 point-cloud kernel 或 warmup 后 `cudaMalloc`。

当前结论：

1. 新 ABI/Provider 架构可稳定运行，但尚未转化为性能领先。
2. `p99=35.1074 ms` 未通过架构文档的 `<=30 ms` 目标。
3. Phase 3 下一步应做 device view 串联和 output lease/直写，不应继续微调已经亚毫秒的点云 kernel。
4. Res1F 必须显式协商 metal capability；未支持时应 fail closed，不能静默忽略。
5. 近空点帧应返回 `ReconstructionInsufficient` 或更具体的质量状态。

## 6. 复现命令

```powershell
# 双向顺序全量运行
./scripts/run_07131555_architecture_battle.ps1 `
  -FirstFrame 0 -LastFrame 294 -RunLabel full_0_294_v1 `
  -ScanMode metal -ProviderOrder res1f-first

./scripts/run_07131555_architecture_battle.ps1 `
  -FirstFrame 0 -LastFrame 294 -RunLabel full_0_294_v2 `
  -ScanMode metal -ProviderOrder legacy-first

# 统计
./scripts/summarize_07131555_architecture_battle.ps1 -RunLabel full_0_294_v1
./scripts/summarize_07131555_architecture_battle.ps1 -RunLabel full_0_294_v2
```

主要产物位于 `output/07131555_architecture_battle/`：

- `res1f_full_0_294_v1.csv` / `legacy_full_0_294_v1.csv`
- `res1f_full_0_294_v2.csv` / `legacy_full_0_294_v2.csv`
- `run_metadata_full_0_294_v1.json` / `run_metadata_full_0_294_v2.json`
- `summary_full_0_294_v1.json` / `summary_full_0_294_v2.json`
- `legacy_full_0_294_nonmetal_control.csv`
