# 第二阶段输入、标定与预处理契约实施计划

> 文档日期：2026-07-09
> 输入基础：第一阶段提交 `a52faae feat(foundation): 建立第一阶段重构骨架`
> 阶段定位：实施计划，不直接进入代码实现。

## 1. 第二阶段目标

第二阶段目标是把第一阶段的 dry-run 骨架推进到“真实输入可验证、标定结果可解析、预处理契约可诊断”的状态，但仍不迁移相位、匹配、重建和 AI 核心算法。

本阶段完成后应具备：

- `StripeFrameGroup` 能承载来自调用方的真实条纹数组元数据，而不是只靠 sample 内部构造假输入。
- 图像校验从“空/黑/尺寸/stride”扩展到输入计划校验、左右一致性、频率/相移覆盖、像素统计和异常比例统计。
- `CalibrationModel` 从“文件存在性骨架”推进到宽松解析 `calibResult.json` 的常见矩阵字段，并生成结构化 summary。
- `SingleFramePipeline` 明确拆出 `input_validation`、`calibration_contract`、`image_preprocess_dry_run` 三个 dry-run 阶段。
- sample 支持从一个轻量 JSON manifest 构造输入契约，验证真实调用边界；不读取 Legacy 扫描目录，不调用 Legacy DLL。
- 诊断输出只写结构化文本/JSON summary，不产生假 depth/normal/quality，不输出 PLY/EXR/PNG。

## 2. 明确不做

- 不实现折叠相位、展开相位、同名点匹配、三维重建、法向计算。
- 不迁移 `GPUPhaseUnwrapper`、`computePointCloudFromPhase`、`DualStreamPPLiteSeg`。
- 不接入 CUDA/TensorRT。
- 不做标定计算、圆点检测、重投影优化或真实 rectification map 生成。
- 不读取 Legacy 扫描目录格式作为生产输入；如需 sample 输入，只读第二阶段定义的最小 manifest。
- 不输出 PLY/EXR/PNG，不生成可被误解为真实算法结果的点云、法向或质量图。
- 不修改 `D:\code\dentalscanserviceinterface`；只继续维护兼容计划。
- 不提交代码，除非用户明确要求。

## 3. 第二阶段核心判断

第一阶段已经证明工程可编译、配置可读、日志可生成、缺失标定可返回结构化状态。第二阶段不应急着迁移 Legacy 算法，因为算法迁移前缺少两个前置条件：

1. 输入数组的业务语义尚未被充分验证：频率、相移、左右相机、projectorIndex、尺寸、类型、stride、黑图/过曝/非有限值等需要先成为稳定契约。
2. MPS `calibResult.json` 的字段格式尚未在本项目中固化：即使第一阶段允许缺失标定，进入相位/匹配/重建前也必须知道标定对象能否被解析、摘要和校验。

因此第二阶段应优先做“算法前置闸门”。只要这个闸门足够可靠，第三阶段再接入相位 baseline 时才不会把输入/标定问题误判为算法问题。

## 4. 目标文件结构

第二阶段建议创建或修改以下文件：

```text
include/reconstruct_one_frame/
  reconstructInterface.h
src/
  config/
    ReconsConfig.h
    ReconsConfig.cpp
  calibration_model/
    CalibrationModel.h
    CalibrationModel.cpp
    CalibrationJsonReader.h
    CalibrationJsonReader.cpp
  image/
    ImageTypes.h
    ImageValidator.h
    ImageValidator.cpp
    InputManifest.h
    InputManifest.cpp
    ImagePreprocessor.h
    ImagePreprocessor.cpp
  diagnostics/
    DiagnosticSummary.h
    DiagnosticSummary.cpp
  pipeline/
    SingleFramePipeline.h
    SingleFramePipeline.cpp
    ReconstructEngine.cpp
  sample/
    reconstructSample.cpp
tests/
  calibration_model_test.cpp
  input_manifest_test.cpp
  image_validator_contract_test.cpp
  image_preprocess_dry_run_test.cpp
  pipeline_phase2_smoke_test.cpp
docs/planning/
  progress.md
  findings.md
  task_plan.md
```

说明：

- `InputManifest` 只服务第二阶段 sample 和测试，目的是把“调用方传入纯数组”的契约文件化验证；它不是生产扫描目录读取器。
- `ImagePreprocessor` 第二阶段只做 CPU 侧 dry-run 统计和标准化计划，不做 remap、不分配 GPU buffer。
- `DiagnosticSummary` 用于统一 sample/stdout/log 输出，避免每个模块拼自己的文本。

## 5. 执行顺序

### Task 1：扩展状态码与统计字段

目的：让第二阶段输入/标定失败有明确状态，不挤在 `InputInvalidValue` 或 `CalibrationInvalid` 中。

文件：

- `include/reconstruct_one_frame/reconstructInterface.h`
- `src/diagnostics/Status.cpp`

建议新增状态码：

```cpp
InputMissingLeftStripes,
InputMissingRightStripes,
InputFrequencyPlanMismatch,
InputPhaseStepMissing,
InputCameraSideMismatch,
InputNonFinitePixel,
InputSaturatedImage,
InputManifestMissing,
InputManifestParseFailed,
CalibrationFieldMissing,
CalibrationMatrixShapeInvalid,
CalibrationImageSizeMismatch
```

建议扩展 `StageStats`：

```cpp
std::size_t saturatedPixels = 0;
std::size_t nonFinitePixels = 0;
double blackPixelRatio = 0.0;
double saturatedPixelRatio = 0.0;
double minPixelValue = 0.0;
double maxPixelValue = 0.0;
double meanPixelValue = 0.0;
```

验收：

```powershell
cmake --build build --config Release
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run-no-calib
```

预期：新增状态码可编译，已有 sample 输出不退化。

### Task 2：把配置解析扩展为输入计划契约

目的：第二阶段不固定全局 `freqCount/stepCount` 到输入结构，但配置层必须能解释“需要哪些频率和相移”。

文件：

- `src/config/ReconsConfig.h`
- `src/config/ReconsConfig.cpp`
- `tests/config_config_load_test.cpp`

操作：

1. 保留第一阶段字段。
2. 增加派生结构：

```cpp
struct StripeRequirement {
    int frequencyIndex = -1;
    int frequencyValue = 0;
    int requiredPhaseSteps = 0;
};

std::vector<StripeRequirement> stripeRequirements;
```

3. 从 `FREQ`、`STEP`、`freqSeries` 派生默认输入计划。
4. 如果 `freqSeries.size() != FREQ`，返回 `ConfigInvalidValue`。
5. 不新增更多 Legacy key；当前评审结论仍成立。

验收：

- `config_config_load_test` 检查 `stripeRequirements.size() == 3`。
- 每个 requirement 的 `requiredPhaseSteps == STEP`。

### Task 3：实现最小 InputManifest

目的：sample 能验证真实输入边界，但不绑定 Legacy 目录，也不读取图像文件。

文件：

- `src/image/InputManifest.h`
- `src/image/InputManifest.cpp`
- `tests/input_manifest_test.cpp`
- `src/sample/reconstructSample.cpp`

Manifest 只描述元数据和测试用像素模式：

```json
{
  "frameId": 1,
  "width": 424,
  "height": 400,
  "elementType": "uint8",
  "channels": 1,
  "leftStripes": [
    {"frequencyIndex": 0, "phaseStepIndex": 0, "projectorIndex": 0, "fill": 32}
  ],
  "rightStripes": [
    {"frequencyIndex": 0, "phaseStepIndex": 0, "projectorIndex": 0, "fill": 48}
  ]
}
```

CLI 增加：

```text
--input-manifest <path>
```

约束：

- 第二阶段 manifest 不接受真实图片路径，避免误导为扫描目录读取能力。
- `fill` 用于构造测试内存图，允许覆盖黑图、过曝图、正常图。
- manifest 缺失返回 `InputManifestMissing`。
- manifest 非 JSON 返回 `InputManifestParseFailed`。

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib
```

预期：返回 `StatusCode::Ok`，日志包含 manifest image count 和校验 summary。

### Task 4：强化图像校验

目的：把图像输入闸门做实，避免第三阶段算法迁移时输入错误被误判。

文件：

- `src/image/ImageValidator.h`
- `src/image/ImageValidator.cpp`
- `tests/image_validator_contract_test.cpp`

校验规则：

- 左右条纹数组分别不能为空，并返回左右缺失的专用状态码。
- 每张条纹图必须匹配自身 `camera` 与所在数组。
- 所有条纹图尺寸、channels、elementType、stride 必须一致。
- 对 `UInt8` / `UInt16` / `Float32` 分别统计：
  - black ratio
  - saturated ratio
  - min/max/mean
  - non-finite ratio，仅 Float32 有意义
- 全黑返回 `InputBlackImage`。
- 饱和比例超过阈值返回 `InputSaturatedImage`。
- Float32 出现 NaN/Inf 超过阈值返回 `InputNonFinitePixel`。
- 根据 `ReconsConfig::stripeRequirements` 检查左右相机是否覆盖每个 frequencyIndex/phaseStepIndex。

验收：

- 覆盖正常输入、缺左、缺右、相机侧错、缺相移、尺寸不一致、stride 错、黑图、过曝、Float32 NaN。
- `ctest --test-dir build -C Release --output-on-failure` 全部通过。

### Task 5：深化 CalibrationModel 宽松解析

目的：让本项目能读取 MPS `calibResult.json` 的常见字段并给出结构化诊断，但不做标定计算。

文件：

- `src/calibration_model/CalibrationModel.h`
- `src/calibration_model/CalibrationModel.cpp`
- `src/calibration_model/CalibrationJsonReader.h`
- `src/calibration_model/CalibrationJsonReader.cpp`
- `tests/calibration_model_test.cpp`

解析策略：

1. 宽松支持多个常见 key 名称，优先记录实际命中的 key：
   - image size：`image_size` / `imageSize` / `size`
   - left intrinsics：`leftIntrinsics` / `cameraMatrixL` / `M1`
   - right intrinsics：`rightIntrinsics` / `cameraMatrixR` / `M2`
   - distortion：`leftDistortion` / `rightDistortion` / `D1` / `D2`
   - stereo extrinsics：`R` / `T`
   - optional Q：`Q`
2. 只检查矩阵元素数量：
   - intrinsics：9
   - distortion：4、5、8、12、14 均接受并记录长度
   - R：9
   - T：3
   - Q：16
3. 如果文件存在但核心矩阵缺失，返回 `CalibrationFieldMissing`。
4. 如果矩阵长度不合法，返回 `CalibrationMatrixShapeInvalid`。
5. 如果标定 image size 与配置 image size 不一致，返回 `CalibrationImageSizeMismatch`，但 `--dry-run-no-calib` 不进入该检查。

验收：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

测试覆盖：

- missing file -> `CalibrationMissing`
- invalid json -> `CalibrationParseFailed`
- missing core field -> `CalibrationFieldMissing`
- malformed matrix -> `CalibrationMatrixShapeInvalid`
- valid minimal calibration -> `Ok`
- image size mismatch -> `CalibrationImageSizeMismatch`

### Task 6：建立 ImagePreprocessor dry-run

目的：为第三阶段相位计算准备标准化输入描述，但不做真实 remap/CUDA。

文件：

- `src/image/ImagePreprocessor.h`
- `src/image/ImagePreprocessor.cpp`
- `tests/image_preprocess_dry_run_test.cpp`

结构建议：

```cpp
struct PreprocessPlan {
    int width = 0;
    int height = 0;
    int channels = 0;
    ImageElementType elementType = ImageElementType::Unknown;
    bool rectificationRequired = false;
    bool gpuUploadRequired = false;
    bool normalizedFloatRequired = true;
    std::size_t stripeCount = 0;
};

struct PreprocessResult {
    Status status;
    StageStats stats;
    PreprocessPlan plan;
};
```

行为：

- 输入已经通过 `ImageValidator` 后才进入。
- 只生成 plan 和统计，不拷贝整张图、不改写输入内存。
- 明确 `normalizedFloatRequired=true`，为后续相位模块准备，但第二阶段不执行转换。
- 如果未来需要 rectification，先只根据 calibration 是否存在记录 `rectificationRequired`。

验收：

- 正常 frame 生成 plan。
- 输出 stageName 为 `image_preprocess_dry_run`。
- `PreprocessResult` 不包含 depth/normal/quality。

### Task 7：调整 SingleFramePipeline 阶段输出

目的：pipeline 从第一阶段单一 validation + dry_run_compute，变成可诊断的前置链路。

文件：

- `src/pipeline/SingleFramePipeline.h`
- `src/pipeline/SingleFramePipeline.cpp`
- `src/pipeline/ReconstructEngine.cpp`
- `tests/pipeline_phase2_smoke_test.cpp`

流程：

```text
config already loaded by engine
calibration optional load/check
input_contract_validation
calibration_contract_validation
image_preprocess_dry_run
algorithm_not_computed marker
FrameResult
```

要求：

- `--dry-run-no-calib`：跳过 calibration contract，不阻塞。
- `--dry-run` 且 `--calib missing`：返回 `CalibrationMissing`。
- `--dry-run` 且 calibration image size mismatch：返回 `CalibrationImageSizeMismatch`。
- `FrameResult.stats` 至少包含：
  - `input_contract_validation`
  - `calibration_contract_validation`，无标定 dry-run-no-calib 时可标记 skipped
  - `image_preprocess_dry_run`
  - `algorithm_not_computed`

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib
ctest --test-dir build -C Release --output-on-failure
```

预期：sample 输出阶段 summary，算法输出仍全部 `notComputed`。

### Task 8：增加 DiagnosticSummary 输出

目的：避免 sample 和模块各自拼接诊断文本，统一 stdout/log 输出。

文件：

- `src/diagnostics/DiagnosticSummary.h`
- `src/diagnostics/DiagnosticSummary.cpp`
- `src/sample/reconstructSample.cpp`

输出内容：

```text
status=Ok
frameId=1
stage=input_contract_validation,status=Ok,inputImages=30,validImages=30,blackRatio=0.000,saturatedRatio=0.000
stage=calibration_contract_validation,status=Ok
stage=image_preprocess_dry_run,status=Ok,normalizedFloatRequired=true
stage=algorithm_not_computed,status=NotComputed
depthComputed=false
normalComputed=false
qualityComputed=false
```

验收：

- sample stdout 和 log 至少包含 status、frameId、每个 stage 的状态。
- 不输出假 depth/normal/quality 值。

### Task 9：测试数据和最小验证

目的：让第二阶段可以在没有真实扫描数据、没有 MPS 标定文件时仍可验证输入和标定契约。

文件：

- `tests/data/phase2_valid_manifest.json`
- `tests/data/phase2_black_manifest.json`
- `tests/data/phase2_missing_step_manifest.json`
- `tests/data/phase2_valid_calibResult.json`
- `tests/data/phase2_bad_calib_matrix.json`
- `tests/data/phase2_mismatch_calibResult.json`

验证命令：

```powershell
git status --short
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build\Release\reconstructSample.exe --help
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --dry-run-no-calib
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_valid_manifest.json --calib tests\data\phase2_valid_calibResult.json --dry-run
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --input-manifest tests\data\phase2_missing_step_manifest.json --dry-run-no-calib
ctest --test-dir build -C Release --output-on-failure
```

预期：

- valid manifest + no calib dry-run 返回 `Ok`。
- valid manifest + valid calib dry-run 返回 `Ok`。
- missing step manifest 返回 `InputPhaseStepMissing` 或 `InputFrequencyPlanMismatch`。
- CTest 全部通过。

## 6. 第二阶段完成标准

第二阶段完成时，应满足：

- 当前第一阶段测试仍全部通过。
- 新增 calibration/input/preprocess/pipeline 测试全部通过。
- sample 支持 `--input-manifest`，且 manifest 只构造内存图，不读取真实扫描目录。
- 缺失 manifest、非法 manifest、缺左、缺右、缺相移、黑图、过曝、NaN/Inf、stride 错、尺寸错均有明确状态码。
- 缺失标定、非法标定、标定字段缺失、矩阵 shape 错、标定尺寸不匹配均有明确状态码。
- `FrameResult` 不填充假 depth/normal/quality。
- `docs/planning/progress.md` 记录实际执行命令、结果、错误和处理。
- `docs/planning/findings.md` 记录 MPS calibResult 字段命中情况；如没有真实样例，则明确说明使用第二阶段测试样例。
- 不修改 DSSI。

## 7. 风险与处理

| 风险 | 处理 |
|---|---|
| MPS `calibResult.json` 真实字段名不确定 | 第二阶段使用宽松 key 映射和测试样例；拿到真实文件后只补 reader 映射，不改 pipeline 契约 |
| manifest 被误解为生产图像读取能力 | 文件注释和 sample help 明确 manifest 只用于契约验证，不读取真实扫描目录 |
| 输入计划与未来变步数频率冲突 | `StripeRequirement` 使用每频率 requiredPhaseSteps，避免把 `STEP` 烧死进 `StripeFrameGroup` |
| 预处理 dry-run 被误认为已完成 remap/normalize | `PreprocessPlan` 只记录 required，不产出图像 buffer；StageStats 中标记 dry-run |
| 状态码膨胀 | 只为第二阶段可验证错误增加状态码，算法阶段状态码留到后续阶段 |
| 日志输出变吵 | sample 输出 summary；库内部仍保持默认 info，不进入逐像素日志 |

## 8. 建议评审点

请重点评审：

1. 第二阶段是否应优先做输入/标定/预处理闸门，而不是直接迁移相位 baseline。
2. `--input-manifest` 是否接受“只构造内存图、不读真实图片路径”的限制。
3. `CalibrationModel` 宽松 key 映射是否足够，是否需要先拿一份 MPS 真实 `calibResult.json` 再实施。
4. `ImageValidator` 是否应在第二阶段就检查完整 frequency/phaseStep 覆盖。
5. 第二阶段是否继续不修改 DSSI，仅维护接口兼容计划。

## 9. 第三阶段前置条件

第二阶段完成后，第三阶段才能安全进入相位 baseline。第三阶段启动前至少需要：

- 一份真实或脱敏的 MPS `calibResult.json` 样例，用于确认字段映射。
- 一组真实条纹输入如何映射到 `StripeImage` 元数据的说明，至少包含左右相机、frequencyIndex、phaseStepIndex、projectorIndex。
- 选定第三阶段相位 baseline 范围：CPU 参考、CUDA stub，还是 Legacy 两阶段算法最小迁移。
- 明确第三阶段是否允许引入 OpenCV；第二阶段建议仍不强依赖 OpenCV。
