# Legacy 大比拼：output260707 gypsum 实施计划

> 当前契约（2026-07-13）：下列 Res1F 命令使用 `calibResult.json`；YAML 标定只由 Legacy runtime 消费。结果摘要保留当时历史运行数据。

## 目标

使用 `D:\Data\output260707_gypsum\Upper\<frame>\SourceImg` 真实输入，与同目录 Legacy `0.exr` 输出进行点数和效率对比。

验收口径：

- 新算法重建点数不能明显少于 Legacy：先以单帧 `ourPointCount / legacyPointCount >= 0.95` 作为告警线。
- 效率不能输：当前数据目录没有 Legacy 单帧重建耗时日志，因此先记录新算法 `runElapsedMs`，同时继续查找可直接比较的 Legacy 热路径耗时证据。
- 如果点数或耗时落后，优先诊断输入映射、配置相移步数、标定选择、过滤开关、CUDA 分配/拷贝和质量统计成本。

## 数据结构发现

- `D:\Data\output260707_gypsum\Upper\SourceImg` 不存在。
- 实际输入为 `D:\Data\output260707_gypsum\Upper\<frame>\SourceImg\L0.bmp..L17.bmp` 和 `R0.bmp..R17.bmp`。
- Legacy 点云输出为 `D:\Data\output260707_gypsum\Upper\<frame>\0.exr`，格式为 `400x424 CV_32FC3` XYZ。
- 旧 `singleStripe` loader 使用 `L\1.bmp` 起始；本轮 SourceImg loader 使用等价映射：`projectorIndex=1 -> L0.bmp/R0.bmp`。

## 实施步骤

| 步骤 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 确认工作区、技能、规划文档和当前代码入口 |
| 2 | complete | 新增 SourceImg 帧目录 loader 与 `reconstructSample --source-img-root --frame` |
| 3 | complete | 增加 sample `runElapsedMs` 与 stage `elapsedMs` 输出 |
| 4 | complete | 新增 `scripts/benchmark_output260707_gypsum.py`，统计 Legacy EXR 点数、新算法点数和耗时 |
| 5 | complete | 构建、CTest、真实帧 smoke、抽样 benchmark |
| 6 | complete | 扩大到全量 466 帧，补充 direct legacy app 耗时对比并记录风险 |

## 结果摘要

- 全量 `0..465` 已跑完；可比帧为 447 帧，Legacy `0.exr` 有效点数为 0 的尾部帧为 `447..465`。
- 可比 447 帧中没有点数低于 Legacy 95% 的帧；最低比例为 frame 362：`146645 / 149971 = 0.977822`。
- 可比 447 帧总点数：新算法 `58,747,416`，Legacy `50,345,889`，总比例 `1.166876`。
- 抽样 `100..104` 点数：新算法总点数为 Legacy 的 `1.109275`，单帧比例范围 `1.095372..1.133006`。
- 同进程批量 `100..104` 热路径耗时：新算法 median `56.35ms`、mean `69.93ms`；Legacy `TeethScanAlgorithmApp` median `434.659ms`、mean `439.985ms`。

## 风险与解释

- `scripts/benchmark_output260707_gypsum.py` 默认逐帧启动 sample，适合点数全量统计；效率结论应优先看 `reconstructSample --first --last` 的同进程批量结果。
- frame `447..465` 的 Legacy `0.exr` 为 0 点；用修正标定路径重跑 Legacy frame 447 仍为 0 点。新算法这些帧会输出低质量点云，例如 frame 447 输出 `85256` 点但 `frameQualityScore=0.171903`、`boundaryRatio=0.903807`，说明质量模块已识别为低质量，但当前重建模块没有按整帧质量硬拒绝。
- 当前效率优势来自批量复用进程/engine 后的热路径。首帧包含 CUDA 初始化/首次 kernel 开销，frame 100 新算法 `124.431ms`，后续 frame `101..104` 稳定约 `56ms`。

## 验证命令

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibResult.json --source-img-root D:\Data\output260707_gypsum\Upper --frame 100
python scripts\benchmark_output260707_gypsum.py --frames 100,200,300,400
python scripts\benchmark_output260707_gypsum.py --frames 0:465 --output build\big_battle_output260707_gypsum_full
build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibResult.json --source-img-root D:\Data\output260707_gypsum\Upper --first 100 --last 104
```
