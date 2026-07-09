# 会话进度

## 2026-07-09

- 读取 `planning-with-files-zh`、`using-superpowers`、`writing-plans`、`brainstorming` 技能说明。
- 读取 `agent.md`，确认本轮要求：概要设计先行、Legacy 只做基线、CUDA 优先、显式数据契约、状态码、日志/参数读取参考 `D:\code\slam`。
- 检查当前仓库，发现新源码主体尚未建立，`legacy/teethscanalgorithm3x4` 为主要 Legacy 基线。
- 抽样读取 Legacy 的 `configs.h`、`reconstruct.h`、`reconstruct.cpp`、`calcPhaseUnwrap.h`、`calcDepthImage.h`、`temporal_sequence_validator.h`、`pplitesegAiSeg.h`、`projector_defocus_quality.h`、`teeth_log.h/cpp`。
- 抽样读取 `D:\code\slam` 的 `SlamSample.cpp` 日志初始化、`SlamInterface.cpp` 参数初始化、`ParamsJson.h` 和 `algPara.json`。
- 开始写入 `docs/planning/` 与 `docs/design/` 文档。
- 完成 `docs/design/2026-07-09-legacy-single-frame-reconstruction-overview.md`，并检查章节、模块图、状态码、验证口径和占位词。
- 根据评审意见修订概要设计：将 `src/app` 改为 `src/sample/reconstructSample.cpp`，日志改为仿照 Slam 引入第三方 Log/spdlog，标定定位为读取 MPS `calibResult.json`，公共头改为 `reconstructInterface.h`，配置沿用 `reconsAlgPara.json`，输入改为 `StripeImage` 数组，补充校正画布策略和 DSSI 下游适配方案。
- 写出第一阶段基础骨架执行计划：`docs/planning/2026-07-09-phase1-foundation-implementation-plan.md`。计划限定第一阶段只做工程骨架、接口契约、配置/日志/标定读取骨架、图像基础校验、pipeline dry-run 和 sample，不迁移相位/匹配/重建/AI 核心算法。
- 记录第一阶段评审结论：接受暂不链接 CUDA/TensorRT、当前 `reconsAlgPara.json` 解析字段足够、`CalibrationModel` 只做 `calibResult.json` 读取骨架、DSSI 第一阶段不实际修改；补充 C ABI 与 C++ API 差异说明和建议决策。

## 2026-07-09 第一阶段实施

- 按用户要求先执行 `git status --short`：起点显示 `.gitignore` 已修改，`agent.md` 与 `docs/` 未跟踪；本轮不清理、不覆盖这些既有工作区状态。
- 读取并遵循 `agent.md`、概要设计、第一阶段执行计划、`task_plan.md`、`findings.md`、`progress.md`。
- 调用并读取 `planning-with-files`、`using-superpowers`、`executing-plans`、`systematic-debugging`、`verification-before-completion` 技能说明；本轮按已有评审结论直接实施，不再停留在设计。
- 创建第一阶段源码目录：`include/reconstruct_one_frame/`、`src/config/`、`src/io/`、`src/logging/`、`src/diagnostics/`、`src/calibration_model/`、`src/image/`、`src/pipeline/`、`src/sample/`、`tests/`。
- 从 `D:\code\teethscanalgorithm3x4\config\reconsAlgPara.json` 复制到 `config/reconsAlgPara.json`。
- 写入最小 `CMakeLists.txt`、`reconstructOneFrame` 库目标、`reconstructSample` 示例程序目标和三个 CTest 测试目标。
- 写入 C++ 公共接口 `include/reconstruct_one_frame/reconstructInterface.h`：包含 `StatusCode`、`Status`、`ImageView`、`StripeImage`、`StripeFrameGroup`、`StageStats`、`FrameResult`、`ReconstructEngine`，并只预留 C ABI 注释区域。
- 写入配置、日志、标定读取骨架、图像校验、pipeline dry-run、Engine 封装、sample 和最小测试源码。

### 验证记录

- `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`：通过。Visual Studio 多配置生成器提示 `CMAKE_BUILD_TYPE` 未使用，属于 VS 生成器预期行为。
- `cmake --build build --config Release`：通过，生成 `build\Release\reconstructSample.exe` 和三个测试可执行文件。
- `build\Release\reconstructSample.exe --help`：通过，输出 dry-run sample 用法，并明确不读取真实扫描目录、不调用 Legacy DLL、不输出 PLY/EXR/PNG。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run-no-calib`：通过，返回 `StatusCode::Ok`，`dry_run_compute` 阶段明确 `NotComputed`，depth/normal/quality 均未计算。
- `ctest --test-dir build --output-on-failure`：在 Visual Studio 多配置生成器下失败，错误为 `Test not available without configuration. (Missing "-C <config>"?)`。
- `ctest --test-dir build -C Release --output-on-failure`：通过，3/3 tests passed。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --calib D:\some\missing\calibResult.json --dry-run`：按预期返回 `CalibrationMissing`，进程退出码为 1。

## 2026-07-09 第二阶段计划

- 读取 `planning-with-files-zh`、`writing-plans`、`brainstorming` 技能说明；本轮按用户要求只给出第二阶段实施计划文档，不进入代码实现。
- 复核第一阶段提交状态：`main...origin/main [ahead 1]`，最近提交为 `a52faae feat(foundation): 建立第一阶段重构骨架`。
- 复核第一阶段关键源码：`reconstructInterface.h`、`CMakeLists.txt`、`ReconsConfig.h`、`CalibrationModel.h`、`ImageValidator.h`、`SingleFramePipeline.h`。
- 写入第二阶段实施计划：`docs/planning/2026-07-09-phase2-input-calibration-preprocess-implementation-plan.md`。
- 第二阶段计划定位为输入契约、标定 JSON 宽松解析、预处理 dry-run 和诊断 summary；明确不迁移相位/匹配/重建/AI 核心算法，不接入 CUDA/TensorRT，不修改 DSSI。

### 第二阶段验证记录

- `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`：通过。Visual Studio 多配置生成器仍提示 `CMAKE_BUILD_TYPE` 未使用，属于预期。
- `cmake --build build --config Release`：通过，生成 `reconstructOneFrame.lib`、`reconstructSample.exe` 和 8 个测试可执行文件。
- `ctest --test-dir build -C Release --output-on-failure`：通过，8/8 tests passed。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib`：通过，返回 `StatusCode::Ok`；`calibration_contract_validation` 标记 skipped；`algorithm_not_computed` 明确 `NotComputed`。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --calib tests\data\phase2_valid_calibResult.json --dry-run`：通过，返回 `StatusCode::Ok`，标定契约检查未 skipped。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_missing_step_manifest.json --dry-run-no-calib`：按预期返回 `InputPhaseStepMissing`，退出码 1。

### 第二阶段实施修正记录

- 首次 `ctest` 后失败 3 项：`pipeline_smoke_test` 仍使用第一阶段单条纹输入，与第二阶段覆盖校验冲突；`image_preprocess_dry_run_test` 缺少 CTest 工作目录；`pipeline_phase2_smoke_test` 受 validator 覆盖 stageName 影响。
- 修正方式：将 sample 和 `pipeline_smoke_test` 的内置输入扩展为 3 频率 x 5 相移左右条纹；给 `image_preprocess_dry_run_test` 设置 repo 根目录工作目录；pipeline 在调用 validator 后恢复阶段名为 `input_contract_validation`。

## 2026-07-09 第三阶段实施

- 按用户确认执行第三阶段，继续遵循 `planning-with-files-zh`、`executing-plans`、`systematic-debugging`、`verification-before-completion`。
- 写入第三阶段实施计划：`docs/planning/2026-07-09-phase3-wrapped-phase-reference-implementation-plan.md`。
- 扩展公共接口：增加 `StatusCode::PhaseQualityInsufficient` 和 `FrameResult::wrappedPhaseComputed`。
- 新增 `src/phase/WrappedPhaseComputer.h/.cpp` 与 `src/phase/WrappedPhaseComputerCuda.cu`，建立 CPU 参考与 CUDA 包裹相位计算、调制统计和低调制质量闸门。
- 配置新增 `phaseStepCounts`，当前为 `[3,5,5]`，表达每个频率独立相移步数；`StripeRequirement` 记录 `firstProjectorIndex` 和 `phaseStepDirection`。
- sample 新增 `--single-stripe-root <path>` 与 `--group <n>`，用于读取真实 `D:\Data\Calib\2607011016_mach6\singleStripe\<group>\L/R\*.bmp`。
- pipeline 在 `image_preprocess_dry_run` 后新增 `wrapped_phase_compute_cuda` 阶段；成功后追加 `downstream_not_computed`，明确 unwrap/matching/reconstruction 未计算。
- `DiagnosticSummary` 增加 `wrappedPhaseComputed` 输出；测试新增 `wrapped_phase_computer_test` 和 `tests/data/phase3_low_modulation_manifest.json`。

### 第三阶段验证记录

- `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`：通过。
- `cmake --build build --config Release`：通过，生成 `reconstructOneFrame.lib`、`reconstructSample.exe` 和 9 个测试可执行文件。
- `ctest --test-dir build -C Release --output-on-failure`：通过，9/9 tests passed。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib`：通过，返回 `StatusCode::Ok`；包含 `wrapped_phase_compute_cuda`，`wrappedPhaseComputed=true`；`downstream_not_computed` 明确 `NotComputed`。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase3_low_modulation_manifest.json --dry-run-no-calib`：按预期在 `wrapped_phase_compute_cuda` 返回 `PhaseQualityInsufficient`，退出码 1。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_missing_step_manifest.json --dry-run-no-calib`：按预期返回 `InputPhaseStepMissing`，退出码 1。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --dry-run-no-calib`：通过，读取真实 BMP，返回 `StatusCode::Ok`；`inputImages=26`，`wrapped_phase_compute_cuda` 的 `cudaPixels=1017600`，`wrappedPhaseComputed=true`。

### 第三阶段风险记录

- CUDA 包裹相位当前只覆盖 wrapped phase 与 modulation，不代表完整 Legacy `GPUPhaseUnwrapper` 迁移。
- 低调制阈值尚未基于真实扫描数据标定，后续进入相位质量模块时需要重新统计。
- 仍未接入真实 MPS `calibResult.json` 样例验证字段覆盖。

## 2026-07-09 第四阶段实施

- 按用户要求实现第四阶段，目标收敛为真实数据 CUDA 相位展开迁移。
- 复核当前仓库起点：`main...origin/main [ahead 1]`，工作区已有第一至三阶段未提交变更；本轮不清理、不覆盖无关工作区状态。
- 读取并遵循 `planning-with-files-zh`、`executing-plans`、`systematic-debugging`、`verification-before-completion`、`cpp-pro` 技能说明。
- 调研 Legacy `D:\code\teethscanalgorithm3x4\include\calcPhaseUnwrap.h` 与 `src\calcPhaseUnwrap.cu`，确认 two-stage CUDA unwrap 入口为 `computePhaseDifferences()`、`getAbsPhaseGPU()` 和 `phaseUnwrapping()`。
- 扩展 `ReconsConfig`：读取 `freq23`、`Bmin`、`winSizeMedian`、`phaseUnwrapResidualGateEnabled`、`phaseUnwrapAbs23ResidualThreshold`、`phaseUnwrapFinalResidualThreshold`、`phaseFinalMedianFilterEnabled`。
- 扩展公共 `FrameResult`：新增 `unwrappedPhaseComputed`。
- 新增 `src/phase/PhaseUnwrapper.h` 与 `src/phase/PhaseUnwrapperCuda.cu`，实现 CUDA `PH12/PH23/PH123`、两级 absolute phase round unwrap 和可选 final median filter。
- pipeline 在 `wrapped_phase_compute_cuda` 后新增 `phase_unwrap_cuda` 阶段；成功后 `downstream_not_computed` 仅标记 matching/reconstruction 未计算。
- 更新 `DiagnosticSummary` 输出 `unwrappedPhaseComputed`；sample help 改为 phase-4 CUDA phase-unwrap dry-run。
- 新增 `tests/phase_unwrapper_test.cpp`，并将 CTest 总数推进到 10 项。

### 第四阶段验证记录

- `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`：通过。
- 首次 `cmake --build build --config Release`：失败，`PhaseUnwrapperCuda.cu` 中 `CUDART_NAN_F` 未定义。
- 修正方式：补充 `#include <math_constants.h>`。
- 再次 `cmake --build build --config Release`：通过，生成 `reconstructOneFrame.lib`、`reconstructSample.exe` 和 10 个测试可执行文件。
- `ctest --test-dir build -C Release --output-on-failure`：通过，10/10 tests passed。
- `build\Release\reconstructSample.exe --help`：通过，显示 phase-4 CUDA phase-unwrap dry-run sample。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib`：通过，返回 `StatusCode::Ok`；包含 `wrapped_phase_compute_cuda` 和 `phase_unwrap_cuda`；`unwrappedPhaseComputed=true`。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase3_low_modulation_manifest.json --dry-run-no-calib`：按预期返回 `PhaseQualityInsufficient`，退出码 1；未进入 unwrap。
- `build\Release\reconstructSample.exe --config config\reconsAlgPara.json --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --dry-run-no-calib`：通过，读取真实 BMP，返回 `StatusCode::Ok`；`inputImages=26`，`wrapped_phase_compute_cuda` 的 `cudaPixels=1017600`，`phase_unwrap_cuda` 的 `cudaPixels=339200`，`wrappedPhaseComputed=true`，`unwrappedPhaseComputed=true`。

### 第四阶段风险记录

- 当前 CUDA unwrap 是 Legacy two-stage 公式迁移，不代表完成匹配/重建。
- 当前未输出 absolute phase 文件；验证依赖 stage summary 和测试断言，后续建议增加诊断图/统计导出以便和 Legacy 数值对比。
- 当前 CUDA buffer 仍按调用临时分配，后续进入实时路径前应改为可复用 GPU workspace。

## 2026-07-09 第五阶段实施

- 按用户要求完成第五阶段：使用真实 `D:\Data\Calib\2607011016_mach6\singleStripe`，走 CUDA 点云重建，并与历史 Legacy 点云对比。
- 新增 `src/reconstruction/PointCloudReconstructor.h` 与 `src/reconstruction/PointCloudReconstructorCuda.cu`，实现 CUDA disparity、Q reproject、可选 local consistency、法向估计和点云统计。
- 新增 `src/io/PlyIO.h/.cpp`，支持 ASCII PLY 写出、Legacy PLY 读取和点云比较指标。
- 扩展 `CalibrationModel`，支持 OpenCV matrix JSON object，并支持直接读取 Legacy `calibParams.yml`。
- 扩展 `ReconsConfig`，读取 disparity/matching/point-cloud filter/smoothing 等阶段五配置字段。
- 扩展 `FrameResult` 与 `DiagnosticSummary`，输出 `pointCloudVertexCount`、`outputPointCloudPath` 和 `legacyComparisonSummary`。
- sample 增加 `--output <dir>` 与 `--compare-legacy <ply>`；pipeline 新增 `point_cloud_reconstruct_cuda`、`ply_output`、`legacy_point_cloud_compare` 阶段。
- 新增 `tests/point_cloud_reconstructor_test.cpp` 和 `tests/data/phase5_opencv_matrix_calibResult.json`。

### 第五阶段验证记录

- `cmake --build build --config Release`：通过，生成 `reconstructOneFrame.lib`、`reconstructSample.exe` 和 11 个测试可执行文件。
- `ctest --test-dir build -C Release --output-on-failure`：通过，11/11 tests passed。
- `build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --output build\phase5\group1_calibparams_yml --compare-legacy D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply`：通过，返回 `StatusCode::Ok`。
- 真实运行阶段：`input_contract_validation` 读取 30 张图，`wrapped_phase_compute_cuda` 处理 `1017600` pixels，`phase_unwrap_cuda` 处理 `339200` pixels，`point_cloud_reconstruct_cuda` 输出 `96160` 点。
- 输出点云：`build\phase5\group1_calibparams_yml\depth_points.ply`，文件大小约 5.18 MB。
- Legacy 对比：generated `96160`，legacy `101528`，vertexDelta `-5368`；nearest mean `0.0535mm`，RMS `0.0626mm`，P95 `0.1029mm`；generated centroid `(7.0393,-0.7647,99.9491)`，legacy centroid `(6.7325,-0.5746,96.4029)`。

### 第五阶段修正记录

- 首轮 raw/filtered 对比发现 filter 将点数压到约 5k，远低于 Legacy 101k；改为默认关闭 pointCloudFilter/pointCloudSmoothing 做 raw reproject 对比。
- 对照 Legacy 发现 `GPUPhaseUnwrapper` 在相位计算前 remap 图像；当前阶段补入 absolute phase remap/rectification 近似后，几何误差明显下降。
- `calibResult.json` 可以运行但历史点云更贴近 `calibParams.yml` 的 R/P/Q；已支持 `--calib D:\Data\Calib\2607011016_mach6\calibParams.yml` 直读。
- MSVC 构建失败于 `std::regex::multiline`，已改为兼容 MSVC 的换行锚点正则并重新验证通过。

### 第五阶段风险记录

- 当前 remap 是 post-unwrap absolute phase remap 近似，Legacy 是 remap intensity 后再算相位；最近邻指标已贴近，但逐点顺序/洞分布仍可能不同。
- 与 Legacy 仍有 `5368` 点数差；几何最近邻误差已到 `0.1mm` P95 量级，后续若追逐点一致，需要继续迁移 Legacy smoothing/filter/hole 规则。
- 当前颜色写出为灰度来源，不代表 Legacy 最终 RGB/纹理合成。
- 本轮未修改 `D:\code\dentalscanserviceinterface`，也未 commit。

## 2026-07-09 第六阶段实施

- 按用户要求完成第六阶段：仿照 Legacy 单帧点质量和整帧质量评价。
- 调研 Legacy `legacy\teethscanalgorithm3x4\include\point_reliability_map.h` 与 `legacy\teethscanalgorithm3x4\src\point_reliability_map.cpp`，确认 reason bits、coverage、center ROI、largest connected component、boundary ratio、percentile 和 sentinel 公式。
- 新增 `src/quality/PointReliability.h/.cpp`，实现点级 `PointReliabilityPixel{semanticClass, score, reason}` 和 `SingleFrameQualityStats`。
- 扩展 `PointCloudReconstructionResult`，新增每像素 `gridPoints`，记录 xyz、normal、matchCost、modulation、candidateCount，供质量评价判断 depth range、match cost、candidate ambiguity、low modulation、hole edge。
- 扩展 `ReconsConfig`，读取 `qualityInfo*` 与 Legacy `qualityMap*` 字段，包括 min modulation、reason channel、phase cost threshold、candidate count、hole edge radius、high confidence threshold。
- 扩展公共 `FrameResult`：新增 `qualitySummary`；`DiagnosticSummary` 输出整帧质量摘要。
- pipeline 在点云重建后新增 `quality_evaluate` 阶段，并将 `qualityComputed=true`。
- sample help 更新为 phase-6 CUDA reconstruction and quality-evaluation sample。
- 新增 `tests/point_reliability_test.cpp`，验证 high match cost、candidate ambiguous、depth range、sentinel 和 summary。

### 第六阶段验证记录

- `cmake --build build --config Release`：通过，生成 `reconstructOneFrame.lib`、`reconstructSample.exe` 和 12 个测试可执行文件。
- `ctest --test-dir build -C Release --output-on-failure`：通过，12/12 tests passed。
- `build\Release\reconstructSample.exe --help`：通过，显示 phase-6 CUDA reconstruction and quality-evaluation sample。
- `build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --output build\phase6\group1_quality --compare-legacy D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply`：通过，返回 `StatusCode::Ok`。
- 真实运行新增阶段：`quality_evaluate,status=Ok,inputImages=1,validImages=96160,rejectedImages=73439`。
- 质量结果：`qualityComputed=true`；`frameQualityScore=0.766212`，`pointQualityScore=0.851249`，`coverage=0.566984`，`centerCoverage=0.737728`，`connectedSurfaceScore=0.492734`，`validPoints=96160`，`centerValidPoints=45326`，`highConfidenceRatio=0.0534006`，`meanScore=0.518172`，`p50=0.58136`，`p90=0.814779`，`p95=0.906386`，`largestConnectedComponent=86099`，`boundaryRatio=0.449688`，`lowQualityReasonBits=1922`。
- 输出点云：`build\phase6\group1_quality\depth_points.ply`，大小约 5.18 MB。
- Legacy 几何对比仍保持第五阶段结果：generated `96160`，legacy `101528`，nearest RMS `0.0626mm`，P95 `0.1029mm`。

### 第六阶段风险记录

- 当前质量评价是 CPU 汇总，不是 CUDA kernel；后续若进入实时路径，需要考虑 GPU 侧质量图生成或减少 D2H 数据。
- 当前没有 semantic/AI mask，因此真实数据不会触发 `ReasonSemanticBackground`。
- 当前 sample 只输出整帧质量摘要，不落盘 per-pixel quality map；内部 `PointReliabilityResult::map` 已存在。
- 高频 modulation 基于左侧最高频条纹原图计算，未完全复制 Legacy remap intensity 后再计算质量的时序。

## 2026-07-09 Legacy 大比拼：output260707 gypsum

- 已执行 `git status --short`：当前工作区包含阶段 2-6 的既有修改和未跟踪文件；本轮继续保留，不清理、不提交。
- 已确认 `D:\Data\output260707_gypsum\Upper\<frame>\SourceImg` 数据结构；`Upper\SourceImg` 这个根级目录不存在。
- 已确认 `D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json`、`D:\Data\Calib\2607011016_mach6\calibParams.yml`、`calibResult.json` 均存在。
- 已新增 `loadSourceImgFrameDirectory()`，按 `projectorIndex=1 -> L0/R0` 映射读取 `Upper\<frame>\SourceImg`。
- 已新增 sample 参数 `--source-img-root <path>` 与 `--frame <n>`，并输出 `runElapsedMs` 和 stage `elapsedMs`。
- 已新增 `scripts/benchmark_output260707_gypsum.py`，用于统计 Legacy EXR 点数、新算法点数、点数比例、sample 热路径耗时和进程总耗时。
- `cmake --build build --config Release`：通过，生成更新后的 `reconstructSample.exe`。
- `ctest --test-dir build -C Release --output-on-failure`：通过，12/12 tests passed。
- `build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --source-img-root D:\Data\output260707_gypsum\Upper --frame 100`：通过，frame 100 输出 `135889` 点，Legacy `0.exr` 有效点为 `124032`。
- `python scripts\benchmark_output260707_gypsum.py --frames 0:465 --output build\big_battle_output260707_gypsum_full`：通过，全量 466 帧完成；可比 447 帧，点数低于 95% 的帧为 0，总点数比例 `1.166876`，最低单帧比例 `0.977822`。
- 补充分阶段计时后，frame 100 新算法阶段耗时：`wrapped_phase_compute_cuda=74..76ms`（首帧 CUDA 初始化影响）、`phase_unwrap_cuda≈3.4ms`、`point_cloud_reconstruct_cuda≈20.3ms`、`quality_evaluate≈19.2ms`。
- `reconstructSample --source-img-root ... --first 100 --last 104`：通过，同进程批量模式下新算法 frame 101..104 单帧约 `56ms`，首帧约 `124ms`。
- 用临时配置 `build\big_battle_output260707_gypsum_full\legacy_run_config_fixed_calib.json` 修正 Legacy `calibParamsPath` 后，`TeethScanAlgorithmApp` 跑 frame `100..104` 成功；Legacy app 热路径 median `434.659ms`、mean `439.985ms`。
- 检查尾部 frame 447：Legacy 原始 `0.exr` 为 0 点，修正配置重跑 Legacy 仍为 0 点；新算法输出 `85256` 点但 `frameQualityScore=0.171903`，记录为“低质量帧是否硬拒绝”的后续策略风险。
