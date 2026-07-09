# 第一阶段基础骨架执行计划

> 文档日期：2026-07-09
> 评审状态：待评审
> 输入设计：`docs/design/2026-07-09-legacy-single-frame-reconstruction-overview.md`
> 阶段目标：建立可编译、可验证、可供后续算法模块迁移的基础工程骨架；不迁移相位、匹配、重建、AI 等核心算法实现。

## 1. 第一阶段目标

第一阶段只解决“工程地基”和“接口契约”，不追求真实重建输出。

完成后应具备：

- 清晰目录骨架：`config`、`logging`、`diagnostics`、`io`、`calibration_model`、`image`、`pipeline`、`sample`、`include/reconstruct_one_frame`。
- 稳定公共头：`include/reconstruct_one_frame/reconstructInterface.h`。
- 可读取 Legacy 算法配置：`config/reconsAlgPara.json`。
- 可读取 MPS 标定结果 JSON 的接口骨架：面向 `calibResult.json`，不支持新标定计算。
- 可初始化日志：仿照 `D:\code\slam` 直接引入第三方 Log/spdlog，默认 `info`。
- 显式状态码、阶段统计、诊断结构。
- 输入图像基础检查：空图、黑图、尺寸、类型、左右数组一致性。
- `src/sample/reconstructSample.cpp` 能展示外部调用流程。
- 一个最小 pipeline 可跑通“配置加载 -> 日志初始化 -> 输入检查 -> 返回结构化状态”，但不计算相位/点云。

## 2. 明确不做

- 不实现折叠相位、展开相位、同名点匹配、三维重建、法向计算。
- 不迁移 `GPUPhaseUnwrapper`、`computePointCloudFromPhase`、`DualStreamPPLiteSeg`。
- 不迁移相位/匹配/重建/AI 核心算法；这些模块只保留接口占位和明确 `notComputed` 状态。
- 不接入 TensorRT 模型。
- 不做标定计算，不检测圆点，不优化内外参。
- 不改 `D:\code\dentalscanserviceinterface`。
- 不删除 Legacy 目录，不大范围重排 Legacy 文件。
- 不提交代码，除非评审后明确要求。

## 3. 目标文件结构

第一阶段建议创建或修改以下文件：

```text
CMakeLists.txt
config/
  reconsAlgPara.json
include/reconstruct_one_frame/
  reconstructInterface.h
src/
  config/
    ReconsConfig.h
    ReconsConfig.cpp
  logging/
    LogSession.h
    LogSession.cpp
  diagnostics/
    Status.h
    StageStats.h
    StageStats.cpp
  io/
    JsonFile.h
    JsonFile.cpp
  calibration_model/
    CalibrationModel.h
    CalibrationModel.cpp
  image/
    ImageTypes.h
    ImageValidator.h
    ImageValidator.cpp
  pipeline/
    SingleFramePipeline.h
    SingleFramePipeline.cpp
  sample/
    reconstructSample.cpp
tests/
  config_config_load_test.cpp
  image_validator_test.cpp
  pipeline_smoke_test.cpp
docs/planning/
  progress.md
  findings.md
```

如果当前 CMake 第一次建立成本偏高，可先把 `tests/` 延后到同一阶段后半段，但 `sample` 和 `pipeline_smoke` 至少需要一个可运行验证入口。

## 4. 执行顺序

### Task 1：建立最小 CMake 工程骨架

目的：让新项目从“只有文档和 Legacy”变成可编译工程。

操作：

1. 新建根 `CMakeLists.txt`。
2. 设置 C++17、MSVC 编译选项和第三方路径变量。
3. 定义库目标 `reconstructOneFrame`。
4. 定义示例程序 `reconstructSample`。
5. 暂不链接 CUDA/TensorRT；第一阶段只建立 CPU 侧基础设施。

验收：

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

预期：工程能配置和编译；如果第三方 Log/spdlog 路径缺失，CMake 应给出明确错误。

### Task 2：建立公共接口和状态码

目的：先固定外部契约，避免后续算法实现继续扩大隐式依赖。

文件：

- `include/reconstruct_one_frame/reconstructInterface.h`
- `src/diagnostics/Status.h`
- `src/diagnostics/StageStats.h`
- `src/diagnostics/StageStats.cpp`

接口草案：

```cpp
namespace reconstruct_one_frame {

enum class StatusCode {
    Ok = 0,
    InputMissing,
    InputEmptyImage,
    InputBlackImage,
    InputTypeMismatch,
    InputSizeMismatch,
    ConfigMissing,
    ConfigParseFailed,
    ConfigInvalidValue,
    CalibrationMissing,
    CalibrationParseFailed,
    CalibrationInvalidGeometry,
    CudaUnavailable,
    CudaAllocationFailed,
    CudaKernelFailed,
    ImageQualityInsufficient,
    WrappedPhaseFailed,
    UnwrapFailed,
    CorrespondenceInsufficient,
    ReconstructionPointCountTooLow,
    AiModelMissing,
    AiInferenceFailed,
    OutputWriteFailed,
    InternalInvariantFailed
};

struct Status {
    StatusCode code = StatusCode::Ok;
    const char* module = "";
    const char* message = "";
};

struct StageStats {
    double cpuMs = 0.0;
    double cudaKernelMs = 0.0;
    int inputPixelCount = 0;
    int validPixelCount = 0;
    int rejectedPixelCount = 0;
};

}
```

验收：

- 状态码覆盖 `agent.md` 要求的输入、配置、标定、CUDA、质量、相位、匹配、重建、输出失败类型。
- 不再用单一 `bool` 或 `0/-1` 表示内部失败。

### Task 3：搬用并强类型读取 `reconsAlgPara.json`

目的：第一阶段直接沿用 Legacy 配置入口，不引入 default/schema 作为前置条件。

文件：

- `config/reconsAlgPara.json`
- `src/io/JsonFile.h`
- `src/io/JsonFile.cpp`
- `src/config/ReconsConfig.h`
- `src/config/ReconsConfig.cpp`

操作：

1. 从 `D:\code\teethscanalgorithm3x4\config\reconsAlgPara.json` 复制到 `config/reconsAlgPara.json`。
2. 建立 `ReconsConfig` 强类型结构，只覆盖第一阶段所需字段：
   - `image_size`
   - `FREQ`
   - `STEP`
   - `freqSeries`
   - `phaseStepDirection`
   - `minZ/maxZ`
   - `aiEnabled`
   - `debugMapOutputEnabled`
   - `qualityInfoEnabled`
   - `saveOutputs`
   - `calibResultPath`，新增兼容字段，指向 MPS 输出 JSON
3. 缺失字段使用 Legacy 默认值。
4. 类型错误、数组长度错误、非法 Z 范围返回 `ConfigInvalidValue`。

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run
```

预期：

- 能打印 effective config 摘要。
- 配置不存在返回 `ConfigMissing`。
- 非法 JSON 返回 `ConfigParseFailed`。

### Task 4：接入日志基础设施

目的：仿照 `D:\code\slam`，使用第三方 Log/spdlog，默认 `info`。

文件：

- `src/logging/LogSession.h`
- `src/logging/LogSession.cpp`

设计：

- `LogSession` 只在 sample、DLL 初始化或下游入口层创建。
- 日志路径：`logs/log_<tag>_<YYYY-MM-DD_HH-MM-SS>.log`。
- 默认等级：`info`。
- 默认保留最近 10 个日志。
- 算法模块后续只调用轻封装，不直接持有第三方 logger。

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run
Get-ChildItem logs\log_*.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
```

预期：生成日志文件，包含 config 加载、pipeline dry-run、状态码 summary。

### Task 5：建立标定结果读取骨架

目的：明确本项目只读取 MPS 标定结果 JSON，不做标定。

文件：

- `src/calibration_model/CalibrationModel.h`
- `src/calibration_model/CalibrationModel.cpp`

操作：

1. 定义 `CalibrationModel`：
   - image size
   - left/right intrinsics
   - left/right distortion
   - R/T
   - Q 或可推导重投影参数
   - rectification canvas policy
2. 定义 `LoadCalibrationResultJson(path)`。
3. 对 `calibResult.json` 字段先做宽松读取：第一阶段允许没有真实文件时返回 `CalibrationMissing`，不阻塞 `--dry-run-no-calib`。
4. 保留 `calibParams.yml` 只作为 Legacy 兼容说明，不实现 YAML 首选读取。

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --calib D:\some\missing\calibResult.json --dry-run
```

预期：返回明确 `CalibrationMissing`，日志说明本项目不执行标定。

### Task 6：定义图像输入契约和基础校验

目的：先把“纯数组输入 + 每张图元数据”的契约落地，不固定全局 `freqCount/stepCount`。

文件：

- `src/image/ImageTypes.h`
- `src/image/ImageValidator.h`
- `src/image/ImageValidator.cpp`

结构草案：

```cpp
enum class CameraSide { Left, Right };

struct ImageView {
    const void* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    int strideBytes = 0;
    int elementType = 0;
};

struct StripeImage {
    CameraSide camera = CameraSide::Left;
    int frequencyIndex = -1;
    int phaseStepIndex = -1;
    int projectorIndex = -1;
    ImageView image;
};

struct StripeFrameGroup {
    std::vector<StripeImage> leftStripes;
    std::vector<StripeImage> rightStripes;
    ImageView color;
};
```

校验规则：

- 左右数组不能为空。
- 每张图必须有 data、width、height、stride。
- 左右图尺寸必须一致，除非后续明确支持非对称输入。
- 图像全黑比例超过阈值返回 `InputBlackImage`。
- 每频率相移步数不在输入结构里强行固定，只在配置解释层检查“所需条纹是否齐全”。

验收：

- `image_validator_test` 覆盖空数组、空图、黑图、尺寸不一致、正常输入。
- sample 可构造一组假输入并返回 `Ok` 或明确输入错误。

### Task 7：建立 `SingleFramePipeline` dry-run

目的：让第一阶段有可运行主链路，但不计算真实点云。

文件：

- `src/pipeline/SingleFramePipeline.h`
- `src/pipeline/SingleFramePipeline.cpp`

流程：

```text
load config
init log
load calibration model if provided
validate stripe frame group
fill StageStats
return FrameResult with StatusCode
```

输出：

- `FrameResult.status`
- `FrameResult.stats`
- 空的 `depth/color/normal/qualityInfo` 占位对象或明确 `notComputed` 标志。

验收：

```powershell
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run
```

预期：输出 `StatusCode::Ok`，日志包含各阶段 summary。

### Task 8：建立 `reconstructSample`

目的：形成外部调用用例，而不是核心 app。

文件：

- `src/sample/reconstructSample.cpp`

职责：

- 解析 `--config`、`--calib`、`--dry-run`、`--dry-run-no-calib`。
- 初始化 `LogSession`。
- 调用 config/calibration/pipeline。
- 输出状态码和关键统计。

不做：

- 不读真实扫描数据目录。
- 不调用 Legacy DLL。
- 不输出 PLY/EXR/PNG。

验收：

```powershell
build\Release\reconstructSample.exe --help
build\Release\reconstructSample.exe --config config\reconsAlgPara.json --dry-run-no-calib
```

预期：命令行行为稳定，不产生默认目录副作用。

### Task 9：建立最小验证测试

目的：保证第一阶段基础设施不是纯文档骨架。

文件：

- `tests/config_config_load_test.cpp`
- `tests/image_validator_test.cpp`
- `tests/pipeline_smoke_test.cpp`

测试最小集：

- `config_load_valid_recons_alg_para`
- `config_missing_returns_config_missing`
- `config_invalid_json_returns_parse_failed`
- `image_validator_rejects_empty_input`
- `image_validator_rejects_black_image`
- `pipeline_dry_run_returns_structured_status`

验收：

```powershell
ctest --test-dir build --output-on-failure
```

预期：全部通过。

## 5. 第一阶段完成标准

第一阶段完成时，应满足以下条件：

- `cmake -B build -S .` 成功。
- `cmake --build build --config Release` 成功。
- `reconstructSample --help` 成功。
- `reconstructSample --config config\reconsAlgPara.json --dry-run-no-calib` 成功。
- 缺失配置、非法配置、缺失标定、空输入、黑图都有明确状态码。
- `logs/log_*.log` 能生成，并默认 `info`。
- `ctest --test-dir build --output-on-failure` 通过，或若测试框架暂未接入，需要在进度文档中记录原因和替代验证命令。
- `docs/planning/progress.md` 记录实际执行命令、结果和未决问题。
- 不产生与真实算法输出混淆的假点云、假法向、假质量分。

## 6. 风险与处理

| 风险 | 处理 |
|---|---|
| 第三方 Log/spdlog 路径和当前仓库 CMake 未配置 | 先参考 `D:\code\slam\CMakeLists.txt` 的第三方路径变量；缺失时 CMake 明确报错 |
| `calibResult.json` 字段格式需要进一步确认 | 第一阶段只做读取骨架和错误状态；真实字段映射作为第二阶段前置任务 |
| 直接搬用 `reconsAlgPara.json` 字段太多 | 第一阶段只解析必要字段，其余保留在 raw JSON 中并写入 diagnostics |
| sample 被误用为生产入口 | 文档和文件注释明确 sample 只做调用示例，核心入口是 `reconstructInterface.h` |
| dry-run 产生“看似成功”的假算法结果 | `FrameResult` 明确标记 `notComputed`，不填充假 depth/normal/quality |

## 7. 建议评审点

请重点评审以下决策：

1. 第一阶段是否可以先不链接 CUDA/TensorRT，只建立 CPU 侧契约和 dry-run。评审结论：可以。
2. `reconsAlgPara.json` 第一阶段解析字段是否足够，是否需要加入更多 Legacy key。评审结论：当前字段足够，不需要增加更多 Legacy key。
3. `CalibrationModel` 是否接受第一阶段只做 `calibResult.json` 读取骨架，不做真实几何完整映射。评审结论：接受。
4. `reconstructInterface.h` 是否应第一阶段就包含 C ABI，还是先 C++ API，后续再加 C wrapper。评审结论：需要先解释差异，再定。
5. DSSI 适配是否第一阶段只写接口兼容计划，不实际修改 DSSI。评审结论：是，第一阶段不实际修改 DSSI。

## 8. C ABI 与 C++ API 说明

### 8.1 C++ API 是什么

C++ API 指在 `reconstructInterface.h` 中直接暴露 C++ 类型、类、命名空间和方法，例如：

```cpp
namespace reconstruct_one_frame {

class ReconstructEngine {
public:
    Status init(const InitOptions& options);
    FrameResult run(const StripeFrameGroup& frame);
    void shutdown();
};

}
```

优点：

- 写法自然，适合本项目内部模块化设计。
- 可以直接使用 `std::vector`、`std::string`、RAII、强类型 enum、类封装。
- 第一阶段实现速度快，便于表达 `StripeImage`、`FrameResult`、`StageStats` 这类结构化契约。

缺点：

- DLL 边界更脆弱：不同 MSVC 版本、运行库设置、STL ABI、Debug/Release 混用时更容易出兼容问题。
- 外部项目如果不是同一编译器/同一运行库，不适合直接跨 DLL 传 `std::vector`、`std::string`、C++ class。
- 热加载 `GetProcAddress` 不适合直接查找 C++ 类方法，符号名会被 C++ name mangling 改写。

适用范围：

- 本项目内部。
- 同一解决方案、同一编译器、同一运行库下的静态链接或 import lib 链接。
- 第一阶段 dry-run 和模块骨架。

### 8.2 C ABI 是什么

C ABI 指在 DLL 边界只暴露 `extern "C"` 函数、普通指针、POD struct、整数错误码等稳定符号，例如：

```cpp
extern "C" {

ROF_API int rof_create(void** handle);
ROF_API int rof_init(void* handle, const RofInitOptions* options);
ROF_API int rof_run(void* handle, const RofFrameInput* input, RofFrameOutput* output);
ROF_API int rof_destroy(void* handle);

}
```

优点：

- DLL 符号名稳定，适合 `LoadLibrary` / `GetProcAddress`。
- 对 DSSI 这类下游更稳，不依赖 C++ name mangling。
- 更容易被 C、C++、C#、Python、Qt plugin loader 或其他语言绑定调用。
- 不跨 DLL 传 STL 对象，能减少内存分配、释放方不一致的问题。

缺点：

- 写起来更繁琐，需要把 `std::vector<StripeImage>` 这类对象展开成 `pointer + count`。
- 错误信息、字符串、数组输出都要明确所有权：谁分配、谁释放、长度是多少。
- 需要额外 wrapper 层把 C ABI 转成内部 C++ API。

适用范围：

- DLL 对外边界。
- 需要热加载、跨语言、跨运行库或长期二进制兼容的下游集成。
- DSSI 过渡兼容层和未来替代 `threeScan_*` 的稳定入口。

### 8.3 建议决策

第一阶段建议同时设计两层，但只重点实现 C++ API：

1. 内部主接口先用 C++ API：`ReconstructEngine`、`StripeFrameGroup`、`FrameResult`、`StageStats`，用于快速建立模块边界和 dry-run。
2. `reconstructInterface.h` 中预留 C ABI 区域，但第一阶段只实现最小生命周期函数或先不导出完整 `run`。
3. 不在 C ABI 中暴露 `std::vector`、`std::string`、`cv::Mat`、C++ class；这些只属于 C++ API 层。
4. 后续 DSSI 真正改造时，再把 C ABI 扩展为 `pointer + count` 的稳定输入输出结构，或者由 DSSI 直接链接 C++ import lib，但那要求两边编译环境严格一致。

推荐原因：

- 第一阶段目标是工程骨架和契约，不宜被 C ABI 的指针所有权细节拖慢。
- 但完全不考虑 C ABI，后续替换 DSSI 热加载时会返工。
- 因此第一阶段以 C++ API 推进，同时把 C ABI 作为“边界设计约束”写进头文件注释和命名规划。

### 8.4 第一阶段具体落点

`include/reconstruct_one_frame/reconstructInterface.h` 第一阶段建议包含：

- C++ API：完整定义 `StatusCode`、`Status`、`ImageView`、`StripeImage`、`StripeFrameGroup`、`FrameResult`、`ReconstructEngine`。
- C ABI：只预留最小导出命名和注释，不跨 DLL 传复杂对象。

第一阶段不要做的事情：

- 不让 C ABI 接收 `std::vector`。
- 不让 C ABI 接收或返回 `cv::Mat`。
- 不设计复杂内存释放回调。
- 不为了兼容 DSSI 立即重做 `threeScan_*`。

第二阶段或 DSSI 改造阶段再决定：

- 是直接链接 C++ import lib。
- 还是使用完整 C ABI。
- 还是保留 `threeScan_*` wrapper 作为短期迁移桥。
