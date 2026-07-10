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

## 2026-07-10 Legacy 大比拼差距复核与修复计划

- 读取并遵循 `planning-with-files-zh`、`brainstorming`、`writing-plans`，本轮只总结差距和修复计划，不修改算法实现。
- 复核 `build\big_battle_output260707_gypsum_full\summary.txt` 和 `benchmark.csv`。
- 首次 PowerShell 聚合误用了 snake_case 字段名，得到无效统计；改用 CSV 实际 camelCase 字段后重算。
- 首次批量写规划文件因 `findings.md` 锚点不匹配而失败；改为按文件精确尾部追加。
- 447 个可比帧点数比例：min `0.977822`、P10 `1.017720`、median `1.099007`；11 帧少于 Legacy，2 帧低于 98%，没有帧低于 95%。
- benchmark 原有 `point_count_losing_frames=0` 只统计低于 Legacy 95% 的帧，名称不准确。
- sparse/zero 差异：Legacy `<100` 点 21 帧、`<1000` 点 23 帧；Legacy zero/new nonzero 19 帧；点数比例 `>2x` 30 帧、`>10x` 5 帧。
- 重新运行 frame 0、446、447，确认新算法仍返回 `StatusCode::Ok` 并输出大量点云；用户指出这些点可能代表相对 Legacy 的提升，因此不再以 Legacy 稀疏直接判定误输出。
- 写入修复计划：`docs/planning/2026-07-10-legacy-big-battle-remediation-plan.md`。
- 优先级修正：P0 benchmark 口径和 extra-only 点独立验证；P1 全量几何比较和 exact remap/matching 对齐；P2 在证据充分后再设计 reject 与 CUDA 工程化。

## 2026-07-10 新算法点云盯帧

- 扩展批量 sample 输出：当 `--first/--last` 为多帧且指定 `--output` 时，按 `<output>/<frame>/depth_points.ply` 写出，避免连续帧互相覆盖。
- 扩展 `D:\tools\exr_triplet_viewer`：支持配置 `source_type=ply`、`frame_dir_pattern`、`point_file`，并增加 ASCII PLY XYZ 读取。
- `cmake --build build --config Release`：通过，CUDA 12.8 / MSVC Release 构建成功。
- `ctest --test-dir build -C Release --output-on-failure`：通过，10/10 tests passed。
- `python -m unittest tests.test_exr_triplet_core -v`：通过，9/9 tests passed。
- `python -m py_compile exr_triplet_core.py exr_triplet_viewer.py`：通过。
- 用真实数据 `D:\Data\output260707_gypsum\Upper` 和真实标定 `D:\Data\Calib\2607011016_mach6\calibParams.yml` 生成 `0..465` 全量点云。
- 全量输出检查：466 个数字帧目录、466 个 `depth_points.ply`、无缺帧，总大小 `3,498,896,996` bytes。
- 输出根目录：`D:\code\reconstructOneFrame\build\watch_output260707_gypsum`。
- 生成日志：`D:\code\reconstructOneFrame\build\watch_output260707_gypsum\generate_0_465.log`。
- 查看器配置已切换到新算法 PLY：`D:\tools\exr_triplet_viewer\exr_triplet_config.json`。
- 查看器已启动，进程 ID `72344`，中心帧 1 显示帧 `0/1/2`，左右方向键可连续切帧。
- 本轮没有提交代码。

## 2026-07-10 远程 Legacy 错误相位匹配调研

- 执行 `git status --short`，确认当前仓库已有规划、接口和 pipeline 的既有未提交修改；本轮保留全部现状。
- 读取 `planning-with-files` 与 `superpowers/using-superpowers` 工作流，恢复 `task_plan.md`、`findings.md`、`progress.md` 上下文。
- 枚举 Legacy 远程相位/匹配相关分支，当前重点为 `origin/zheng/ParallaxMatching`，另记录 `origin/zheng/denoise` 和 `origin/v1.0.0`。
- 已开始提交谱系与生产默认配置核对；本轮暂未修改算法源码，也未提交代码。
- 核对远程 ancestry：`origin/main=5410c143`，`origin/zheng/ParallaxMatching=52bda09c`，后者是前者祖先，说明相关改动已经进入最新主线。
- 查看 `998cb06`、`351d8a1`、`2f6d674`、`6ea30e6`、`52bda09`、`bd92c6f`、`6da2003` 的变更统计，已将调研重点收敛到匹配 CUDA 热路径和生产 JSON 默认组合。
- 逐行核对 `998cb06`：确认亚像素插值失败时旧实现会从 `best_cor_x_float=0` 继续计算，新实现回退到整数最佳候选并记录诊断计数。
- 逐行核对 `351d8a1`：确认新增由 `Q + minZ/maxZ + margin` 推导的物理视差搜索窗口，约束发生在候选搜索阶段。
- 逐行核对 `2f6d674` 和最新 `origin/main:src/calcDepthImage.cu`：确认左右一致性从整数邻域反查演进为可配置亚像素反查。
- 确认最新主线存在 `checkRightPhaseMonotonicSupport()`，并在亚像素插值前直接拒绝缺少局部相位穿越支持的候选，同时输出 failure map 和 rejection counter。
- 读取最新 `origin/main:config/reconsAlgPara.json`，确认生产默认开启物理视差窗、左右一致性、右相位单调穿越和亚像素路径，其他实验约束默认关闭。
- 搜索当前项目配置与 CUDA 实现，确认当前缺少右相位单调穿越，且历史配置关闭左右一致性和亚像素。
- 阅读当前 `PointCloudReconstructorCuda.cu` 匹配核：确认左右一致性仅整数反查，正向亚像素已有安全整数回退，且没有右相位单调穿越。
- 复核 `build/constraint_probe/summary.csv`：window、window+LR、window+LR+uniqueness 均能减少 frame 0/100/447 点数，但 frame 447 仍保留 `48787` 点。
- 完成远程 Legacy 核心改动学习和迁移优先级总结；仅修改 `docs/planning/task_plan.md`、`findings.md`、`progress.md`，未修改算法源码，未提交。
- 最终执行 `git diff --check -- docs/planning/task_plan.md docs/planning/findings.md docs/planning/progress.md`：退出码 0，无补丁空白错误。
- 使用 `rg` 确认远程调研、生产默认开关、消融数据和核心结论均已写入规划文件。
- 最终 `git status --short` 仍显示进入本轮前已有的接口/pipeline/sample 修改和 remediation plan；本轮未触碰这些非规划文件。
- Git 仅提示规划 Markdown 下次可能由 LF 转 CRLF，以及用户全局 ignore 文件无读取权限；不影响本轮只读调研和文档更新。

## 2026-07-10 错误相位匹配核心约束实施

- 用户确认按远程调研结论继续实施。
- 开始前再次执行 `git status --short`，保留已有规划、接口、pipeline 和 sample 未提交修改，不清理、不覆盖。
- 本轮目标：迁移右相位单调穿越、升级亚像素左右一致性、对齐生产默认开关，并用真实 frame `0/100/447` 消融验证。
- 本轮不启用 uniqueness、candidate quality、row DP 或整帧硬 reject，不提交代码。
- 首次提取 Legacy helper 的 PowerShell 命令误将源码数组作为单个 `Select-String -InputObject` 对象，函数行号错误显示为 1；改为逐行索引后成功取得完整函数。
- 已核对 Legacy 亚像素左右反查和右相位单调穿越的精确实现。
- 已确认本地配置解析、CUDA kernel 参数、结果结构和 `point_cloud_reconstructor_test` 是本轮最小改动面。
- 已在 CUDA 匹配核中迁移右相位单调穿越，并将左右一致性升级为亚像素反查。
- 新增设备侧 LR/单调穿越拒绝计数，D2H 后形成 `matchingSummary` 并传入公共 `FrameResult`。
- `ReconsConfig` 和默认 JSON 已对齐生产组合：视差窗、LR、单调穿越、亚像素默认开启；其他实验约束保持关闭。
- `DiagnosticSummary` 已输出 `matchingSummary`，真实 sample 可直接看到两类拒绝计数。
- 扩展 `config_config_load_test` 校验生产默认组合。
- 扩展 `point_cloud_reconstructor_test` 覆盖正确单调穿越、平坦候选拒绝，以及 `0.6px/0.4px` 亚像素 LR 容差边界。
- `cmake --build build --config Release`：通过，CUDA 12.8 / MSVC Release 编译成功。
- 首次 `ctest --test-dir build -C Release --output-on-failure`：9/10 通过；仅 `point_cloud_reconstructor_test` 的“正确穿越场景 rejection count 必须为 0”断言失败。
- 当前先增加合成场景计数输出定位边界拒绝，不修改 CUDA 算法假设。
- 单测直跑结果：`crossingVertices=112`、`matchingLeftRightRejected=0`、`matchingRightPhaseMonotonicRejected=8`。
- 确认 8 个拒绝为每行一个缺少外侧支持的边界候选，已将断言改为“仅边界拒绝”，并移除临时输出。
- 重新编译 `point_cloud_reconstructor_test` 并运行全量 CTest：10/10 tests passed，0 failures。
- 合成回归已验证：正确单调穿越保留、平坦相位拒绝、亚像素 LR `0.6px` 通过且 `0.4px` 拒绝。
- 检查真实 probe 配置：`window_lr.json` 已开启 window/LR/subpixel、关闭 uniqueness；monotonic 字段缺失。
- 决定生成显式 `window_lr_monotonic.json`，避免真实回归依赖 C++ 缺省值。
- 生成 `build/constraint_probe/window_lr_monotonic.json` 与 `window_lr_no_monotonic.json`，仅切换 monotonic 开关。
- 使用当前 Release 二进制完成 frame `0/100/447` A/B；所有运行均返回 `StatusCode::Ok`。
- monotonic on 点数为 `27464/118756/30629`，off 点数为 `56411/126520/61347`。
- 完整运行日志分别位于 `build/constraint_probe/window_lr_monotonic_runs` 和 `build/constraint_probe/window_lr_no_monotonic_runs`。
- 提取点云阶段耗时：monotonic on 为 `20.15/20.18/19.10ms`，off 为 `18.27/19.42/18.68ms`。
- 生成 frame `0/100/447` 新 PLY，路径为 `build/constraint_probe/window_lr_monotonic_ply/<frame>/depth_points.ply`。
- 对比新旧 PLY Z 统计：正常 frame 100 的分布明显收敛，但 frame 0/447 仍覆盖约 `84..154mm` 完整深度范围。
- 结论：本轮约束迁移有效且成本低，但没有根治 sparse/zero 帧的深度幕布；后续优先 exact intensity remap 和 phase validity 对齐。
- 启动独立只读代码审查；首次参数组合不兼容，去掉显式 reviewer 类型后成功启动。
- 结构图工具未解析出 CUDA/C++ 函数变化，已明确不使用其风险评分。
- 聚焦 diff 自审发现 failure path 会在复制 `matchingSummary` 前返回；已将摘要复制移动到 status 检查前，确保零点/失败帧仍输出拒绝计数。
- 最终 `cmake --build build --config Release`：通过。
- 最终 `ctest --test-dir build -C Release --output-on-failure`：10/10 tests passed。
- 最终 `reconstructSample --help`：通过；真实 frame 447 smoke 返回 Ok、`30629` 点、LR reject `24133`、monotonic reject `30838`。
- 最终 `git diff --check`：通过；未提交代码。
- 独立审查代理两次等待超时，已准备收窄审查范围，不将超时视为审查通过。
- 收窄后独立审查返回 5 项 Important 和 1 项性能建议。
- 已修复：平坦且完全相等相位绕过、亚像素外插、相位数组长度不足、旧配置缺 key 静默启用、公开结果字段插入顺序。
- 新增旧配置兼容、exact-flat rejection、短 phase 数组安全回归；全局 atomic 性能建议暂记录，因真实增量低于 2ms 未在本轮复杂化。
- 审查补丁完成后重新执行 `cmake --build build --config Release`：通过，CUDA 12.8 / MSVC Release 编译成功。
- 重新执行 `ctest --test-dir build -C Release --output-on-failure`：通过，`10/10` tests passed。
- 使用真实数据 `D:\Data\output260707_gypsum\Upper` 和标定 `D:\Data\Calib\2607011016_mach6\calibParams.yml` 重新运行 frame `0/100/447` 的 monotonic on/off 消融，六次运行均返回 `StatusCode::Ok`。
- 审查后 monotonic on 点数为 `27430/118748/30581`，off 点数为 `56413/126520/61347`。
- 审查后 LR rejection 为 `18683/9873/24133`，monotonic rejection 为 `29125/7813/30887`。
- 最新 point-cloud stage 耗时：monotonic on `17.551/19.953/18.651ms`，off `18.826/21.235/18.989ms`；当前证据未显示效率落后。
- 最新输出和日志位于 `build/constraint_probe/post_review/{monotonic_on,monotonic_off}/{0,100,447}/`。
- 完成六个 PLY 的 Z 分位数统计；frame 0/447 在 monotonic on 后仍覆盖约 `84..154mm` 完整工作深度，深度幕布尚未根治。
- 阶段 53 已完成：本轮核心约束迁移、审查修复、合成回归、真实消融和风险记录均已闭环；未提交、未推送。

## 2026-07-10 Legacy 逐阶段乱点根因追踪

- 用户肉眼确认 frame 0 几乎全是乱点，frame 100 虽有较多无效点但确实重建出物体；开始逐阶段核对 Legacy。
- 已确认 `reconstructOneFrame` 原有未提交修改边界，未清理或覆盖。
- 已确认 Legacy 工作区干净，当前 `feature/single-frame-quality@27613a3`，远程最新主线 `origin/main@5410c14`。
- 本轮先追踪输入、相位、匹配、重建、过滤和质量完整链路，再决定下一项实现，不先叠加阈值。
- 已定位 Legacy 入口：`Reconstruct::init/reconstructOne`、`GPUPhaseUnwrapper`、`computePointCloudFromPhase`。
- 已检查 frame 0/100 历史输出目录；只有最终 EXR/PNG，没有可直接用于定位的相位/视差中间图。
- 已发现当前 Legacy Release 可执行文件晚于历史输出，后续会同时使用源码追踪和当前分支重跑，并明确版本差异。
- 已完整读取 `Reconstruct::init/reconstructOne/finalizeReconstructionOutputs` 的阶段编排。
- 确认默认 disparity 路径跳过原图 phase-quality gate，AI 关闭时也没有 semantic 背景删除。
- 确认后置 quality map 只评分不删点；下一步重点转向 `GPUPhaseUnwrapper::unwrapPhase()` 和 `computePointCloudFromPhase()` 内部有效性规则。
- 已读取 `GPUPhaseUnwrapper` remap、调制度、wrapped phase、two-stage unwrap 和整帧质量判定。
- 确认 Legacy 在强度域先 remap；低调制度按频率写 NaN，但高低亮 flags 仅供质量图使用。
- 确认整帧 unwrap 失败阈值为相位跳变边比例 `>80%`，不足以解释或阻止连续错误相位形成的乱点。
- 已读取 two-stage residual/NaN 传播和当前质量分支的完整 disparity candidate kernel。
- 确认当前质量分支缺少右相位单调穿越，LR 为整数反查，亚像素仍可能外插。
- 下一步核对真实运行配置是否开启这些可选约束，并继续读取重投影、固定点过滤、平滑、补洞和法向生成。
- 已核对仓库、Release 和大比拼 Legacy 配置：residual/uniqueness/LR/subpixel/local consistency 全部关闭。
- 已读取 disparity 重投影、Z gate、固定高斯平滑、局部点支持过滤、金属补洞、法向和 quality-map 构建顺序。
- 确认石膏非金属帧不补洞；连续错误幕布能通过局部邻域过滤，是 frame 0 乱点仍可能成片存在的原因。
- 已读取 `nonConnectedComponentFilterKernel` 精确规则和最终点云压缩条件。
- 首次搜索 Legacy App CLI 时误在新项目目录执行，已标记该结果无效，下一步切回 Legacy 仓库重查。
- 已在正确 Legacy 仓库读取 `sample/teeth_scan_algorithm_app.cpp`，确认单帧运行参数、SourceImg 读取和输出文件语义。
- 已确认可以仅通过临时 JSON 开启现有 debug maps 和 raw/nofill/final 点云，不修改 Legacy 源码。
- 已生成 `build/legacy_step_trace/baseline_debug/legacy_debug_baseline.json`，只打开 debug/stage 输出，保持历史匹配与深度阈值不变。
- 已成功重跑真实 frame 0 和 100，均返回 status 0，并保存 debug maps、raw/nofill/final 点云。
- frame 0：raw/reprojection `31086` 点，经固定平滑后 filter input `87014`，最终只保留 `29` 点。
- frame 100：raw/reprojection `131540` 点，经固定平滑后 filter input `145286`，最终保留 `89900` 点。
- 当前首要差异已从“Legacy 匹配完全不出错”修正为“Legacy 也出 raw 错点，但后置连续面过滤强力压制无支撑帧”。
- 已确认 Gaussian point smoothing 会在空洞位置生成临时 `(0,0,z)`，随后由局部三维邻域过滤删除。
- 已解析 frame 0/100 的 debug maps 并生成 `debug_map_stage_stats.csv`。
- frame 0 高频 modulation 中位数仅约 `5.48/4.52`，frame 100 为 `32.97/30.94`；历史 quality gate 关闭使低信号 frame 0 仍进入匹配。
- phase cost 分布在 frame 0/100 高度重叠，证明仅靠 `phaseDiffThreshold=0.2` 不能解决乱点。
- 已确认新算法当前盯帧配置关闭点云平滑和过滤，输出语义是 raw，而 Legacy 历史 `0.exr` 是 final filtered depth。
- 已核对最新 Legacy `origin/main`：新增匹配约束后仍固定执行相同高斯平滑和局部点过滤。
- 已读取新算法现有点云平滑/过滤 CUDA：过滤规则接近 Legacy，但平滑会插值完整 XYZ，与 Legacy 的 Z-only 平滑不同。
- 首次 filter 消融在 frame 0 得到 0 点和 `ReconstructionInsufficient`；脚本因把该预期状态当异常而提前停止，下一次将继续收集其余组合。
- filter-only frame 100 成功输出 `84833` 点，接近当前 Legacy final `89900` 点。
- `smooth_filter.json` 尚未生成导致两次 `ConfigMissing`，已定位为前一轮提前退出的连锁结果，下一步重新生成后补跑。
- 已补建并运行 smooth+filter：frame 0=`3793`，frame 100=`94529`；确认现有 XYZ smoothing 会在 frame 0 过度填点。
- 已读取 2026-07-07 历史 EXR：frame 0=`50` 点，frame 100=`124032` 点。
- 已完成 Legacy 输入、相位、匹配、重投影、平滑、过滤、法向、质量和输出的逐阶段追踪。
- 已写入 `docs/planning/2026-07-10-legacy-stage-by-stage-root-cause-analysis.md`，阶段 54-58 完成；本轮未修改算法源码、未提交。

## 2026-07-10 Legacy 最终点云收敛机制实施

- 恢复 `task_plan.md`、`findings.md`、`progress.md` 和当前 `git diff`，确认工作区仍包含匹配约束阶段的既有未提交修改。
- 读取并遵循 `planning-with-files`、`using-superpowers`、`brainstorming`、`executing-plans`、`systematic-debugging`、`verification-before-completion` 和 `cpp-pro`。
- 用户已在上一轮批准实施方向，本轮按既定根因直接执行，不重新停留在设计。
- 确认当前仓库是普通 `main` checkout，不是 linked worktree；因现有阶段代码未提交，按已记录决策继续原地修改。
- 逐行对照当前 `gaussianBlurPointKernel` 与 Legacy `gaussianBlurKerneltoPt`，确认根因是当前实现平均完整 XYZ、跳过无效点并重新归一化权重。
- 逐行复核 Legacy `nonConnectedComponentFilterKernel`，确认空洞 `(0,0,z)` 会作为 filter input，但因与真实邻域三维距离不连续而被删除。
- 首次规划文件补丁调用为空，工具拒绝且未产生修改；已改用完整补丁重试。
- 已完成测试先行红绿循环第一轮：旧 XYZ smoothing 在新增回归中失败于“中心 X 必须保持不变”；改为 Z-only 后该测试通过。
- Release 全量构建通过，CTest `10/10` 通过。
- 完成真实 frame `0/100/447` 三组合 A/B：raw=`27430/118748/30581`，filter-only=`0/84833/0`，对称 Z-only smooth+filter=`0/60324/0`。
- 真实结果说明 filter-only 当前最接近 Legacy；对称 Z-only smoothing 对正常 frame 100 过度削减。
- 继续核对 Legacy 常量与调用后发现：实际 3x3 smoothing 错位索引 7x7 `gaussianKernel` 的前 9 项，未使用已定义的 `gaussianKernelS`；最新 `origin/main` 同样如此。
- 已将测试期望切换到最新 Legacy 实际使用的历史权重，确认对称核失败后替换 CUDA kernel 权重，回归恢复通过。
- 新增棋盘稠密面回归，确认当前 filter 重复应用 Z gate 会错误清除平滑后的非零连续点；移除重复 Z gate 后回归通过。
- 重新运行 Legacy-compatible smooth+filter：frame `0/100/447` 为 `0/62,134/2`，仍不如 filter-only 对正常帧的保留率。
- 最终生产决定为 `smoothing=false, filter=true`；真实 filter-only frame `0/100/447` 为 `0/84,833/0`。
- filter-only frame 100 与当前 Legacy final 比较：generated `84,833`、Legacy `89,900`、nearest mean `0.0698mm`、RMS `0.2090mm`、P95 `0.2373mm`。
- frame 100 Z 分位数 filter-only P5/P50/P95=`106.3836/109.1723/111.3925mm`，Legacy=`106.6240/109.1080/111.1720mm`，主表面深度分布高度接近。
- 新增 `pointCloudSummary`，失败帧也会输出 `rawValid/smoothedValid/filteredValid/smoothingApplied/filterApplied`。
- 真实验证产物根目录：`build/legacy_step_trace/z_only_validation`；生产候选 PLY 为 `filter_only/100/depth_points.ply`，Legacy 比较日志为 `filter_only_compare_legacy/100/run.log`。

## 2026-07-10 filter-only 全量 Legacy 再比拼

- 用户确认点云明显改善，要求使用同一算法重新进行 Legacy 全量大比拼，并重点关注效率。
- 开始前执行 `git status --short`，保留现有未提交阶段代码和规划文档，不清理、不覆盖。
- 读取现有 benchmark 脚本，确认其逐帧启动进程，只用于正确性统计；本轮将新增同进程稳定性能口径。
- 固定算法配置为 `build/legacy_step_trace/z_only_validation/filter_only/config.json`。
- 修正 benchmark 对 `ReconstructionInsufficient` 的处理：0 点结果不再被排除，而是作为完整算法结果计入点数输赢。
- 增加 point-cloud CUDA 阶段耗时、raw/smoothed/filtered 点数和更准确的 `<Legacy` / `<95%` 指标。
- 完成 filter-only `0..465` 全量逐进程正确性：466 帧算法均完成，历史 Legacy 非零可比 447 帧，点数总比例 `0.725917`，444 帧低于 Legacy 95%，无 Legacy zero/new nonzero 帧。
- 完成新算法全量单进程效率基线：wall `34.268s`、`13.599 FPS`，稳定 run median `52.654ms`、point-cloud stage median `20.654ms`。
- 完成 Legacy 全量单进程效率基线：wall `11.377s`、`40.960 FPS`，稳定核心 median `9.744ms`。
- 定位第一项确定性效率问题：`qualityInfoEnabled=false` 时 pipeline 仍强制执行约 `13.9ms` 的 CPU quality；已改为按配置跳过，并保留 skipped/notComputed 阶段记录。
## 2026-07-10 filter-only 再比拼：性能定位与第一轮优化

- 构建并运行：
  - `cmake --build build --config Release`
  - `build\Release\reconstructSample.exe --config build\legacy_step_trace\z_only_validation\filter_only\config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --source-img-root D:\Data\output260707_gypsum\Upper --first 100 --last 104`
- 跳过被配置关闭的 CPU 质量评价后，frame `101..104` 稳定 `runElapsed` 为 `37.261/37.557/37.346/37.550ms`，点数与修改前完全一致。
- 在 `PointCloudReconstructorCuda.cu` 增加低成本内部计时，输出 allocation/H2D/kernel/host allocation/D2H/materialize。
- frame `101..104` 点云阶段 median `20.581ms`，其中 CPU materialize median `15.270ms`，CUDA kernel median 仅 `0.482ms`。
- 已开始第一轮单变量优化：`qualityInfoEnabled=false` 时跳过质量专用的 score/candidate D2H、`gridPoints` 构造和 modulation 计算；仍保留完整 compact vertices、normal 和点数。
- 诊断日志：
  - `build\big_battle_filter_only_full_20260710\new_batch_100_104_quality_skip_baseline.log`
  - `build\big_battle_filter_only_full_20260710\new_batch_100_104_point_cloud_breakdown.log`
- `nvprof` 尝试失败，退出码 `-1073741515` 且未生成 profile；已切换到内部计时，不再重复该命令。
- 第一轮点云热路径优化验证：
  - frame `100..104` 点数逐帧完全一致；
  - 稳定 `runElapsed` median：`37.201 -> 26.583ms`；
  - point-cloud stage median：`20.581 -> 9.566ms`；
  - CPU materialize median：`15.270 -> 4.812ms`。
- 已开始第二轮单变量优化：wrapped phase 使用 thread-local CUDA workspace，按实际 `requiredPhaseSteps` 动态扩容，兼容每个频率不同相移步数，不固定全局 3/5 步。
- 第二轮 wrapped workspace 验证：
  - frame `100..104` 点数逐帧完全一致；
  - wrapped phase median：`8.828 -> 4.787ms`；
  - 稳定 `runElapsed` median：`26.583 -> 22.325ms`。
- 已开始第三轮单变量优化：新增 point-cloud output options 和 GPU 点数统计；无 PLY、无 Legacy PLY compare、质量关闭时走 count-only 路径，不回传完整 filtered points/normals，不构造 CPU vertices。
- count-only 首次构建因 `status` 声明顺序错误失败；命令随后误运行旧二进制，输出已判定无效。已修正声明位置，后续构建命令显式检查 `$LASTEXITCODE`，避免失败后继续运行旧产物。
- wrapped modulation reduction 首次编译缺少 `CUDART_INF_F` 定义；已补充 `math_constants.h`，该次未运行旧二进制。
- 第三轮 count-only 验证：
  - frame `100..104` 点数逐帧完全一致；
  - point-cloud stage median：`9.323 -> 1.480ms`；
  - 稳定 `runElapsed` median：`22.325 -> 14.368ms`；
  - benchmark 模式输出 `verticesMaterialized=false`、`qualityGridMaterialized=false`、`normalComputed=false`，但 GPU 匹配、重投影、过滤与点数统计均完成。
- 已开始第四轮单变量优化：phase unwrap 左右相机共用 thread-local CUDA workspace，复用 9 个同尺寸中间 buffer。
- 第四轮 unwrap workspace 验证：
  - frame `100..104` 点数逐帧完全一致；
  - unwrap stage median：`3.237 -> 1.899ms`；
  - 稳定 `runElapsed` median：`14.368 -> 12.979ms`。
- 已开始第五轮单变量优化：ImageValidator 为生产数据的 packed `UInt8` 条纹增加专用统计路径，保留完整黑图/饱和/均值/min/max 校验语义。
- 第五轮 UInt8 validator 验证：
  - `image_validator_test` 和 `image_validator_contract_test` 通过；
  - frame `100..104` 点数逐帧完全一致；
  - validation stage median：`3.964 -> 2.735ms`；
  - 稳定 `runElapsed` median：`12.979 -> 11.678ms`。
- 全量优化版 `0..465` 已完成：466 帧点数与优化前 `benchmark.csv` 逐帧完全一致，状态为 `Ok=442`、`ReconstructionInsufficient=24`；稳定 run median `11.881ms`、p90 `18.344ms`。
- 已开始第六轮单变量优化：pipeline 不物化未消费的 wrapped modulation 数组，CUDA kernel 内 block reduction 直接输出 modulation mean/min/max；默认 API 路径仍保留 modulation vector。
- 第六轮 wrapped modulation reduction 五帧验证：
  - `wrapped_phase_computer_test` 通过；
  - frame `100..104` 点数逐帧完全一致；
  - wrapped stage median：`4.688 -> 2.635ms`；
  - 稳定 `runElapsed` median：`11.678 -> 9.533ms`。
- 最终候选第一次全量长跑：
  - 466 帧点数与优化前逐帧一致；
  - run median/p90：`9.364/9.918ms`，优于 Legacy `9.744/10.671ms`；
  - wall `14.580s`、`31.961 FPS`，仍低于 Legacy `11.377s`、`40.960 FPS`。
- 将 stdout/stderr 重定向到空设备后 wall `14.803s`、`31.480 FPS`，排除诊断输出文件成本。
- 已在 sample 批处理路径增加 `loadElapsedMs`，下一步量化并优化 BMP loader。
- 旧 BMP loader 的 frame `101..104` 稳定 `loadElapsedMs` median 为 `21.079ms`，大于当前算法 `runElapsed` median `9.509ms`。
- 已开始第七轮单变量优化：`loadBmp8()` 改为固定头读取、单次像素块读取和 thread-local pixel block 复用，不再使用 `istreambuf_iterator` 逐字符读取整文件。
- 第七轮 BMP loader 五帧验证：
  - `input_manifest_test` 通过；
  - frame `100..104` 点数逐帧完全一致；
  - `loadElapsedMs` median：`21.079 -> 5.084ms`，约 `4.15x` 加速。
- 执行 loader 优化后的最终 `0..465` 单进程长跑：
  - 命令：`build\Release\reconstructSample.exe --config build\legacy_step_trace\z_only_validation\filter_only\config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --source-img-root D:\Data\output260707_gypsum\Upper --first 0 --last 465`
  - 日志：`build\big_battle_filter_only_full_20260710\new_batch_0_465_optimized_v3.log`
  - sample exit `1`，对应 24 帧 `ReconstructionInsufficient` 的预期批量状态；
  - wall `6.855061s`、`67.978976 FPS`；
  - warm load median/p90 `5.1058/5.8168ms`；
  - warm run median/p90 `9.1875/10.3028ms`。
- 与 Legacy 无输出基线比较：
  - Legacy wall `11.377s`、`40.960 FPS`；
  - Legacy 核心 median/p90 `9.744/10.671ms`；
  - 新算法 wall 吞吐快 `1.6596x`，核心 median/p90 分别快 `1.0606x/1.0357x`。
- 解析最终长跑日志并与 `benchmark.csv` 比较：466 帧点数 mismatch=`0`，状态 `Ok=442`、`ReconstructionInsufficient=24`。
- 首次按 `D:\code\teethscanalgorithm3x4\build\Release` 查找 Legacy App 失败；递归定位到实际路径 `build\bin\Release\TeethScanAlgorithmApp.exe` 后继续。
- 使用相同 Legacy 可执行版本、相同配置，单独开启 final 输出完成 466 帧正确性基准：
  - 输出：`build\big_battle_filter_only_full_20260710\legacy_current_final_output_v1`
  - 运行成功 `466/466`，输出运行 wall `134.616s`，该时间包含 PLY/EXR/PNG 写入，不用于效率比较；
  - 新算法/当前 Legacy 总点数：`36,546,920/40,415,218`，ratio `0.904286`；
  - 新算法没有在任何当前 Legacy 零点帧额外出点；
  - 新算法为零、Legacy 仅有少量点的帧为 `0/1/2/445/446`。
- frame 100 完整物化回归：
  - 输出：`build\big_battle_filter_only_full_20260710\new_materialized_v3\depth_points.ply`
  - `verticesMaterialized=true`、`normalComputed=true`、点数 `84,833`；
  - 对当前 Legacy `89,900` 点的 nearest mean/RMS/P95 为 `0.0698/0.2090/0.2373mm`。
- 生成逐帧对比产物：
  - `build\big_battle_filter_only_full_20260710\current_legacy_comparison.csv`
  - `build\big_battle_filter_only_full_20260710\current_legacy_comparison_summary.txt`
- 最终验证：
  - `cmake --build build --config Release`：成功；
  - `ctest --test-dir build -C Release --output-on-failure`：`10/10` 通过；
  - `git diff --check`：通过；
  - `git status --short`：仅保留开始时已有的阶段修改和未跟踪规划文件/`scripts/__pycache__`，未提交、未推送。

## 2026-07-10 彩色纹理矩阵集成

- 用户要求读取 RGB 辅助彩色帧、吸收 Legacy 颜色矩阵并参数化，然后重新计算真实数据点云。
- 已完成 Legacy 与当前新实现的数据流追踪，确认当前输出是单灰度图伪 RGB。
- 已用 frame 100 量化复现：新 PLY RGB 全相等，Legacy 为真实彩色。
- 已写入 `docs/planning/2026-07-10-color-texture-integration-plan.md`。
- 完成颜色配置字段：`colorTextureEnabled`、`colorTextureProjectorIndices`、`colorCorrectionMatrix`、`colorGamma`。
- 完成 `SourceImg`/singleStripe 三张辅助 BMP 的 packed BGR owned buffer 加载。
- 完成 CUDA 双线性颜色整流、`3x4` 矩阵校正和 gamma，且只在 vertices 完整物化时执行。
- 增加 loader `includeColor` 参数；sample 仅在 `--output` 或 `--compare-legacy` 时传 `true`，无输出 benchmark 不读取 RGB 辅助帧。
- `cmake --build build --config Release`：成功。
- 聚焦 CTest：`config_config_load_test`、`input_manifest_test`、`point_cloud_reconstructor_test`，`3/3` 通过。
- 无输出真实数据 smoke：
  - 命令：`build\Release\reconstructSample.exe --config build\color_texture_upper_20260710\filter_only_color.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --source-img-root D:\Data\output260707_gypsum\Upper --first 100 --last 104`
  - 结果：5 帧状态均为 `Ok`，输出 `colorTextureApplied=false`、`verticesMaterialized=false`，证明 count-only 路径跳过颜色。
  - 日志：`build\color_texture_upper_20260710\count_only_after_include_color.log`。
- frame 100 彩色回归：
  - 输出：`build\color_texture_upper_20260710\frame100_filter_only_v2\depth_points.ply`
  - 点数：`84,833`
  - `colorTextureApplied=true`
  - mean RGB=`[253.6555,205.7671,182.5587]`
  - equal RGB ratio=`0.0`
  - unique colors=`5,148`
- 当前 Legacy frame 100 mean RGB=`[253.2076,203.7344,180.4113]`，equal RGB ratio=`0.0`，unique colors=`6,164`。
- Upper `0..465` 全量彩色重算：
  - 输出：`build\color_texture_upper_20260710\full_v1\<frame>\depth_points.ply`
  - 日志：`build\color_texture_upper_20260710\full_v1.log`
  - wall：`104.57s`
  - sample exit=`1`，对应预期的 24 帧 `ReconstructionInsufficient`
  - 解析帧数=`466`，`Ok=442`、`ReconstructionInsufficient=24`
  - 彩色 PLY=`442`
  - 总点数=`36,546,920`
  - 与 filter-only 基线逐帧点数 mismatch=`0`
  - 汇总：`build\color_texture_upper_20260710\full_v1_summary.json`
- 独立规格审查发现并修复 singleStripe `includeColor=false` 测试缺口，复核结果 `PASS`。
- 独立代码质量审查发现并修复两个 P2：
  - 移除 `includeColor` 默认值，所有 loader 调用必须显式声明是否需要颜色。
  - loader 本地校验 `colorTextureProjectorIndices` 长度和正值，直接构造非法配置返回 `ConfigInvalidValue`。
- 最终真实 frame 100 双路径回归：
  - 无输出：`colorTextureApplied=false`、`verticesMaterialized=false`、点数 `84,833`。
  - 输出：`colorTextureApplied=true`、`verticesMaterialized=true`、点数 `84,833`。
  - 最终单帧 PLY：`build\color_texture_upper_20260710\frame100_filter_only_v3\depth_points.ply`。
- 修复审查问题后的最终验证：
  - Release 全量构建成功。
  - CTest `10/10` 通过。
  - `git diff --check` 通过。
- 本轮未修改 Legacy，未提交，未推送。
