# Legacy 重构调研发现

## 当前仓库状态

- `D:\code\reconstructOneFrame` 当前只有 `agent.md`、`.gitignore`、`logs/` 与 `legacy/`，尚未建立新源码主体。
- `legacy/teethscanalgorithm3x4` 是本仓库内置 Legacy 基线，包含 `include/`、`src/`、`config/reconsAlgPara.json`、`model/model.trt`、测试与诊断工具。

## Legacy 关键事实

- `include/configs.h` 的 `Config` 把标定路径、图像尺寸、频率、相位、匹配、重建、质量、AI、调试输出等参数混在一个结构中。
- `Reconstruct::init` / `Reconstruct::reconstructOne` 是主链路入口；`reconstructOne` 同时承担图像加载、相位、匹配、点云、法向、AI 分割、质量信息、输出保存等编排职责。
- `GPUPhaseUnwrapper` 已包含 GPU 图像上传、remap、三角求和、包裹相位、展开相位、直解法调试图、高频调制/光照标志等能力。
- `computePointCloudFromPhase` 负责相位到点云/法向/质量图，接口参数过长，是新设计中拆分匹配、三角化、过滤、法向和质量诊断的重点。
- `TemporalSequenceValidator` 与 `ProjectorDefocusQuality` 已经提供连续帧输入质量、相位顺序、离焦质量等 C API，可吸收为新图像处理/质量评定模块的诊断能力。
- `DualStreamPPLiteSeg` 使用 TensorRT + CUDA stream 做单/双图分割，应作为 AI 牙龈分割模块的独立推理后端，而不是嵌入重建主流程。
- Legacy 的 `teeth_log::ScopedLogSession` 已经把第三方 `Log.h` 隔离到适配层，并输出到 `logs/log_<tag>_<timestamp>.log`。

## D:\code\slam 参考点

- `SlamSample::InitializeLog` 使用时间戳日志名、最多保留 10 个日志文件、基于 aftercure Log / spdlog 设置日志等级。
- `SlamInterface::Init` 先设置默认参数，再从 JSON 字符串覆盖，并可打印当前参数。
- `ParamsJson` 提供 `LoadAlgParamsFromFile`、`LoadAlgParamsFromJson`、`LoadAlgParamsFromString` 三类入口，适合重构项目复用这种“文件 / JSON 对象 / 字符串”三入口模式。
- `slam` 文档明确所有算法参数集中在 `algPara.json`，并按 `common/fusion/track/realTimeReg` 分节；新项目应使用类似分节，但改成单帧重构领域的 `runtime/calibration/image/phase/matching/reconstruction/ai/quality/diagnostics/logging`。

## 设计边界

- 新设计优先 CUDA 热路径：相位、unwrap、匹配、重建、mask/质量图和批量像素操作不应默认回退 CPU。
- 生产路径默认少日志、少拷贝、少分配；诊断图、阶段产物、质量直方图和 CPU 参考输出默认关闭。
- 状态码必须区分输入、配置、标定、CUDA、质量、相位、匹配、重建、AI、输出等失败原因。

## 2026-07-09 评审修订结论

- 外部调用用例放在 `src/sample/reconstructSample.cpp`，不使用 `src/app/` 作为核心入口。
- 日志层直接参考 `D:\code\slam` 的第三方 Log/spdlog 接入方式，默认 `info` 等级。
- 本项目不做标定计算；`calibration_model` 只读取 `D:\code\manufacturing-process-monitoring-system` 的 `calibResult.json` 并派生几何运行对象，`calibParams.yml` 仅作为 Legacy 兼容产物。
- 公共头文件命名为 `include/reconstruct_one_frame/reconstructInterface.h`。
- 算法配置第一阶段直接沿用 `D:\code\teethscanalgorithm3x4\config\reconsAlgPara.json` 的文件名和主要字段，不新增 default/schema 作为前置条件。
- 输入契约改为条纹图数组 `std::vector<StripeImage>`，不在 `StripeFrameGroup` 中固定全局 `freqCount` / `stepCount`。
- 校正策略参数化为 `none`、`rectifyKeepInputSize`、`rectifyExpandedCanvas`，默认设计优先保留输入信息，是否保持原始画布由下游接口和性能预算决定。
- DSSI 可同步改造：短期保留 `threeScan_*` wrapper 兼容热加载，长期应转向可链接的 `reconstructInterface.h` 或新的 `ReconstructOneFrameLoader`。

## 2026-07-09 第一阶段实施发现

- `D:\code\slam\SlamSample\SlamSample.cpp` 的日志参考点为 `InitializeLog(LogLevel::Info, 10, logTag)`，生成 `logs/log_<tag>_<YYYY-MM-DD_HH-MM-SS>.log`，并保留最近 10 个 `log_*.log`。
- 第一阶段当前实现采用标准库文件日志封装来保持本仓库独立可编译；接口和行为按 Slam 日志口径对齐。若后续要求严格链接第三方 aftercure Log/spdlog，需要补充 thirdparty 路径和 CMake 链接配置。
- Legacy `reconsAlgPara.json` 已复制到新项目 `config/reconsAlgPara.json`；第一阶段强类型解析覆盖 `image_size`、`FREQ`、`STEP`、`freqSeries`、`phaseStepDirection`、`minZ/maxZ`、`aiEnabled`、`debugMapOutputEnabled`、`qualityInfoEnabled`、`saveOutputs`、`calibResultPath`。
- `calibResultPath` 在当前 Legacy 配置中不存在，第一阶段按评审结论允许缺省；`--dry-run-no-calib` 不阻塞，显式 `--calib` 缺失时返回 `CalibrationMissing`。

## 2026-07-09 第二阶段计划发现

- 当前第一阶段接口已经具备 `ImageView`、`StripeImage`、`StripeFrameGroup`、`StageStats`、`FrameResult` 和 `ReconstructEngine`，第二阶段无需重写接口骨架，应在现有契约上扩展状态码和统计字段。
- `ReconsConfig` 当前只保存 `FREQ`、`STEP`、`freqSeries` 原始字段；第二阶段应派生 `StripeRequirement`，用于检查每个频率的相移覆盖，同时避免把固定 `STEP` 写死到输入结构。
- `CalibrationModel` 当前只记录 image size 和矩阵 vector 占位；第二阶段应增加宽松 key 映射和矩阵 shape 校验，但仍不做真实几何映射或标定计算。
- 当前 sample 只构造内存假图；第二阶段适合增加 `--input-manifest`，仍只构造内存图，不读取真实扫描目录，以便验证调用契约而不越界到 IO/Legacy 数据读取。

## 2026-07-09 第二阶段实施发现

- 第二阶段实现后，pipeline 阶段输出稳定为 `input_contract_validation`、`calibration_contract_validation`、`image_preprocess_dry_run`、`algorithm_not_computed`。
- `--input-manifest` 仅构造内存条纹图，未读取真实扫描目录或图像文件；测试样例覆盖 valid、black、missing step 三类 manifest。
- 标定读取当前基于测试样例验证了 `image_size`、`leftIntrinsics/rightIntrinsics`、`leftDistortion/rightDistortion`、`R/T/Q` 的宽松解析和矩阵长度校验；尚未用 MPS 真实 `calibResult.json` 验证字段命中情况。
- 第二阶段仍未输出 depth/normal/quality，`FrameResult` 中三者保持 false，算法阶段用 `NotComputed` 明确标记。

## 2026-07-09 第三阶段实施发现

- 第三阶段将 pipeline 阶段输出推进为 `input_contract_validation`、`calibration_contract_validation`、`image_preprocess_dry_run`、`wrapped_phase_compute_cuda`、`downstream_not_computed`。
- 真实数据 `D:\Data\Calib\2607011016_mach6\singleStripe` 为 5 组数据，每组左右各 18 张 424x400 8-bit BMP；第三阶段 sample smoke 读取 group 1 中按 `phaseStepCounts=[3,5,5]` 映射的 26 张条纹图。
- `WrappedPhaseComputerCuda.cu` 通过 CUDA kernel 计算包裹相位和调制强度，用于建立相位模块输入/输出契约；它仍不是 Legacy `GPUPhaseUnwrapper` 完整迁移。
- 当前配置新增 `phaseStepCounts`，表达每个频率独立相移步数；这避免把全局 `STEP=5` 错写成所有频率固定 5 步。
- valid manifest 当前可通过包裹相位参考阶段并设置 `wrappedPhaseComputed=true`；depth、normal、quality 仍保持 false。
- 低调制 manifest 会在 `wrapped_phase_compute_cuda` 阶段返回 `PhaseQualityInsufficient`，用于验证质量闸门不会伪造后续结果。
- missing step manifest 仍在输入契约阶段失败，说明第三阶段没有绕过第二阶段的频率/相移覆盖校验。

## 2026-07-09 第四阶段实施发现

- Legacy `GPUPhaseUnwrapper::phaseUnwrapping()` 的默认 two-stage 路径先计算 `PH12/PH23/PH123`，再调用 `getAbsPhaseGPU(d_PH23, d_PH123, freq23_)` 得到 `abs23`，最后调用 `getAbsPhaseGPU(d_phi[2], d_phase_abs23, freq3/freq23)` 得到高频 absolute phase。
- Legacy `getAbsPhaseKernel` 的核心公式为 `round((warp_b * rate - warp_s) / 2pi)`，输出 `warp_s + 2pi * rounded`；当前第四阶段已按该公式迁入 `PhaseUnwrapperCuda.cu`。
- 当前 `config/reconsAlgPara.json` 中 `phaseUnwrapResidualGateEnabled=false`，因此第四阶段默认不启用 residual gate；代码已读取 residual threshold 以便后续打开验证。
- `phase_unwrap_cuda` 输出左右相机 absolute phase vector 和统计：真实 group 1 运行 `cudaPixels=339200`，即 `424*400*2`。
- 第四阶段仍未做标定 remap、左右匹配或三维重建；`downstream_not_computed` 现在只表示 matching/reconstruction 未计算。
- 每频率相移步数继续由 `phaseStepCounts` 控制，真实 group 1 仍读取 26 张 BMP，不消费每侧剩余的 14..18 号图。

## 2026-07-09 第五阶段实施发现

- Legacy `computePointCloudFromPhase()` 的核心点云路径包含 disparity 匹配、`Q` reproject、点云 smoothing、nonConnected filter 和 normal 估计；当前第五阶段迁入 CUDA disparity/Q reproject/normal，并保留 smoothing/filter 为可选配置。
- 历史 Legacy `D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json` 不包含 `phaseStepCounts`，因此按 Legacy `STEP=5` 回退为 3 个频率每侧各 15 张图、左右共 30 张图；项目默认配置仍支持 `[3,5,5]` 这种每频率不同相移步数。
- 使用 `config\reconsAlgPara.json` 的 `[3,5,5]` 与使用历史 `run_config.json` 的全 5 步会消费不同输入张数；做 Legacy 历史点云对比时应使用 `singleStripe\output\run_config.json`。
- 首次点云 filter/smoothing 组合会显著改变洞分布和点数：filter 可把点数降到约 5k，smoothing 可扩洞到约 148k；公平对比 Legacy 原始几何时应默认关闭。
- 未加入 rectification/remap 时，点数可接近 Legacy 但 Z/几何偏差明显；补入 remap/rectification 后，`calibResult.json` 可达到 nearest RMS 约 `0.1597mm`、P95 约 `0.2506mm`。
- 历史 Legacy 输出最贴近 `calibParams.yml` 的矩阵集合；直接用 `D:\Data\Calib\2607011016_mach6\calibParams.yml` 后，group 1 输出 `96160` 点，对 Legacy `101528` 点，nearest mean `0.0535mm`、RMS `0.0626mm`、P95 `0.1029mm`。
- `calibParams.yml` 是 OpenCV YAML matrix 格式；MSVC 不支持 `std::regex::multiline`，解析器应使用显式 `(^|\n)` 锚点和 `data: [...]` 块匹配。
- 当前 remap 实现是 post-unwrap absolute phase remap 近似，而 Legacy 在 `GPUPhaseUnwrapper::unwrapPhase()` 中先 remap intensity image 再计算 wrapped/absolute phase；这是剩余逐点差异的重要来源。
- 当前输出颜色为左侧条纹灰度派生，不等价于 Legacy RGB/纹理路径；几何对比指标优先看 nearest distance、点数和洞分布，而非颜色。

## 2026-07-09 第六阶段实施发现

- Legacy 单帧质量主参考是 `point_reliability_map`：点级质量通道为 semantic class、quality score、reason bits，整帧质量由 summary/sentinel 承载。
- Legacy reason bits 包括 `ReasonSemanticBackground`、`ReasonLowModulation`、`ReasonSaturation`、`ReasonHighMatchCost`、`ReasonCandidateAmbiguous`、`ReasonDepthRange`、`ReasonHoleEdge`、`ReasonLocalDiscontinuity` 等；第六阶段已保留同名 bit 口径。
- Legacy 整帧质量公式不是简单均值：先统计 valid coverage、center ROI coverage、largest connected component、boundary ratio、mean/p50/p90/p95，再用 coverage hard support 和 spatial penalty 得到 `frameQualityScore`。
- Legacy sentinel 写入 quality map `(0,0)` 的 score/reason；本阶段内部质量 map 同样写 sentinel，但 sample 当前只输出 `qualitySummary`，未导出质量图文件。
- 当前第五阶段 compact vertices 不足以做洞边缘/覆盖率判断；第六阶段扩展 `gridPoints`，保留每像素点云、match cost、candidate count、modulation。
- 真实 group 1 质量评价结果：`frameQualityScore=0.766212`、`coverage=0.566984`、`centerCoverage=0.737728`、`connectedSurfaceScore=0.492734`、`lowQualityReasonBits=1922`。
- 当前不接入 AI/TensorRT，因此 semantic background reason 在真实数据上不会触发；后续 AI mask 接入后可复用 reason bit。

## 2026-07-09 Legacy 大比拼：output260707 gypsum

- `D:\Data\output260707_gypsum\Upper\SourceImg` 不存在；实际结构是 `D:\Data\output260707_gypsum\Upper\<frame>\SourceImg`。
- 数字帧目录为 `0..465`，另有 `texture` 目录；实际是 466 个数字帧。
- 每帧 SourceImg 中有 `L0.bmp..L17.bmp` 和 `R0.bmp..R17.bmp` 共 36 张 8-bit BMP；示例文件大小为 170678 bytes。
- Legacy 点云输出位于每帧目录 `0.exr`，可用 OpenCV 在 import `cv2` 前设置 `OPENCV_IO_ENABLE_OPENEXR=1` 读取；有效点数以 XYZ 三通道有限且向量范数非零统计。
- 当前数据目录没有发现可直接作为 Legacy 单帧重建耗时的日志；`pose_vis` 下是 BA/pose 后处理文本，`texture` 下为纹理相关小文本。
- 全量 `0..465` 对比结果：可比 447 帧中没有点数低于 Legacy 95% 的帧；新算法总点数 `58,747,416`，Legacy 总点数 `50,345,889`，总比例 `1.166876`。
- 直接重跑 Legacy `TeethScanAlgorithmApp` 需要修正临时配置中的 `calibParamsPath`，原始 `run_config.json` 指向不存在的 `D:/Data/Calib/260701_mach6/calibParams.yml`。
- 同进程批量 `100..104` 耗时对比：新算法 median `56.35ms`、mean `69.93ms`；Legacy app median `434.659ms`、mean `439.985ms`。
- Legacy `0.exr` 尾部 frame `447..465` 为 0 点；用修正配置重跑 Legacy frame 447 仍为 0 点。新算法 frame 447 输出 `85256` 点但质量分 `0.171903`，说明问题不是点数不足，而是后续是否需要按低整帧质量硬拒绝。

## 2026-07-10 Legacy 大比拼复核发现

- 点数比例分布：447 个可比帧中，median `1.099007`、P10 `1.017720`、min `0.977822`。
- 实际有 11 帧点数少于 Legacy，2 帧低于 Legacy 的 98%；旧 summary 的 `point_count_losing_frames=0` 使用 95% 阈值，名称容易误解。
- Legacy 点数小于 100 的帧有 21 帧，小于 1,000 的帧有 23 帧；新算法仍普遍输出约 7.5 万至 9 万点。
- 点数比例超过 2 倍的帧有 30 帧，超过 10 倍的帧有 5 帧；Legacy zero、new nonzero 的帧有 19 帧。
- frame 0：Legacy `50` 点，新算法 `74923` 点，`frameQualityScore=0.577159`，`boundaryRatio=0.912936`。
- frame 445：Legacy `13` 点，新算法 `75253` 点。
- frame 446：Legacy `244` 点，新算法 `77446` 点，`frameQualityScore=0.457082`，`boundaryRatio=0.913411`。
- frame 447：Legacy `0` 点，新算法 `85256` 点，`frameQualityScore=0.171903`，`boundaryRatio=0.903807`。
- 修正结论：正常帧点数和效率没有重大落后；sparse/zero 帧的大量 extra-only 点可能是误点，也可能是相对 Legacy 的有效恢复，必须使用相位、匹配、局部几何、跨帧或融合模型进行独立验证。
- 修复计划文档：`docs/planning/2026-07-10-legacy-big-battle-remediation-plan.md`。

## 2026-07-10 新算法点云盯帧发现

- frame 0/1/2 的 PLY 点数分别为 `74923/75651/74607`，各帧完整范围接近 `X=0.27..14.94mm`、`Y=-9.94..8.56mm`、`Z=83.79..153.65mm`。
- 三帧聚合 Z 百分位为：P1 `84.27mm`、P5 `86.38mm`、P50 `110.79mm`、P95 `147.11mm`、P99 `152.45mm`；异常不是少量 Z 离群点，而是大量点覆盖完整深度范围。
- 抽查 frame `0/1/2/447/465`，每帧 Z 最小值均为 `83.79062mm`，最大值均为 `153.6479mm`。精确重复的上下边界强烈提示深度范围钳制、边界视差或无效匹配仍被当成有效点输出。
- 该证据否定了“首尾 sparse/zero 帧只是比 Legacy 恢复更多有效表面”的乐观解释；至少当前输出包含大面积缺乏真实几何支持的点。
- 盯帧时应优先观察：整个像素网格是否形成规则矩形幕布、Z 边界面是否稳定存在、物体移动时背景点是否与物体同步、frame 447 之后是否仍维持完整视场点云。

## 2026-07-10 远程 Legacy 错误相位匹配调研发现

- Legacy 远程仓库为 `D:\code\teethscanalgorithm3x4`，相位/匹配相关远程分支中 `origin/zheng/ParallaxMatching` 的 tip 为 `52bda09 json参数规范化`，是本轮首要研究对象。
- 其他相关远程候选为 `origin/zheng/denoise`（`35ba7df 相位差阈值`）和 `origin/v1.0.0`（`bd92c6f 相位跳变检测/点云滤波优化`）；它们分别更偏阈值调整和整帧质量/后处理，不应先于匹配约束主线。
- 当前调研先区分“分支名称”和“仍未合并实验”：需要通过 ancestry/contains 证明 `ParallaxMatching` 是否已经进入最新 `origin/main`，再以生产主线实际默认配置判断核心改动。
- 当前 `reconstructOneFrame` 工作区已有多项未提交修改，本轮只更新规划记录，不清理、不覆盖、不提交。
- `origin/zheng/ParallaxMatching@52bda09` 已被 `origin/main@5410c14` 完整包含，`git branch -r --contains 52bda09` 同时返回 `origin/main` 和该分支；因此它代表已落地主线的匹配约束演进，不是独立待验证分支。
- 核心提交变更面显示：`998cb06` 修复视差回退；`351d8a1` 引入物理视差窗口；`2f6d674` 引入左右一致性；`6ea30e6` 集中加入唯一性、单调穿越、候选质量和行级 DP 等约束；`52bda09` 仅规范化生产 JSON 并确定默认开关。
- `bd92c6f` 同时修改相位跳变检测和点云过滤，主要用于整帧质量/后处理；`6da2003` 是三频纠错与极线最小二乘诊断的大型扩展，不能仅凭功能更复杂就视为当前生产核心。
- `998cb06` 修复了确定性的错误匹配制造器：旧代码把 `best_cor_x_float` 初始化为 `0.0f`，当左右相位不满足严格单调插值条件时会用右图第 0 列计算视差；修复后默认回退到整数最佳候选 `float(best_cor_x)`，并区分 `subpixel_success/subpixel_fallback`。
- 同一提交将邻域插值条件从 `>=/<=` 改为严格 `>/<`，避免相位平台导致分母为零或把平坦区域当成可插值穿越。
- `351d8a1` 新增 `DisparityWindowParams`，从 `Q(2,3)`、`Q(3,2)`、`Q(3,3)` 和工作深度范围推导合法视差，再用像素 margin 扩展；它把原先整条极线的任意数值接近搜索限制到物理可达区域。
- 物理视差窗不是最终 Z 范围检查的重复：它在候选搜索前减少周期相位重复造成的错误候选，最终 Z 检查只能在错误匹配已经形成后做事。
- `2f6d674` 的左右一致性第一版以右图最佳相位为目标，在左图原像素附近反向搜索；反查最佳位置距原左像素超过约 1 px 时直接清零 disparity，而不是保留点后再降低质量。
- 最新 `origin/main` 已将左右一致性升级为独立 `checkLeftRightConsistency()`，支持浮点反查位置、物理视差窗和可配置容差；主线生产默认容差需要结合 JSON 确认。
- 最新主线新增 `checkRightPhaseMonotonicSupport()`：右图最佳候选与左相位尚有差值时，必须在候选左右有限邻域内沿正确方向出现真实相位穿越；平坦相位、局部噪声造成的偶然数值接近会被拒绝。
- 主线匹配顺序为：候选阈值/uniqueness -> 左右一致性 -> 右相位单调穿越 -> 亚像素插值。前两个几何/拓扑约束失败时直接 `match_ok=false`，并分别写入 failure map 和 rejection counter。
- 这与当前“点先进入最终 PLY，再由质量模块添加 reason bit”的语义不同：Legacy 主线把错误匹配约束放在 disparity 生成前，属于有效性判定而非质量评分。
- 最新 Legacy `origin/main` 的生产默认组合为：`disparityWindowEnabled=true`、`matchingLeftRightConsistencyEnabled=true`、`matchingRightPhaseMonotonicEnabled=true`，容差 `1.5 px`、单调半径 `1`、最小 slope `0.0001`。
- 最新生产配置仍关闭 `matchingUniquenessEnabled`、candidate quality filter、row DP 和 disparity local consistency；因此这些属于可选诊断/增强约束，不应与已验证的生产核心同等优先迁移。
- 最新生产路径仍使用 `phaseUnwrapMethod=legacyTwoStage` 和 `reconstructionMethod=disparity`；`6da2003` 的三频纠错/极线 LS 大模块当前不是默认主链路。
- 当前 `reconstructOneFrame` 已有物理视差窗、uniqueness、左右一致性、亚像素和 local consistency 代码，但 `config/reconsAlgPara.json` 中左右一致性和亚像素均为 `false`，实际石膏历史运行没有采用 Legacy 最新主线默认组合。
- 当前项目完全没有 `matchingRightPhaseMonotonicEnabled/radius/minSlope` 配置或 CUDA 检查，这是与最新 Legacy 生产匹配热路径最明确的功能缺口。
- 当前配置同时把 `disparitySubpixelEnabled=false`；代码虽然使用整数最佳点作为安全回退，但未启用亚像素时几何精度和左右反查行为仍与最新 Legacy 生产路径不完全一致。
- 当前 `leftRightConsistent()` 只返回整数 `bestX`，没有像 Legacy 最新主线那样对反查点做亚像素插值；这是启用同名开关后仍存在的实现差异。
- 当前正向亚像素插值已正确以整数 `bestX` 初始化，插值条件也使用严格 `>/<`，因此 `998cb06` 的 x=0 回退 bug 已经避免。
- 已有 `build/constraint_probe/summary.csv` 单变量消融：frame 447 在 window 下 `85427` 点，window+LR 下 `61347` 点，window+LR+uniqueness 下 `48787` 点；约束有效，但仍远未解释 Legacy 同帧 0 点。
- frame 100 在相同消融下从 `136384` 降至 `126520`、再降至 `104739`，说明 uniqueness 会明显减少正常帧点数；考虑到 Legacy 生产默认关闭 uniqueness，不应先把它作为主修复强制开启。
- 现有消融尚未覆盖右相位单调穿越，因为当前项目没有该实现；它同时满足“Legacy 生产默认开启”和“专门拒绝平坦/偶然相位接近”两个条件，是下一项最有价值的单变量验证。
- 若单调穿越后 frame 447 仍大量出点，下一步应测试候选 modulation/饱和/低亮过滤；point-cloud filter 和整帧质量 reject 应放在匹配有效性约束之后，避免用后处理掩盖错误对应。

## 远程 Legacy 核心结论

1. 最严重的历史 bug 是亚像素插值失败回退到右图 x=0；当前项目已避免。
2. 最基础的物理约束是 `Q + 工作深度` 推导视差搜索窗口；当前项目已有并默认开启。
3. 最新生产主线的关键新增组合是左右一致性和右相位局部单调穿越；当前项目前者默认关闭且仅整数反查，后者完全缺失。
4. uniqueness、candidate quality、row DP、local consistency 是诊断/增强手段，最新生产配置默认关闭，不能替代前三项核心约束。
5. 三频纠错/极线 LS 与相位跳变整帧判定不是当前错误匹配修复的第一迁移目标。

## 2026-07-10 错误相位匹配实施发现

- Legacy 最新 `checkLeftRightConsistency()` 在反查得到整数最佳左像素后，会按左右相位关系做一次严格单调的亚像素插值，再以浮点位置和原左像素比较容差。
- Legacy `checkRightPhaseMonotonicSupport()` 将 radius 限制到 `1..5`，将 min slope 限制为非负；最佳候选已在 slope 内接近目标时直接通过，否则要求正确方向邻点跨越目标相位。
- 当前配置和解析入口集中在 `ReconsConfig.h/.cpp`，适合直接增加 `matchingRightPhaseMonotonicEnabled`、`matchingRightPhaseMonotonicRadius`、`matchingRightPhaseMinSlope`。
- 当前 `point_cloud_reconstructor_test` 只有一组正常线性相位 smoke；需要扩展为正确单调穿越、平坦错误候选拒绝和左右反查亚像素容差场景。
- 当前 CUDA kernel 已持有左右 absolute phase 和 candidate count，不需要增加额外图像输入；新增约束只增加局部邻域访问与少量分支。
- 合成线性穿越场景中，单调约束保留 `112/128` 个像素，并拒绝 `8` 个候选；这 8 个候选对应每行一个缺少正确方向外侧邻点的边界位置，属于预期边界行为。
- 既有 `build/constraint_probe/window_lr.json` 可作为真实消融基线：window=true、LR=true、subpixel=true、uniqueness=false，且未包含 monotonic 字段。
- 外部 `singleStripe/output/run_config.json` 显式关闭 LR 和 subpixel，不适合直接验证最新生产组合；真实消融应使用显式 window+LR+monotonic+subpixel 配置。
- 当前二进制真实 A/B：frame 0 从 monotonic off 的 `56411` 点降到 on 的 `27464` 点，减少 `28947` 点，约 51%；单调拒绝计数 `29089`。
- frame 100 从 `126520` 降到 `118756`，减少 `7764` 点，约 6%；单调拒绝计数 `7805`，正常高质量帧主体仍保留。
- frame 447 从 `61347` 降到 `30629`，减少 `30718` 点，约 50%；单调拒绝计数 `30838`，证明此前约一半点来自缺少局部相位穿越支持的匹配。
- frame 447 仍有 `30629` 点且 `frameQualityScore=0.12504`，说明单调穿越是必要约束但不是完整答案；剩余点需要继续检查候选光照/调制度和精确 remap 时序。
- 三帧的 LR rejection 在 monotonic on/off 间保持不变，分别为 `18685/9873/24133`，说明本次点数变化由新增单调约束单独贡献，消融口径干净。
- 点云重建阶段耗时 A/B：frame 0 为 `18.27 -> 20.15ms`，frame 100 为 `19.42 -> 20.18ms`，frame 447 为 `18.68 -> 19.10ms`；新增约束成本约 `0.4..1.9ms`。
- 分进程总耗时存在 CUDA 初始化和系统波动，不适合据此宣称加速或退化；核心 point-cloud stage 未出现明显效率落后。
- 新约束 PLY 已生成于 `build/constraint_probe/window_lr_monotonic_ply/{0,100,447}/depth_points.ply`，用于继续检查剩余深度场分布。
- PLY 统计显示 frame 0 的 Z 范围从旧 `83.7906..153.6479mm` 变为 `83.7547..153.9581mm`，P5/P50/P95 几乎不变；单调约束主要降低点密度，没有压缩异常深度跨度。
- frame 447 同样仍覆盖 `83.7799..153.9041mm`，P5 `86.6302mm`、P95 `147.1124mm`，与旧输出几乎一致；“全深度场幕布”问题尚未根治。
- 正常 frame 100 的分布得到收敛：P1 从 `90.92` 提升到 `95.01mm`，P95 从 `128.75` 降到 `123.35mm`，说明约束对正常物体边缘/背景也有净化作用。
- 因 Legacy 生产默认关闭 uniqueness 和 candidate quality filter，剩余首要差异应优先回到 phase 数据来源：Legacy 在相位计算前 remap intensity，当前仍是 post-unwrap phase remap 近似；相位有效 mask/低调制时序也可能不同。
- 下一修复阶段不应仅继续加更强匹配阈值，而应实现或验证 exact intensity-remap-before-phase，再重新观察 frame 0/447 的 Z 分布。
- 独立审查发现并已接受四项重要修正：缺失 key 的旧配置保持原关闭行为、正反向亚像素禁止超出相邻采样区间、相位数组必须严格等于 `width*height`、`FrameResult::matchingSummary` 追加到末尾保持位置聚合兼容。
- 审查还指出 Legacy 原版在 `leftPhase==bestPhase` 时直接通过，会放过“平坦且完全相等”的相位；当前实现加强为必须存在局部低侧/高侧斜率支持，并新增回归测试。
- 全局 atomic rejection counters 在异常帧有集中争用，但实测点云阶段增加约 `0.4..1.9ms`；本轮保留可观测性，后续 GPU workspace/reduction 优化时处理。

## 2026-07-10 审查修复后最终验证发现

- 审查修复后的 Release 全量重建和 `10/10` CTest 均通过；本节数据取代前述审查前消融数字，作为当前实现的权威验证结果。
- monotonic off -> on 的点数变化：frame 0 为 `56413 -> 27430`，frame 100 为 `126520 -> 118748`，frame 447 为 `61347 -> 30581`。
- 最新 rejection 计数：frame 0 为 LR `18683`、monotonic `29125`；frame 100 为 LR `9873`、monotonic `7813`；frame 447 为 LR `24133`、monotonic `30887`。
- 审查修复使 exact-equal 平坦相位也必须具有局部低侧/高侧斜率支持，因此 frame 0/100/447 相比审查前结果又分别少 `34/8/48` 点，符合新增边界约束预期。
- 最新 point-cloud stage 耗时 monotonic on 为 `17.551/19.953/18.651ms`，off 为 `18.826/21.235/18.989ms`；单次运行未显示新增约束造成明确退化，仍需用同进程多帧基准评估 atomic 争用。
- monotonic on 的 Z 统计：frame 0 为 `83.7547..153.9581mm`，P5/P50/P95=`86.3762/110.7908/147.4807mm`；frame 447 为 `83.7799..153.9041mm`，P5/P50/P95=`86.6302/111.0012/147.1125mm`。
- 正常 frame 100 的 monotonic on Z 分布为 `83.7688..153.7276mm`，P1/P5/P50/P95/P99=`95.0150/105.9631/109.2675/123.3455/132.8693mm`。
- frame 0/447 的 Z 分位数在 monotonic 开关前后几乎不变，证明右相位单调穿越主要剔除重复的错误匹配密度，没有消除覆盖完整工作深度范围的错误相位层。
- 当前下一优先级保持不变：迁移或验证 intensity remap before wrapped-phase compute，并显式传播 phase validity/低调制 mask；不应仅继续叠加 uniqueness 或更强点云后过滤。
- 最新 PLY 和日志位于 `build/constraint_probe/post_review/{monotonic_on,monotonic_off}/{0,100,447}/`。

## 2026-07-10 Legacy 逐阶段追踪初始发现

- `D:\code\teethscanalgorithm3x4` 当前工作区干净，分支为 `feature/single-frame-quality`，HEAD=`27613a3`；最新远程主线为 `origin/main@5410c14`。
- 当前工作分支与最新主线不是同一提交，因此追踪时需要明确区分：历史/质量分支实际 pipeline、最新主线的错误匹配约束、以及已有 `0.exr` 输出的生成版本。
- `reconstructOneFrame` 当前未提交修改保持原状；本轮不清理、不覆盖，也不修改 DSSI。
- Legacy 主入口已定位到 `src/reconstruct.cpp:Reconstruct::init()` 和 `Reconstruct::reconstructOne()`；相位链位于 `src/calcPhaseUnwrap.cu:GPUPhaseUnwrapper`，匹配/重建链位于 `src/calcDepthImage.cu:computePointCloudFromPhase()`。
- 历史 frame 目录只保存最终产物，没有现成的 wrapped phase、absolute phase、disparity 或 rejection map：frame 0 的 `0.exr` 仅 `4820` bytes，frame 100 的 `0.exr` 为 `671759` bytes。
- frame 0/100 都包含 `SourceImg`、`0.exr`、`1.exr`、`2.png`、`3_legacy.png` 和后续生成的 `3.png`；需要通过代码确认每个编号的精确语义，不能只按文件名猜测。
- 当前 `build/bin/Release/TeethScanAlgorithmApp.exe` 和运行配置时间为 2026-07-10 12:00，晚于历史输出 2026-07-07 10:23；当前重跑可用于分析现有分支行为，但不能直接当作历史 `0.exr` 的精确版本复现。
- `Reconstruct::reconstructOne()` 首先并行执行左右 `GPUPhaseUnwrapper::unwrapPhase()`；任一相机的整帧 `unwrap_quality==0` 时直接返回 `-2`，否则继续重建。
- 当前分支的 `phase_quality_gate` 对默认 rectified disparity 路径明确跳过：代码只允许 epipolar least-squares 使用原图质量门控，因此它不会在现有生产 disparity 链路中剔除 frame 0/100 的低调制相位。
- AI semantic mask 仅在 `is_AI_scan=true` 时生成；石膏非 AI 扫描下 `semantic_info` 全零，不能依靠语义背景 mask 删除无效区域。
- 默认 disparity 分支调用 `computePointCloudFromPhase()`，一次传入相位阈值、物理视差窗、uniqueness、左右一致性、亚像素和局部一致性配置；真正的点有效性判定发生在这个 CUDA 函数内部。
- `finalizeReconstructionOutputs()` 在深度完成后才构建 `qualityInfo` 和 frame-quality sentinel；该质量图用于评分/诊断，不会反向清零 `depth_map`。
- Legacy 已有无需改代码的强诊断入口：开启 `debug_map_output_enabled` 可保存 `phase_left/right`、每频 wrapped phase、modulation、disparity、scores、depth 和 quality；开启 `save_stage_point_clouds` 可保存 raw、nofill、final 三阶段点云。
- Legacy 相位顺序是严格的 intensity-domain 流程：先用标定 `mapx/mapy` 对每张条纹图做双线性 remap，越界像素写 NaN；然后才计算三角和、wrapped phase 和 absolute phase。
- `computePhaseKernel()` 对每个频率计算 `magnitude=sqrt(sinSum^2+cosSum^2)*2/STEP`；小于 `B_min/modulation_min` 的该频率 wrapped phase 直接写 NaN。
- 高频饱和/低亮 flags 只在启用 quality info 时生成，阈值固定为 `>=250` 和 `<=10`；这些 flags 本身不在 `unwrapPhase()` 中清零 absolute phase。
- 默认 `legacyTwoStage` 依次执行 `PH23/PH123 -> abs23` 和 `high-frequency/abs23 -> final absolute phase`；残差门控是否生效由配置 `phaseUnwrapResidualGateEnabled` 决定。
- 最终 absolute phase 的整帧 `unwrap_quality` 只在相邻有效相位中 `>1rad` 跳变比例超过 `80%` 时置零；这个门槛只能拦截极端全帧崩坏，无法识别“相位连续但对应错误”的深度幕布。
- Legacy 每帧在 `unwrapPhase()` 内临时分配 remap buffer；该实现虽有性能代价，但证明正确时序是先 remap intensity，而不是当前新算法的 post-unwrap phase remap 近似。
- two-stage unwrap 对任一级输入 NaN 都继续传播 NaN；开启 residual gate 时，整数级次估计的舍入残差超过阈值也会写 NaN。第一阶段 abs23 默认经过中值滤波，最终 absolute phase 是否中值滤波由单独配置控制。
- 当前 `feature/single-frame-quality@27613a3` 的 disparity 匹配顺序是：左相位非零/非 NaN -> 物理视差窗 -> 最小相位差阈值 -> 可选 uniqueness -> 可选整数 LR consistency -> 可选亚像素 -> 写 disparity。
- 该工作分支没有最新主线的右相位局部单调穿越；LR consistency 只返回整数反查位置，没有亚像素反查。
- 当前分支的正向亚像素只要求邻点相位方向正确，没有要求左目标相位位于 `best/neighbor` 区间内，因此可能产生超过一个像素区间的外插；最新 `origin/main` 与本项目审查修复后已更严格。
- 如果 uniqueness/LR/subpixel 在历史运行配置中关闭，则候选只要在物理窗内取得最小相位差且小于 `pha_diff_threshold` 就会生成 disparity；对周期相位背景，这是形成大面积错误深度面的直接通道。
- 已核对仓库配置、当前 Release 配置和大比拼临时配置：`phaseUnwrapResidualGateEnabled=false`、`matchingUniquenessEnabled=false`、`matchingLeftRightConsistencyEnabled=false`、`disparitySubpixelEnabled=false`、`disparityLocalConsistencyEnabled=false`。
- 因此历史大比拼 Legacy 实际有效的匹配保护主要只有：低调制度 NaN、`phaseDiffThreshold=0.2`、物理视差窗和最终 Z 范围。
- 大比拼临时配置使用 `minZ=30/maxZ=180`，比仓库默认 `50/160` 更宽；候选搜索仍用 `disparityWindowMinZ=85/maxZ=150/margin=10`。最终 Z gate 的放宽会保留更多边缘/外推深度。
- disparity 重投影后始终执行点云高斯平滑，再固定执行 `nonConnectedComponentFilterKernel(distance=0.1,minNeighbors=5)`；这不是全局连通域过滤，而是图像邻域局部支持过滤。
- 对 frame 0 这种大面积连续错误幕布，邻域内错误点彼此接近，固定点过滤会把它们视作有支持的曲面而保留；该过滤主要删除孤立噪点。
- 非金属扫描不会执行 `fillHolesWithNormals`；石膏 frame 0/100 的大面积点不是补洞制造的。补洞仅在 `is_metal_scan=true` 时执行。
- 点过滤后再次计算法向，quality map 基于最终点、match cost、调制度和 light flags 生成，但仍不修改最终点云。
- `nonConnectedComponentFilterKernel` 的精确条件是：3×3 内至少 5 个距离 `<0.1mm` 的点、15×15 内至少约 177 个有效点，并且距离 `<1mm` 的邻点平均距离 `<0.8mm`。
- 邻域循环包含中心像素本身，因此“至少 5 个近邻”实际可由中心点加约 4 个相邻点满足；规则且稠密的错误深度面很容易通过。
- 最终 `generateCloudPoints()` 只排除 `(0,0,0)`，不会根据 normal、quality score、reason bits 或 frame sentinel 再删点。
- Legacy App 支持位置参数 `scan_root config output_root`，并通过 `--first/--last` 选择数字帧；可分别用 `--first 0 --last 0` 和 `--first 100 --last 100` 精确重跑。
- App 将每帧输出目录作为 `outputPrefix` 传入算法，配置中开启 `debugMapOutputEnabled/saveStagePointClouds/saveOutputs` 后可直接获得算法内部 debug maps、raw/nofill/final 点云，不需要改 Legacy 源码。
- 历史文件语义已由 App 源码确认：`0.exr=depth CV_32FC3`、`1.exr=normal CV_32FC3`、`2.png=color`、`3.png=quality info`。
- CLI 的 `--save-outputs` 只控制 App 外层 PLY/EXR/PNG；算法内部阶段输出仍由 JSON 的 `saveOutputs/debugMapOutputEnabled/saveStagePointClouds` 控制。
- 使用相同重建阈值、仅打开 debug/stage 输出后，当前 Legacy 分支对 frame 0 的阶段计数为：左相位有效 `130975`、候选匹配 `31195`、重投影有效 `31086`、平滑后进入点过滤 `87014`、过滤保留 `29`。
- frame 100 的阶段计数为：左相位有效 `161485`、候选匹配 `135089`、重投影有效 `131540`、平滑后进入点过滤 `145286`、过滤保留 `89900`。
- 这证明 Legacy 并不是在相位/匹配阶段完全避免 frame 0 错误：它同样先生成约 3.1 万 raw 乱点，最终主要依赖固定点云平滑和局部稠密/连续性过滤把结果压到几十点。
- frame 100 有真实连续表面，因此大量点能同时满足 15×15 稠密支持和 3×3 小距离条件，保留约 8.99 万点；背景和边缘无效点则被大量删除。
- `filter_input > reprojection_points`，说明重投影后的高斯点云平滑会向原空洞位置扩展非零值；随后局部过滤再决定这些扩展点是否形成足够稳定的连续面。需要继续核对 Gaussian kernel 是否把零点当作可填充区域。
- 本次临时配置设置了旧别名 `qualityMapEnabled`，当前二进制日志仍显示 `quality_info=false`；质量图未生成，但不影响 depth/raw/nofill/final 点数链路。后续若需要质量图应使用 `qualityInfoEnabled`。
- 当前诊断运行产物根目录为 `build/legacy_step_trace/baseline_debug`，运行版本日志为 `v20251111.1`。
- Gaussian point smoothing 只平滑 `z`，中心像素的 `x/y` 原样保留；原空洞会暂时变为 `(0,0,smoothedZ)`，因此 `filter_input` 数量膨胀，但这些点与正常邻点三维距离不一致，随后大多被局部过滤删除。
- frame 0 的最高频 modulation 中位数为左 `5.48`、右 `4.52`，P95 仅约 `14.92/13.39`；frame 100 的最高频中位数为左 `32.97`、右 `30.94`，P95 约 `55.03/55.85`。
- 这说明 frame 0 的条纹信号整体很弱，而历史配置的 `phaseQualityGateEnabled=false` 使质量阈值 8 不参与 disparity 路径有效性判定；低信号但仍超过基础 Bmin 的相位会继续匹配。
- frame 0 匹配点的 phase cost 中位数 `0.0761`、P95 `0.1856`，frame 100 为 `0.0787/0.1612`；仅使用 `phaseDiffThreshold=0.2` 无法可靠区分低信号错误匹配和真实表面匹配。
- frame 0 的 raw disparity 实际覆盖整个物理窗边界 `-145..173px`；固定过滤后仅 29 点，最终 Z 主要落在约 `89.6..151.0mm` 的两个残余簇。
- frame 100 最终保留 `89900` 点，Z 的 P5/P50/P95 为 `106.62/109.11/111.17mm`，主物体表面已高度收敛；极端范围仅由少量边缘残点贡献。
- debug map 统计已保存到 `build/legacy_step_trace/baseline_debug/debug_map_stage_stats.csv`。
- Legacy 和新算法当前基础相位门槛均为 `Bmin=1`；frame 0 的 modulation 虽低于质量阈值 8，但仍有大量像素高于 1，因此只靠 Bmin 不会阻止乱点。
- 本次新算法盯帧配置 `build/constraint_probe/window_lr_monotonic.json` 明确设置 `pointCloudSmoothingEnabled=false`、`pointCloudFilterEnabled=false`，最终 PLY 因而对应 raw CUDA 重投影结果，而不是 Legacy final output。
- 最新 `origin/main` 虽默认开启 LR、右相位单调穿越和亚像素，仍保留固定 `gaussianBlurKerneltoPt -> nonConnectedComponentFilterKernel` 后处理链；它没有用新匹配约束替代点过滤。
- 最新主线同样保持 `Bmin=1`、phase residual gate=false、phase quality gate=false、candidate quality filter=false；因此其策略是“较宽松地产生候选，再用匹配约束和最终三维连续面过滤共同收敛”。
- 新算法现有 `nonConnectedFilterKernel` 基本复制 Legacy 的 15×15/3×3 邻域规则，但 `gaussianBlurPointKernel` 会对有效邻点的完整 XYZ 做加权平均并填入空洞；Legacy 只平滑 Z，保留中心原始 X/Y，因此两者的 smoothing 语义不等价。
- 新算法在保持 monotonic/LR 的情况下启用 `filter-only`，frame 0 被压到 0 点并返回 `ReconstructionInsufficient`；说明邻域过滤本身能有效阻止当前 raw 幕布输出。
- 同一 `filter-only` 配置下 frame 100 保留 `84833` 点，当前 Legacy 诊断运行 final 为 `89900` 点；在不启用不等价 smoothing 的情况下，点数已处于同一量级。
- 相比 raw monotonic frame 100 的 `118748` 点，邻域过滤删除约 `33915` 点；相比 frame 0 raw `27430` 点则全部删除，说明该规则对“真实连续物体”和“全场错误幕布”具有明显区分能力。
- 新算法现有 XYZ smoothing+filter 输出 frame 0=`3793`、frame 100=`94529`；它在 frame 0 重新制造了数千点，验证了该 smoothing 不能直接替代 Legacy Z-only smoothing。
- 2026-07-07 历史 EXR 统计：frame 0=`50` 点，Z=`93.97..138.63mm`；frame 100=`124032` 点，Z P5/P50/P95=`99.27/101.92/108.66mm`。
- 当前 Legacy 重跑与历史 EXR 绝对点数/深度不同，说明二进制、分支或 rectification planner 已演进；但两者都证明 frame 0 final 是极稀疏结果，不能与新算法 raw PLY 直接比较。
- 完整逐阶段分析已写入 `docs/planning/2026-07-10-legacy-stage-by-stage-root-cause-analysis.md`。

## 2026-07-10 Legacy 最终点云收敛实施发现

- 当前 `gaussianBlurPointKernel` 与 Legacy 有三处确定性差异：跳过无效邻点、按剩余权重重新归一化、同时平均 X/Y/Z。
- Legacy `gaussianBlurKerneltoPt` 对所有 3x3 样本的 Z 加权，包括零点；边界采用镜像反射；输出 X/Y 始终复制中心输入点。
- 因此 Legacy 空洞中心会暂时变成 `(0,0,smoothedZ)`，而当前实现会生成邻域平均的 `(smoothedX,smoothedY,smoothedZ)`，后者更容易被局部三维过滤误认为真实连续表面。
- 当前结果只暴露 `rawValidPointCount` 与 `filteredGridValidPointCount`，缺少 smoothing/filter-input 点数，容易再次混淆 raw 与 final 输出口径。
- 当前 `point_cloud_reconstructor_test` 使用 `16x8`，过滤器按 `width >= 32 && height >= 32` 条件禁用；需要新增至少 `32x32` 的合成稠密面与中心空洞场景。
- 本轮回归测试将通过公开 CUDA 重建入口验证：有效中心的 X/Y 不变、Z 使用包含零点的完整 3x3 核、空洞经 smoothing 产生 `(0,0,z)`，并在 filter 后归零。
- 进一步核对发现，Legacy 虽定义了对称 `gaussianKernelS[3x3]`，但 `gaussianBlurKerneltoPt(..., KERNEL_SIZE_S)` 内实际索引的是 `gaussianKernel[7x7]`；因此真实 3x3 权重是 7x7 数组的前 9 项，并非对称 `gaussianKernelS`。
- 最新 `origin/main@5410c14` 仍保持这一行为。对称 3x3 Z-only 实现使真实 frame 100 的 final 点数从 filter-only `84,833` 下降到 `60,324`，明显偏离当前 Legacy final `89,900`，不能作为兼容实现。
- 使用真实历史权重后 frame 100 仅回升到 `61,597`，仍明显低于 filter-only 和 Legacy，说明剩余差异不在核权重。
- Legacy `nonConnectedComponentFilterKernel` 接收 `minZ/maxZ` 参数但未使用，中心和邻点有效性只检查是否为 `(0,0,0)`；当前实现则在 filter 内再次要求 Z 位于工作范围。
- raw reproject 已执行过 Z gate，但 smoothing 会因零邻点把部分原有效点的 Z 稀释到 `minZ` 以下。Legacy 仍让这些非零点参与局部连续性判定，当前实现会提前删除，是下一项单变量修复。
- 移除 filter 内重复 Z gate 后，Legacy-compatible smooth+filter 的 frame 100 从 `61,597` 小幅增至 `62,134`，仍低于 filter-only `84,833`；该差距主要来自当前 raw 相位/匹配几何与 Legacy raw 不完全一致，而非 filter Z gate。
- 最终生产选择 filter-only：frame `0/100/447` 为 `0/84,833/0`；既能压制无支撑帧，又最大限度保留 frame 100 的真实连续表面。
- filter-only frame 100 与当前 Legacy final `89,900` 点相比少 `5,067` 点，nearest mean/RMS/P95 为 `0.0698/0.2090/0.2373mm`，两者质心 Z 为 `109.2966/109.4040mm`。
- Z 分位数进一步确认 filter-only 不是保留全深度幕布：P5/P50/P95=`106.3836/109.1723/111.3925mm`，Legacy current final=`106.6240/109.1080/111.1720mm`。
- filter-only point-cloud stage 在 frame 100 为约 `22.7..23.4ms`，相对 raw 约 `21.6ms` 只增加约 `1..2ms`，远低于已测 Legacy app 约 `435ms` 单帧总耗时。
- 生产 JSON 已显式开启 `pointCloudFilterEnabled=true` 并保持 `pointCloudSmoothingEnabled=false`；缺少这两个 key 的旧配置仍默认双关闭。

## 2026-07-10 filter-only 全量再比拼初始发现

- 现有 `scripts/benchmark_output260707_gypsum.py` 每个 frame 都通过 `subprocess.run()` 新启动 `reconstructSample`，适合逐帧点数/状态统计，但每帧都包含进程启动和 CUDA 初始化，不能作为稳定效率结论。
- 效率需要单独使用 sample 的 `--first/--last` 同进程批量模式，并用 Legacy App 的同类批量入口做相同帧段对比。
- 本轮公平配置应使用刚验证的 `build/legacy_step_trace/z_only_validation/filter_only/config.json`，不能退回未启用最终过滤的历史 benchmark 配置。
- 原脚本将 `exitCode != 0` 的帧排除出 comparable；filter-only 的 0 点帧以 `ReconstructionInsufficient`/exit 1 返回，会被错误排除，导致点数输赢偏乐观。
- 本轮修正为：`Ok` 和 `ReconstructionInsufficient` 都视为算法已完成；0 点必须计入与 Legacy 的点数比例，同时单独统计真正失败状态。
- benchmark CSV 新增 `pointCloudElapsedMs` 和 raw/smoothed/filtered 点数，summary 区分 `<100%` 与 `<95%` 帧，并保留旧 `point_count_losing_frames` 作为 `<95%` 兼容字段。
- 全量同进程基线：新算法 wall `34.268s`、`13.599 FPS`，稳定 `runElapsedMs` median `52.654ms`；Legacy wall `11.377s`、`40.960 FPS`，稳定核心 median `9.744ms`，当前效率明确落后。
- 新算法阶段中位数：wrapped phase `8.956ms`、unwrap `3.179ms`、point cloud `20.654ms`、quality `13.931ms`。
- filter-only 配置明确 `qualityInfoEnabled=false`，但 pipeline 仍无条件执行 `evaluatePointReliability()`；这是已证实的约 `14ms/帧` 非对称额外工作，Legacy benchmark 配置同样关闭质量图。
## 2026-07-10 filter-only 性能根因定位

- 质量评价关闭前，稳定帧 `runElapsed` median 为 `52.654ms`；`quality_evaluate` 自身 median 约 `13.931ms`。
- 按 `qualityInfoEnabled=false` 跳过质量评价后，frame `101..104` 点数保持 `76556/99435/99164/95866`，稳定 `runElapsed` median 约 `37.2ms`。
- 点云阶段内部细分计时（frame `101..104` median）：
  - device allocation：`0.281ms`
  - H2D：`0.496ms`
  - CUDA kernels + synchronize：`0.482ms`
  - host allocation：`1.991ms`
  - D2H：`0.855ms`
  - host materialize：`15.270ms`
- 因此点云阶段约 `20.6ms` 的首要根因是 CPU 侧结果物化，而不是 CUDA kernel 或逐帧 `cudaMalloc`。当前 materialize 无条件完成以下工作：
  - 分配并填充 `169600` 个 `PointCloudGridPoint`；
  - 复制全尺寸 match score 与 candidate count；
  - 对每个像素重新从条纹图计算 high-frequency modulation；
  - 同时构造 compact vertices。
- filter-only 配置明确 `qualityInfoEnabled=false`，上述 grid/score/candidate/modulation 仅供质量模块使用，属于关闭功能后的无效热路径工作；应条件化，不改变点云匹配、过滤和顶点结果。
- `nvprof` 本机尝试以 `-1073741515` 退出且无输出文件，后续不重复该失败路径。
- 条件化质量专用物化后，点云阶段 median 从 `20.581ms` 降到 `9.566ms`，稳定总耗时从 `37.201ms` 降到 `26.583ms`；五帧点数完全一致。
- 优化后主要阶段变为 wrapped phase 约 `8..10ms`、point cloud 约 `9.6ms`、validation 约 `4ms`、unwrap 约 `3.2ms`。
- wrapped phase 每帧按左右相机和三个频率依次调用 6 次 `computeOneCuda()`，旧实现每次都重新创建 `requiredPhaseSteps + 3` 个 device buffer；当前数据共约 48 次 device buffer 生命周期。复用 workspace 是不改变数值算法的下一项独立假设。
- wrapped phase workspace 验证成立：stage median `8.828 -> 4.787ms`，稳定总耗时 `26.583 -> 22.325ms`，点数完全一致。
- pipeline 当前对完整点云的消费边界明确：
  - compact vertices 只用于 `--output` 和 `--compare-legacy`；
  - full-resolution `gridPoints` 只用于质量评价；
  - 普通 benchmark 的公开 `FrameResult` 只输出 `pointCloudVertexCount`。
- 因此无输出且质量关闭时，继续回传 filtered points/normals 并构造 vertices 属于未被消费的主机工作。下一项优化采用 GPU raw/smoothed/filtered count，不改变 GPU 匹配、重投影和过滤算法。
- count-only 验证成立：point-cloud stage median `9.323 -> 1.480ms`，稳定总耗时 `22.325 -> 14.368ms`；raw/smoothed/filtered 点数与旧路径完全一致。
- count-only 之后稳定阶段占比约为 validation `4.0ms`、wrapped `4.8ms`、unwrap `3.2ms`、point cloud `1.5ms`。
- unwrap 旧实现对左右相机分别创建和释放 9 个相同尺寸 device buffer；这 18 个逐帧 buffer 生命周期是当前可独立移除的开销。
- unwrap workspace 验证成立：stage median `3.237 -> 1.899ms`，稳定总耗时 `14.368 -> 12.979ms`，点数完全一致。
- 当前剩余稳定耗时约为 validation `3.96ms`、wrapped `4.72ms`、unwrap `1.90ms`、point cloud `1.65ms`。
- 真实 SourceImg 全部是 packed single-channel `UInt8`，但 validator 仍逐像素执行通用 element type switch、double 转换和 `std::isfinite`；这是 validation 热点的明确结构性原因。
- UInt8 专用统计路径验证成立：validation median `3.964 -> 2.735ms`，稳定总耗时 `12.979 -> 11.678ms`，validator 契约测试和点数均保持一致。
- 全量长跑显示稳定 run median `11.881ms`、p90 `18.344ms`；Legacy 核心 median `9.744ms`、p90 `10.671ms`，当前 median 仍慢约 `2.14ms`，且长跑 p90 波动明显更大。
- wrapped phase 当前仍回传 6 张 full-resolution modulation map 并在 CPU 扫描 mean/min/max，但 pipeline 后续完全不读取这些 modulation vectors；质量模块使用的是原始高频条纹重新计算的 modulation。
- modulation reduction 五帧验证后，稳定 run median 已降至 `9.533ms`，wrapped stage `4.688 -> 2.635ms`，点数完全一致。
- 最终候选全量长跑：run median `9.364ms`、p90 `9.918ms`，已优于 Legacy 核心 `9.744/10.671ms`；但 wall 仅 `31.961 FPS`，仍低于 Legacy `40.960 FPS`。
- 输出重定向到空设备后 wall 为 `31.480 FPS`，证明诊断文本写盘不是 wall 差距根因。
- 当前 `loadBmp8()` 使用 `std::istreambuf_iterator` 将每个 BMP 逐字符构造成完整文件 vector，再分配 packed pixels 并逐行复制；每帧调用 30 次，是相对 Legacy `cv::imread()` 的明确输入路径劣势。
- `loadElapsedMs` 实测 frame `101..104` median `21.079ms`，约占当前端到端每帧时间的三分之二；优化输入加载是 wall FPS 反超的必要条件。

## 2026-07-10 filter-only 最终性能与当前 Legacy 对比

- BMP loader 的根因修复为：不再用 `istreambuf_iterator` 逐字符读取完整文件，改为读取固定 54 字节 BMP header、一次性读取像素块，并使用 thread-local pixel buffer 复用容量。
- loader 优化后全量 `0..465` 长跑：
  - `loadElapsedMs` warm median/p90/p99：`5.1058/5.8168/7.2468ms`
  - `runElapsedMs` warm median/p90/p99：`9.1875/10.3028/11.3047ms`
  - validation/wrapped/unwrap/point-cloud warm median：`2.798/2.504/1.894/1.625ms`
  - wall：`6.855061s`
  - throughput：`67.978976 FPS`
- Legacy 无输出全量基线为 wall `11.377s`、`40.960 FPS`，核心 median/p90 `9.744/10.671ms`。新算法端到端吞吐快 `1.6596x`，核心 median/p90 分别快 `1.0606x/1.0357x`。
- loader 优化后的 466 帧点数与优化前 `benchmark.csv` 逐帧完全一致；状态仍为 `Ok=442`、`ReconstructionInsufficient=24`。
- 为避免历史输出版本混用，使用效率基线相同的 `TeethScanAlgorithmApp.exe` 和相同 `legacy_benchmark_no_output.json`，仅通过 CLI 开启 final 输出，生成 `build/big_battle_filter_only_full_20260710/legacy_current_final_output_v1/<frame>/depth_points.ply`。
- 当前 Legacy final 总点数为 `40,415,218`，新算法为 `36,546,920`，总点数 ratio `0.904286`；逐帧 ratio median/p10/p90 为 `0.905275/0.686913/1.004607`。
- 当前 Legacy 非零帧 `447`，新算法非零帧 `442`。新算法为零而 Legacy 仅保留少量点的 5 帧为 `0/1/2/445/446`，Legacy 点数分别 `29/73/464/24/21`；Legacy 零点而新算法非零的帧数为 `0`。
- 历史 `0.exr` 总点数为 `50,345,889`，对应 ratio `0.725917`；该输出不是当前效率基线二进制同次生成，因此不再作为当前公平点数结论。
- 当前 Legacy benchmark 配置只开启物理视差窗，关闭 LR 和 subpixel，且当前分支没有右相位单调穿越；新算法 filter-only 同时开启 LR、右相位单调穿越和 subpixel。新算法少约 `9.6%` 点与更严格有效性约束一致，不能仅按数量判定为能力退化。
- frame 100 完整物化和当前 Legacy PLY 比较保持：
  - generated/Legacy：`84,833/89,900`
  - nearest mean/RMS/P95：`0.0698/0.2090/0.2373mm`
  - centroid：new `(8.5836,-1.4132,109.2966)`，Legacy `(8.3659,-1.3030,109.4040)`
- count-only 仅用于无 PLY、无 Legacy compare、质量关闭的 benchmark 路径；frame 100 `--output + --compare-legacy` 已确认 `verticesMaterialized=true`、`normalComputed=true`，完整输出语义未被性能优化破坏。
- 最终验证：Release 全量构建成功，CTest `10/10` 通过，`git diff --check` 通过。
- 逐帧当前 Legacy 对比产物：
  - `build/big_battle_filter_only_full_20260710/current_legacy_comparison.csv`
  - `build/big_battle_filter_only_full_20260710/current_legacy_comparison_summary.txt`

## 2026-07-10 点云彩色纹理根因

- 当前 `assignColor()` 选择左相机最高 projector index 的单张相位灰度图，并复制到 `r/g/b`，因此输出不是 RGB 纹理。
- frame 100 实测：
  - 新 PLY `84,833` 点，RGB 三通道完全相等比例 `1.0`，unique colors=`151`。
  - Legacy PLY `89,900` 点，RGB 三通道完全相等比例 `0.0`，unique colors=`6164`。
- Legacy 左相机输入分配为 `(3 + FREQ * STEP)` 张图；最后三张辅助帧与相位图一起 remap。
- Legacy 合成顺序明确为 `L15 -> B`、`L16 -> G`、`L17 -> R`。
- Legacy 线性颜色校正矩阵为：
  - B：`[1.17255, -0.0715125, -0.168186, 23.8505]`
  - G：`[-0.143254, 1.20397, -0.308086, 35.2089]`
  - R：`[-0.077533, 0.0993569, 1.39308, 28.8695]`
- Legacy 校正后应用 `gamma=0.5` LUT，输出 `CV_8UC3 BGR`；点云绑定时使用 `r=pixel[2]`、`g=pixel[1]`、`b=pixel[0]`。
- 当前公开契约已经有 `StripeFrameGroup::leftColor`，可直接承载 raw packed BGR，不需要把三张颜色辅助帧伪装成 phase stripe。
- 当前点云 CUDA 已实现 `KK/Dist/R/P` 的 rectified-to-raw 映射，可复用相同数学给颜色整流，避免引入 OpenCV 运行依赖。
- Python 用当前标定、Legacy 矩阵和 gamma 复现 frame 100 后，BGR 均值接近 Legacy `2.png`；剩余 MAE 主要来自 Legacy 当前运行时的自适应 rectification canvas 与当前 YAML 固定 `R_L/P_L` 差异，不影响“颜色必须与本项目点云 rectified 坐标一致”的设计。

## 2026-07-10 彩色纹理真实数据结论

- 当前颜色输入不是普通相位条纹，而是三张左相机辅助曝光帧；`SourceImg` one-based projector index `16/17/18` 映射到文件 `L15/L16/L17.bmp`。
- loader 将三张灰度辅助帧打包为 owned `UInt8 BGR`，通过 `StripeFrameGroup::leftColor` 传入重建，避免把颜色帧伪装成 phase stripe。
- `includeColor` 是输出消费边界：只有 `--output` 或 `--compare-legacy` 需要完整 vertices 时才加载颜色；普通 count-only benchmark 即使配置启用颜色也不读取 `L15/L16/L17`。
- 无输出 frame `100..104` 回归显示 `colorTextureApplied=false`、`verticesMaterialized=false`，稳定帧 `loadElapsedMs` 约 `6..7ms`，点数保持不变。
- 完整物化 frame 100：
  - points=`84,833`
  - mean RGB=`[253.6555,205.7671,182.5587]`
  - std RGB=`[7.8145,21.5968,19.2287]`
  - equal RGB ratio=`0.0`
  - unique colors=`5,148`
- 当前 Legacy frame 100：
  - points=`89,900`
  - mean RGB=`[253.2076,203.7344,180.4113]`
  - std RGB=`[9.8192,22.6399,19.9227]`
  - equal RGB ratio=`0.0`
  - unique colors=`6,164`
- 全量 `0..465` 彩色输出耗时约 `104.57s`，包含 442 个 ASCII PLY 写盘，不用于无输出效率对比。
- 全量状态和点数保持此前 filter-only 口径：`Ok=442`、`ReconstructionInsufficient=24`、总点数 `36,546,920`、逐帧几何 mismatch=`0`。
- 代表帧 `50/100/200/300/400` 的 equal RGB ratio 均为 `0.0`，unique colors 分别为 `7397/5148/4213/1151/2255`。
- frame 200/400 的 R 通道均值接近饱和，说明当前参数忠实复现 Legacy 矩阵/gamma，但后续若追求纹理观感，需单独评估曝光或 highlight compression，不能在本阶段悄悄改矩阵。
- 规格审查首次发现 singleStripe phase-only 路径缺少测试；已增加只创建 `L/R 1..3.bmp`、不创建颜色辅助帧的回归，`includeColor=false` 时加载成功且颜色视图为空。
- 代码质量审查发现 `includeColor=true` 默认值会让旧四参数调用隐式启用颜色 I/O；已移除默认参数，所有调用方必须显式表达颜色消费需求。
- 两个 loader 原先依赖 `loadReconsConfig()` 保证 `colorTextureProjectorIndices.size()==3`；已增加本地校验，直接构造非法配置也返回 `ConfigInvalidValue`，不再可能越界访问。
- 极端损坏 BMP 的尺寸溢出仍是 P3 风险；当前真实设备 BMP、全量 Upper 数据和测试均未触发，本轮保留为后续 parser hardening 项。
