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
