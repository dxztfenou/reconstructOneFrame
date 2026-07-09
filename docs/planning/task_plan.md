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
