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
