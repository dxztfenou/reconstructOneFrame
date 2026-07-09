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
