# reconstructOneFrame Legacy 重构概要设计任务计划

## 目标

在 `D:\code\reconstructOneFrame` 中形成 Legacy 单帧重构链路的模块化概要设计文档，覆盖参数读取、日志、图像检查、相位、匹配、三维点/法向、AI 牙龈分割和单帧质量评定，并遵循 `agent.md` 的模块边界、CUDA 优先、数据契约、状态码和验证要求。

## 阶段

| 阶段 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 读取技能要求、`agent.md`、当前仓库结构 |
| 2 | complete | 抽样检查 Legacy 核心接口、配置、日志与 `D:\code\slam` 参考 |
| 3 | complete | 写入规划文件和概要设计文档 |
| 4 | complete | 自检文档章节、模块图、状态码和验证口径 |
| 5 | complete | 根据评审意见修订概要设计：sample 入口、日志、外部标定 JSON、接口命名、配置沿用、条纹数组、校正画布和 DSSI 适配 |
| 6 | complete | 写出第一阶段基础骨架执行计划供评审 |

## 当前决策

- 本轮只形成概要设计文档，不进入代码实现。
- Legacy 作为行为基线和功能覆盖清单，不作为新架构约束。
- `D:\code\slam` 的参数读取和日志模式仅作为工程模式参考：JSON 覆盖默认参数、初始化时打印配置、日志集中封装、按时间命名和轮转。
- 2026-07-09 评审后收敛：日志直接仿照 `D:\code\slam` 引入第三方 Log/spdlog，默认 `info` 等级；算法配置先沿用 `config/reconsAlgPara.json`；标定由 `D:\code\manufacturing-process-monitoring-system` 产生，本项目读取 `calibResult.json`。
- 规划文件固定放在 `docs/planning/`。

## 错误记录

| 错误 | 处理 |
|---|---|
| 初次扫描 `src` / `CMakeLists.txt` 返回不存在 | 当前重构仓库尚未展开主体源码，改为检查 `legacy/teethscanalgorithm3x4` 和外部 Legacy 基线 |

## 第一阶段基础骨架实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 7 | complete | 建立 CMake 工程骨架、公共 C++ API、配置读取、日志、标定读取骨架、图像校验、pipeline dry-run、sample 和最小测试 |
| 8 | complete | 执行 Release 构建、sample smoke、CTest Release 验证，并记录 VS 多配置下 `ctest` 需要 `-C Release` 的差异 |

## 第一阶段实施错误记录

| 错误 | 处理 |
|---|---|
| `JsonFile.cpp` raw string 正则在 MSVC 下出现“常量中有换行符” | 改用带自定义分隔符的 raw string literal |
| `std::sregex_iterator` 不能接收临时 `std::regex` | 将 number regex 提升为局部变量，避免绑定临时对象 |
| sample/tests 找不到 `src` 内部头文件 | 给 sample 和测试目标补充 private include path |
| `ctest --test-dir build --output-on-failure` 在 VS 多配置生成器下 Not Run | 使用 `ctest --test-dir build -C Release --output-on-failure` 完成等价 Release 测试验证 |

## 第二阶段计划

| 阶段 | 状态 | 内容 |
|---|---|---|
| 9 | complete | 读取第二阶段规划技能、第一阶段文档、当前接口和源码骨架 |
| 10 | complete | 写出第二阶段输入、标定与预处理契约实施计划 |

## 第二阶段当前决策

- 第二阶段优先补齐算法前置闸门：输入契约、标定 JSON 解析、预处理 dry-run 和诊断 summary。
- 第二阶段仍不迁移相位、匹配、重建、AI 核心算法，不接入 CUDA/TensorRT，不修改 DSSI。
- 第二阶段计划文档：`docs/planning/2026-07-09-phase2-input-calibration-preprocess-implementation-plan.md`。

## 第二阶段实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 11 | complete | 扩展状态码、统计字段和配置输入计划契约 |
| 12 | complete | 实现 InputManifest、CalibrationJsonReader、ImagePreprocessor dry-run 和 DiagnosticSummary |
| 13 | complete | 接入 pipeline/sample，并补充第二阶段测试数据与测试用例 |
| 14 | complete | 执行 Release 构建、sample smoke 和 8 项 CTest 验证 |

## 第二阶段实施错误记录

| 错误 | 处理 |
|---|---|
| 第一阶段 `pipeline_smoke_test` 输入不足导致覆盖校验失败 | 将内置 synthetic frame 扩展为 3 频率 x 5 相移左右条纹 |
| `image_preprocess_dry_run_test` 在 CTest 下找不到 `tests/data` | 给该测试设置 `WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}` |
| validator 内部重写 `stageName` 导致 pipeline 阶段名不稳定 | pipeline 调用 validator 后恢复 `input_contract_validation` 阶段名 |

## 第三阶段计划

| 阶段 | 状态 | 内容 |
|---|---|---|
| 15 | complete | 建立第三阶段真实数据 CUDA 包裹相位实现计划 |
| 16 | complete | 扩展状态码、FrameResult 和诊断 summary |
| 17 | complete | 新增 WrappedPhaseComputer CUDA 计算与低调制检查 |
| 18 | complete | 接入真实 singleStripe BMP 输入、pipeline/sample，并补充第三阶段测试 |
| 19 | complete | 执行 Release 构建、manifest smoke、真实 singleStripe CUDA smoke 和 9 项 CTest 验证 |

## 第三阶段当前决策

- 第三阶段建立真实 BMP 输入与 CUDA 包裹相位诊断闸门，不迁移 Legacy unwrap、匹配、重建或 AI 实现。
- `phaseStepCounts` 表达每个频率独立相移步数；为空时才回退到 Legacy 全局 `STEP`。
- `wrapped_phase_compute_cuda` 成功只代表 CUDA 包裹相位阶段可计算；`unwrap/matching/reconstruction` 仍以 `NotComputed` 明确标记。
- 低调制输入返回 `PhaseQualityInsufficient`，不输出假 depth/normal/quality。
- 第三阶段计划文档：`docs/planning/2026-07-09-phase3-wrapped-phase-reference-implementation-plan.md`。

## 第四阶段实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 20 | complete | 调研 Legacy `GPUPhaseUnwrapper` 两级 CUDA unwrap 公式 |
| 21 | complete | 扩展 unwrap 配置读取和公共 `FrameResult` 标记 |
| 22 | complete | 新增 `PhaseUnwrapperCuda`，实现 `PH23/PH123 -> abs23 -> high-frequency abs` |
| 23 | complete | 接入 pipeline/sample/diagnostic summary，并新增 `phase_unwrapper_test` |
| 24 | complete | 执行 Release 构建、10 项 CTest、manifest smoke 和真实 `singleStripe` CUDA unwrap smoke |

## 第四阶段当前决策

- 第四阶段迁移 Legacy two-stage CUDA unwrap 数学公式，形成 `phase_unwrap_cuda` 阶段。
- 第四阶段仍不实现左右匹配、视差、三维点云、法向、质量图、TensorRT 或 AI。
- `phaseStepCounts=[3,5,5]` 是当前真实数据输入契约；最高频/低频相移步数可以不同，不能改回全局固定 `STEP`。
- 真实数据验证路径固定为 `D:\Data\Calib\2607011016_mach6\singleStripe`，本阶段已验证 group 1。
- 第四阶段计划文档：`docs/planning/2026-07-09-phase4-cuda-phase-unwrapping-implementation-plan.md`。

## 第五阶段实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 25 | complete | 扩展 OpenCV matrix JSON/YAML 标定读取和重建配置字段 |
| 26 | complete | 新增 CUDA 点云重建、PLY 输出和 Legacy 点云对比模块 |
| 27 | complete | 接入 pipeline/sample 的 `--output` 与 `--compare-legacy` |
| 28 | complete | 用真实 `singleStripe` group 1 输出点云并对比历史 Legacy `depth_points.ply` |
| 29 | complete | 根据差异修正 rectification/remap、过滤默认值和 Legacy 标定矩阵来源 |
| 30 | complete | 执行 Release 构建、11 项 CTest 和真实 Legacy comparison 验证 |

## 第五阶段当前决策

- 第五阶段进入真实 CUDA 点云重建：`phase_unwrap_cuda -> point_cloud_reconstruct_cuda -> ply_output -> legacy_point_cloud_compare`。
- `D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json` 未提供 `phaseStepCounts`，因此按 Legacy `STEP=5` 回退读取 30 张输入；项目默认配置仍保留每频率独立步数契约。
- 历史 Legacy 输出最贴近 `D:\Data\Calib\2607011016_mach6\calibParams.yml` 的矩阵集合；sample 已支持直接用该 YAML 作为 `--calib`。
- 默认关闭点云 smoothing/filter，用 raw CUDA reproject 结果做公平 Legacy 对比；过滤/平滑会显著改变点数。
- 第五阶段计划文档：`docs/planning/2026-07-09-phase5-point-cloud-legacy-comparison-implementation-plan.md`。

## 第五阶段实施错误记录

| 错误 | 处理 |
|---|---|
| 首次点云过滤后仅剩约 5k 点，远低于 Legacy 101k 点 | 确认为 post-filter 过强，默认关闭 point cloud filter/smoothing，用 raw reproject 作为 Legacy 对比基线 |
| 未做 rectification/remap 时点数接近但 Z/几何偏差明显 | 对照 Legacy `GPUPhaseUnwrapper`，补入 phase remap/rectification 近似后最近邻 RMS 降至约 0.16mm |
| `calibResult.json` 结果仍不如历史 Legacy 点云贴近 | 读取 Legacy `calibParams.yml` 的 R/P/Q 矩阵后最近邻 RMS 降至 0.0626mm |
| MSVC 不支持 `std::regex::multiline` | 将 YAML 正则改为 `(^|\n)` 锚点匹配，构建恢复通过 |

## 第六阶段实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 31 | complete | 调研 Legacy `point_reliability_map` 点级 reason bits 和整帧质量公式 |
| 32 | complete | 新增 `PointReliability` 质量模块，迁入 coverage、center ROI、连通性、percentile 和 frame score 口径 |
| 33 | complete | 扩展点云重建结果，保留每像素 grid point、match cost、candidate count 和 high-frequency modulation |
| 34 | complete | 扩展配置、公共 `FrameResult` 和 diagnostic summary，输出 `qualitySummary` |
| 35 | complete | 接入 pipeline `quality_evaluate` 阶段，并新增 `point_reliability_test` |
| 36 | complete | 执行 Release 构建、12 项 CTest、真实 group 1 quality smoke |

## 第六阶段当前决策

- 单帧质量评价以 Legacy `point_reliability_map` 为主参考：点级 `score/reasonBits`，整帧 `coverage/centerCoverage/connectedSurface/pointQuality/frameQuality`。
- 第六阶段不接入 TensorRT/AI，因此真实数据不产生 semantic background reason；该 reason bit 已保留给后续 AI mask。
- 质量评价当前是 CPU 汇总模块，输入来自第五阶段 CUDA 点云 grid；点云热路径仍保持 CUDA。
- pipeline 总是执行 `quality_evaluate`，即使历史 config 中 `qualityMapEnabled=false`，因为第六阶段目标是建立质量评价能力。
- 第六阶段计划文档：`docs/planning/2026-07-09-phase6-quality-evaluation-implementation-plan.md`。

## 第六阶段实施错误记录

| 错误 | 处理 |
|---|---|
| 当前点云输出只有 compact vertices，无法判断洞边缘和覆盖率 | 扩展 `PointCloudReconstructionResult::gridPoints`，保留每像素点云和匹配诊断 |
| Legacy 质量图依赖 OpenCV `cv::Mat` | 新模块改用标准库 vector/map/stats，不引入 OpenCV 依赖 |
| 历史配置可能关闭 `qualityMapEnabled` | 为满足第六阶段目标，pipeline 仍执行质量评价；配置字段继续读取，作为后续导出/开关策略依据 |

## Legacy 大比拼：output260707 gypsum

| 阶段 | 状态 | 内容 |
|---|---|---|
| 37 | complete | 支持 `D:\Data\output260707_gypsum\Upper\<frame>\SourceImg` 真实输入并与 Legacy `0.exr` 点数/效率对比 |

## Legacy 大比拼当前决策

- 输入根目录为 `D:\Data\output260707_gypsum\Upper`，每帧数据位于 `<frame>\SourceImg\L0.bmp..L17.bmp` / `R0.bmp..R17.bmp`。
- SourceImg loader 使用 `projectorIndex=1 -> L0/R0` 的 zero-based 映射，保持与旧 `singleStripe` loader 的 `L\1.bmp` 语义等价。
- 当前数据目录未发现 Legacy 单帧重建耗时日志；先用 Legacy `0.exr` 统计点数，用新算法 `runElapsedMs` 统计热路径耗时，并继续寻找可比较 Legacy 运行证据。
- 计划文档：`docs/planning/2026-07-09-legacy-big-battle-output260707-gypsum-plan.md`。

## Legacy 大比拼结果

- 全量 `0..465` 跑完；可比帧 447 帧，Legacy 零点尾帧为 `447..465`。
- 可比帧点数未输：`point_count_losing_frames=0`，总点数比例 `1.166876`，最低单帧比例 `0.977822`。
- 同进程批量 `100..104` 效率未输：新算法 median `56.35ms`、mean `69.93ms`；Legacy app median `434.659ms`、mean `439.985ms`。
- 发现的自身风险：Legacy 零点尾帧上新算法仍输出低质量点云；frame 447 的质量分 `0.171903`，后续需要明确是否由质量模块触发整帧 reject，而不是只输出质量摘要。

## 2026-07-10 Legacy 差距修复计划

| 阶段 | 状态 | 内容 |
|---|---|---|
| 38 | complete | 复核全量点数分布、性能口径和 sparse/zero 帧行为，形成差距判断 |
| 39 | pending | 修正 benchmark 指标命名、质量特征导出和 warm/cold 性能口径 |
| 40 | pending | 独立验证 Legacy sparse/zero 区域的 extra-only 点，区分有效提升和无支持点 |
| 41 | pending | 增加 Legacy XYZ EXR 全量几何比较和代表帧误差报告 |
| 42 | pending | 按几何证据迁移 exact intensity remap、matching validity 和洞规则 |
| 43 | pending | 执行 466 帧正确性与效率回归，评估 CUDA workspace 优化 |

## Legacy 差距修复当前决策

- 正常可比帧的点数下限和热路径效率没有重大落后。
- 当前核心待证是 Legacy sparse/zero 帧上新算法输出的 extra-only 点是否真实有效，不能仅按 Legacy 点数定性。
- `point_count_losing_frames=0` 只代表没有低于 Legacy 95% 的帧；实际有 11 帧点数少于 Legacy。
- 下一步先修 benchmark 并验证 extra-only 点的相位、匹配、局部几何和跨帧支持，再决定是否需要 reject。
- 修复计划：`docs/planning/2026-07-10-legacy-big-battle-remediation-plan.md`。

## 2026-07-10 新算法点云盯帧

| 阶段 | 状态 | 内容 |
|---|---|---|
| 44 | complete | 增加批量重建按帧子目录 PLY 输出，扩展 `exr_triplet_viewer` 支持 ASCII PLY，并生成 `0..465` 全量盯帧数据 |

## 点云盯帧当前决策

- 新算法盯帧根目录固定为 `build/watch_output260707_gypsum/<frame>/depth_points.ply`。
- `D:\tools\exr_triplet_viewer` 当前直接读取上述 PLY，不再读取本次 Legacy `0.exr`。
- 查看器使用固定完整 bbox 和 `z=[80,160]` 高度着色，保留异常深度分布，不通过裁剪掩盖问题。
- 首帧、尾帧及 Legacy zero 区域抽查均出现相同的 `Z=83.7906..153.6479mm` 上下界，后续修复应优先检查 disparity/matching validity 与深度边界钳制。

## 2026-07-10 远程 Legacy 错误相位匹配调研

| 阶段 | 状态 | 内容 |
|---|---|---|
| 45 | complete | 确认当前工作区边界、远程仓库和相位匹配相关分支候选 |
| 46 | complete | 追踪 `origin/zheng/ParallaxMatching` 提交谱系并提炼生产核心约束 |
| 47 | complete | 对照当前 CUDA 匹配实现，识别尚未迁移或默认值不一致的约束 |
| 48 | complete | 更新调研结论、消融优先级和后续迁移建议 |

## 远程 Legacy 调研当前边界

- 本轮目标是学习远程 Legacy 中针对错误相位匹配的核心改动，不直接修改当前算法。
- 外部 Legacy 工作区存在既有修改，只允许读取提交、分支和源码，不清理或覆盖。
- 当前远程相关候选包括 `origin/zheng/ParallaxMatching`、`origin/zheng/denoise` 和 `origin/v1.0.0`；优先追踪 `ParallaxMatching` 的提交谱系及其是否已合入 `origin/main`。
- 已确认 `origin/zheng/ParallaxMatching@52bda09` 是 `origin/main@5410c14` 的祖先；后续以最新 main 的 CUDA 实现和生产 JSON 默认值为准，而不是把该分支视为未合并实验。
- 最核心迁移顺序已收敛为：右相位单调穿越 -> 生产默认启用左右一致性/亚像素 -> 异常帧单变量消融；candidate quality/uniqueness 仅在核心约束仍不足时继续验证。
- 本轮只完成远程代码学习和规划记录更新，没有修改当前匹配算法。

## 2026-07-10 错误相位匹配核心约束实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 49 | complete | 核对 Legacy 精确函数、当前配置接口和可复用测试入口 |
| 50 | complete | 实现右相位单调穿越和亚像素左右一致性反查 |
| 51 | complete | 增加配置字段、生产默认值、诊断计数和回归测试 |
| 52 | complete | 执行 Release 构建、CTest 和 frame 0/100/447 单变量验证 |
| 53 | complete | 记录点数、质量、性能变化和后续风险 |

## 本轮实施决策

- 直接在当前已获用户授权的工作区继续实施，不创建会丢失现有未提交阶段代码的独立 worktree。
- 匹配有效性约束必须发生在 disparity 写入前，不通过最终点云过滤或整帧质量 reject 代替。
- 生产默认组合对齐最新 Legacy：物理视差窗、左右一致性、右相位单调穿越、亚像素启用；uniqueness、candidate quality、row DP 和 local consistency 保持关闭。
- 验证必须至少覆盖合成平坦相位拒绝、正确单调穿越保留、亚像素反查容差，以及真实 frame `0/100/447`。

## 本轮实施错误记录

| 错误 | 处理 |
|---|---|
| 首次 CTest 的 `point_cloud_reconstructor_test` 失败于“正确穿越场景单调拒绝计数必须为 0” | 实测保留 112 点、拒绝 8 点，正好每行一个缺少外侧邻点的边界候选；改为验证只拒绝该边界 |
| 首次启动审查代理同时指定 `fork_context=true` 和 reviewer 类型被拒绝 | 改为继承当前上下文，不显式指定 agent type，审查任务成功启动 |
| 结构影响分析未识别 CUDA/C++ 函数级变化 | 不采用其 0 风险结论，改用聚焦 `git diff`、编译、测试和真实数据证据审查 |
| 独立审查代理连续两次 60 秒等待未返回 | 不继续空等，收窄到 CUDA 核心和配置并要求立即输出高风险问题；本地审查与验证照常完成 |
| 首次搜索 Legacy App CLI 时工作目录仍为 `reconstructOneFrame` | 搜索结果属于新 sample，已丢弃；改到 `D:\code\teethscanalgorithm3x4` 重新定位 |
| 新算法 filter 消融脚本把 `ReconstructionInsufficient` 的退出码 1 当作工具失败 | 该状态是 frame 0 被过滤为 0 点的有效结果；改为记录退出码并继续其他帧/组合 |
| `smooth_filter.json` 因前一轮脚本在 frame 0 提前退出而未生成 | 从原 monotonic 配置重新生成该变体，再单独运行 frame 0/100 |

## 2026-07-10 Legacy 逐阶段乱点根因追踪

| 阶段 | 状态 | 内容 |
|---|---|---|
| 54 | complete | 确认 Legacy 工作区、历史输出运行配置和真实执行入口 |
| 55 | complete | 逐行追踪输入 remap、包裹相位、展开相位与有效 mask |
| 56 | complete | 逐行追踪匹配、视差、三角化、点过滤与质量判定 |
| 57 | complete | 对 frame 0/100 运行 Legacy 并收集可用阶段诊断 |
| 58 | complete | 对照新实现形成乱点根因排序和下一修复方案 |

## 本轮追踪边界

- 本轮先做证据驱动的 Legacy 数据流追踪，不直接修改新算法或 Legacy。
- 当前 Legacy 工作区为干净的 `feature/single-frame-quality@27613a3`，最新远程主线为 `origin/main@5410c14`；必须分别说明历史运行链和最新主线差异。
- 重点解释 frame 0 几乎全乱点、frame 100 有物体但背景无效点较多时，Legacy 在每个阶段如何生成、标记和剔除数据。

## 2026-07-10 Legacy 最终点云收敛机制实施

| 阶段 | 状态 | 内容 |
|---|---|---|
| 59 | complete | 用回归测试锁定 Legacy 3x3 Z-only smoothing、历史权重和空洞语义 |
| 60 | complete | 替换当前 XYZ smoothing，并增加 raw/smoothed/filtered 阶段计数 |
| 61 | complete | 在生产配置中显式启用 point-cloud filter，保持缺 key 旧配置默认关闭 |
| 62 | complete | 执行 Release 构建、CTest 和 CUDA 合成回归 |
| 63 | complete | 用真实 frame 0/100/447 验证点数、Z 分布、质量和效率 |

## 本轮实施边界

- 用户已经批准按逐阶段根因分析继续实施，不重新设计或叠加无证据阈值。
- 只迁移 Legacy 固定的 3x3 Z-only smoothing 和 15x15/3x3 局部三维连续性过滤口径。
- 不用整帧质量分、PLY 后处理或更强 matching 阈值代替最终三维过滤。
- 缺少 point-cloud smoothing/filter JSON key 时继续保持关闭，生产配置通过显式 key 开启。
- 当前工作区包含尚未提交的阶段代码，继续原地实施，不创建丢失现场的独立 worktree，不提交、不推送。

## 本轮最终决策

- 生产配置采用 `pointCloudSmoothingEnabled=false`、`pointCloudFilterEnabled=true`。
- 保留 Legacy-compatible Z-only smoothing 实现和测试，但不作为生产默认；当前 raw 几何下启用 smoothing 会使 frame 100 从 filter-only `84,833` 点降到 `62,134` 点。
- point-cloud filter 内不重复应用 `minZ/maxZ`；Z gate 只在 reproject 阶段执行，filter 按 Legacy 只排除全零点。
- 最终对比口径统一为 filtered point cloud；raw/smoothed/filtered 点数通过 `pointCloudSummary` 明确输出。
- filter-only frame 100 对当前 Legacy final：`84,833 vs 89,900`，nearest P95 `0.2373mm`；Z P5/P50/P95 为 `106.3836/109.1723/111.3925mm`，Legacy 为 `106.6240/109.1080/111.1720mm`。

## 本轮错误记录

| 错误 | 处理 |
|---|---|
| 首次规划文件 `apply_patch` 调用传入空补丁 | 未产生文件修改；改用完整补丁并继续 |

## 2026-07-10 filter-only 全量 Legacy 再比拼

| 阶段 | 状态 | 内容 |
|---|---|---|
| 64 | complete | 核对新算法和 Legacy 批量 CLI、公平计时与配置口径 |
| 65 | complete | 用 filter-only 配置完成 `0..465` 全量点数与状态对比 |
| 66 | complete | 用新算法单进程批量模式统计稳定单帧耗时和 CUDA 阶段耗时 |
| 67 | complete | 用 Legacy App 相同帧段单进程模式统计稳定单帧耗时 |
| 68 | complete | 汇总点数覆盖、sparse/zero 帧、几何和效率结论 |
| 69 | complete | 更新规划文档并执行最终构建、测试和补丁审计 |

## 2026-07-10 彩色纹理矩阵集成

| 阶段 | 状态 | 内容 |
|---|---|---|
| 70 | complete | 追踪 Legacy RGB 辅助帧、整流、颜色矩阵、gamma 和点云绑定链路 |
| 71 | complete | 增加强类型颜色配置和 SourceImg/singleStripe RGB 辅助帧加载 |
| 72 | complete | 在完整物化路径实现 CUDA rectified color correction |
| 73 | complete | 增加配置、loader、按需颜色加载和点云颜色回归验证 |
| 74 | complete | 使用 Upper `0..465` 真实数据重新生成彩色点云 |
| 75 | complete | 汇总颜色统计、执行完整构建测试和补丁审计 |

## 彩色纹理实施边界

- 颜色输入固定使用左相机三张辅助灰度帧，按配置 projector index 映射。
- 颜色输出与 depth/normal 使用同一 rectified 像素坐标。
- 颜色矩阵和 gamma 参数化；旧配置缺 key 默认关闭。
- 颜色只在点云完整物化时计算，count-only 基准热路径不增加工作。
- 不修改 Legacy，不引入 AI texture mapper，不提交、不推送。

## 彩色纹理最终结果

- `colorTextureProjectorIndices=[16,17,18]` 对应 `SourceImg/L15.bmp/L16.bmp/L17.bmp`，合并顺序固定为 raw BGR。
- CUDA 完整物化路径执行左相机双线性整流、参数化 `3x4` 颜色矩阵和 `gamma=0.5`；无输出 count-only 路径不读取辅助 RGB，也不生成颜色。
- frame 100 保持 `84,833` 点，RGB 三通道完全相等比例由旧实现 `1.0` 降为 `0.0`，unique colors=`5,148`；Legacy 为 `6,164`。
- frame 100 新/Legacy mean RGB 分别为 `[253.656,205.767,182.559]` 和 `[253.208,203.734,180.411]`。
- Upper `0..465` 全量运行解析 `466` 帧，状态 `Ok=442`、`ReconstructionInsufficient=24`，生成 `442` 个彩色 PLY。
- 全量彩色运行总点数 `36,546,920`，与此前 filter-only 几何基线逐帧 mismatch=`0`。
- 全量输出：`build/color_texture_upper_20260710/full_v1/<frame>/depth_points.ply`。
- 汇总：`build/color_texture_upper_20260710/full_v1_summary.json`。

## 彩色纹理错误记录

| 错误 | 处理 |
|---|---|
| 首次派发实现代理时同时传入 full-history fork 和显式 agent type，参数冲突 | 去掉显式 agent type 后重新派发，未产生代码修改 |
| 首次 PLY header 脚本只识别 `LF`，对 `CRLF` header 误读到二进制/正文末尾并超时 | 改为逐行 `strip()==end_header`，重新读取成功 |
| 首次全量摘要脚本使用旧列名 `new_vertices` | 读取当前 CSV 表头后改用 `newPointCount`，摘要生成成功 |
| 首个规格审查代理未读取工作区，只返回准备动作 | 该结果作废，重新派发独立只读审查 |
| 规格审查发现 singleStripe `includeColor=false` 缺少测试 | 增加无颜色文件的 singleStripe phase-only 回归，复核 `PASS` |
| 代码质量审查发现 `includeColor=true` 默认值和直接构造非法 projector indices 风险 | 移除默认参数，强制调用方显式声明；两个 loader 索引前增加本地校验并补回归 |

## 彩色纹理剩余风险

- 损坏 BMP 的极端尺寸仍可能触发整数溢出或异常分配；正常设备数据不受影响，本轮不增加与主目标无关的重型防御。

## 本轮比拼口径

- 算法配置固定使用已验证的 filter-only 组合：物理视差窗、LR、右相位单调穿越、亚像素开启，point-cloud smoothing 关闭、filter 开启。
- 全量 `0..465` 正确性可逐帧进程运行，以便逐帧保存状态和 Legacy EXR 点数。
- 效率结论不使用逐帧进程总时间；新算法和 Legacy 均使用单进程连续帧段，并排除首帧初始化。
- 同时报告新算法 `runElapsedMs`、`point_cloud_reconstruct_cuda` 阶段耗时和外部进程 wall-clock，避免混淆口径。
- 不修改 Legacy 算法，不提交、不推送。

## 本轮性能定位

- filter-only 全量正确性结果：`466/466` 帧算法完成，Legacy 零点帧不再额外出点；总点数为 Legacy 的 `0.725917`。
- 公平单进程效率基线：新算法修改前稳定 `runElapsed` median `52.654ms`，Legacy 核心 elapsed median `9.744ms`。
- `qualityInfoEnabled=false` 时跳过 CPU 质量评价后，frame `101..104` 稳定 `runElapsed` median 降至约 `37.4ms`，点数逐帧不变。
- 点云 CUDA 内部分段计时表明：kernel median `0.482ms`、device allocation median `0.281ms`，CPU materialize median `15.270ms`，当前点云阶段首要瓶颈不是 CUDA kernel 或 `cudaMalloc`，而是质量关闭后仍执行的主机诊断物化。
- 当前单变量优化：质量关闭时不复制 match score/candidate count，不构造全尺寸 `gridPoints`，不逐像素计算 high-frequency modulation；点云 vertices 和点数输出保持不变。

## 本轮最终比拼结论

- BMP loader 改为固定头读取、单次像素块读取和 thread-local buffer 复用后，稳定 `loadElapsedMs` median/p90 为 `5.1058/5.8168ms`，旧实现 median 为 `21.079ms`。
- 新算法优化后 `runElapsedMs` median/p90 为 `9.1875/10.3028ms`，Legacy 核心为 `9.744/10.671ms`；核心热路径分别快约 `6.1%/3.6%`。
- 新算法 `0..465` wall 为 `6.855s`、`67.979 FPS`；Legacy 无输出 wall 为 `11.377s`、`40.960 FPS`，新算法端到端吞吐为 `1.660x`。
- 466 帧点数与优化前 filter-only 基线逐帧完全一致，状态保持 `Ok=442`、`ReconstructionInsufficient=24`。
- 使用同一个当前 Legacy 二进制重新保存 final 输出后，新算法总点数为当前 Legacy 的 `0.904286`，逐帧 ratio median 为 `0.905275`；没有任何 Legacy 零点帧被新算法错误出点。
- 新算法为零而当前 Legacy 仅有少量点的帧为 `0/1/2/445/446`，对应 Legacy 点数 `29/73/464/24/21`，属于低支持边界帧。
- 历史 `0.exr` 口径的 `0.725917` 来自不同运行版本，只保留为历史旁证，不作为当前公平点数结论。
- 当前新算法开启 LR、右相位单调穿越和亚像素，效率 Legacy 配置关闭这些约束；因此约 `9.6%` 的点数差不能直接解释为重建能力落后。
- frame 100 完整物化路径保持 `84,833` 点，当前 Legacy 为 `89,900` 点，nearest mean/RMS/P95 为 `0.0698/0.2090/0.2373mm`。
- 最终 Release 构建成功，CTest `10/10` 通过，`git diff --check` 通过；未提交、未推送、未修改 Legacy 仓库。

## 本轮性能定位错误记录

| 错误 | 处理 |
|---|---|
| 使用 `nvprof` 尝试 profile 时进程以 `-1073741515` 退出且没有生成 profile | 不重复使用已失败的 profiler 路径，改用函数内 `std::chrono` 分段计时定位 allocation/H2D/kernel/D2H/materialize |
| count-only 首次编译时条件分配分支引用了声明前的 `status` | 将 `Status status` 提前到统一分配区；该次构建失败后运行的是旧二进制，相关输出不计入优化结果 |
| wrapped modulation reduction 首次编译找不到 `CUDART_INF_F` | 补充 CUDA `math_constants.h`，未运行失败构建产物 |
| 首次按假定路径查找 Legacy `build\Release` 可执行文件失败 | 通过递归定位确认实际路径为 `build\bin\Release\TeethScanAlgorithmApp.exe`，未修改 Legacy |
