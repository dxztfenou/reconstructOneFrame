# reconstructOneFrame 破坏式 init/setConfig/calc 接口重构设计

> 日期：2026-07-16
> 范围：D:\code\reconstructOneFrame 对外 ABI、C++ engine 生命周期、DSSI 推送式 18 图输入契约
> 决策：不再以 v1 兼容为主路线，破坏式替换为 init()、setConfig()、calc() 生命周期。

## 1. 目标

本轮重构把项目从“创建 session 时顺带加载配置并计算 process_frame()”改成明确的三段生命周期：

1. init()：创建/初始化运行时上下文，只负责对象、日志、CUDA/runtime 基础资源的生命周期入口。
2. setConfig()：加载算法配置、标定、输出策略并生成 capture plan / execution plan。
3. calc()：接收 DSSI 推送的一组图像并执行核心计算。

销毁接口不命名为 release()，避免误解为引用计数或只释放部分资源。新接口使用：

- shutdown()：停止/排空当前上下文中的计算资源，使其可重新 setConfig()。
- destroy()：析构 opaque context，释放对象所有权；调用后 handle 不再可用。

## 2. 对外 ABI

公开 ABI 使用一个破坏式 v2 RofApi 函数表：

- rof_get_api(major=2, minor=0) -> RofApi
- RofApi.init(...) -> RofContextHandle
- RofApi.set_config(handle, ...)
- RofApi.get_capture_plan(handle, ...)
- RofApi.get_camera_model(handle, ...)
- RofApi.calc(handle, ...)
- RofApi.copy_last_error(handle, ...)
- RofApi.shutdown(handle, ...)
- RofApi.destroy(handle)

ABI 里只允许固定宽度 POD、pointer + byte_size + stride + count、opaque handle 和 caller-owned output buffer。禁止跨 DLL 传递 cv::Mat、STL 容器、C++ 字符串、异常或 allocator 相关对象。

## 3. 输入契约

calc() 输入为 DSSI 推送的一帧完整 capture group：

- 左相机图像 stack。
- 右相机图像 stack。
- 当前生产配置为 18 张图：5 + 5 + 5 相移条纹，加 BGR 三彩辅助图。
- 每个 stack 显式声明 width/height/row_stride/image_stride/image_count/element_type/memory_kind/coordinate_space。
- 当前生产 ABI 支持 host UInt8 和 Float32 输入；后续可以扩展 device memory kind，但不能改变现有字段语义。

calc() 不依赖 OpenCV 输入类型。DSSI 可在 host 侧把自己的采集内存直接声明为 RofImageStackView。

## 4. 输出契约

输出继续采用 caller-owned 连续内存 view：

- depth：Float32 x 3，左相机毫米坐标。
- normal：Float32 x 3，左相机坐标系法向。
- color：UInt8 x 3，rectified left image 坐标。
- quality：UInt16 x 3，质量语义、分数和原因。
- metrics：frame id、状态码、输出尺寸、点数、耗时。

输出不是 OpenCV 对象。DSSI 如果需要 cv::Mat，只能在 DSSI 侧包装 caller-owned buffer。

## 5. 内部工作单元

calc() 内部按工作单元组织，不再把 SingleFramePipeline::run() 当成唯一大函数：

1. input_contract_validation
2. calibration_contract_validation
3. image_preprocess
4. wrapped_phase
5. phase_unwrap
6. matching
7. reconstruction
8. quality
9. output_materialization
10. diagnostics_output

第一步先把现有 pipeline 的阶段统一纳入 calc() 语义；下一步再把这些阶段抽成显式 WorkUnit 接口，以便实现 device-resident stage graph、stream/event 串联和有界 workspace 复用。

## 6. 返回值统一

所有主要 ABI 函数统一返回 int32_t 状态码，同时通过 RofStatus 返回：

- code
- severity
- flags
- 最近错误文本通过 copy_last_error() 获取

C++ 内部继续使用 Status { StatusCode, module, message }，但公开边界不再出现 bool、裸 -1、日志字符串或隐式异常。

## 7. 破坏性变更

本轮有意破坏旧接口：

- 删除旧 ABI v1 类型名和函数表字段：RofApiV1、create_session、process_frame、drain、destroy_session。
- DLL 动态导出只保留 rof_get_api 作为稳定入口。
- threeScan_* 兼容 shim 不再作为默认导出面；DSSI 必须迁移到 v2 init/set_config/calc。
- C++ ReconstructEngine 改为 init()、setConfig()、calc()。

## 8. 验收标准

- C header 可作为 C translation unit 编译。
- ABI layout test 固定 v2 结构大小、关键 offset 和常量。
- 插件契约测试覆盖：ABI major mismatch、init()、缺配置 set_config()、有效配置、capture plan、camera model、空输入 calc()、shutdown()、destroy()。
- DLL 动态测试验证只要求 rof_get_api；旧 threeScan_* 不再是必需导出。
- Release 构建和 CTest 回归通过。
