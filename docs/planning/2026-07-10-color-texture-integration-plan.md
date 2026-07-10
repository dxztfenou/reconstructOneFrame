# 彩色纹理集成实施计划

## 背景

当前 `PointCloudReconstructorCuda::assignColor()` 从左相机最高 projector index 的单张相位灰度图取值，并将同一灰度复制到 RGB。frame 100 实测新 PLY 的 RGB 三通道完全相等比例为 `100%`，只有 `151` 种灰度颜色；Legacy PLY 为真实彩色，RGB 三通道完全相等比例为 `0%`，有 `6164` 种颜色。

Legacy 的生产链路明确为：

1. 左相机相位图之后的三张辅助图作为 `B/G/R` 输入。
2. 三张图与相位图使用相同左相机整流映射。
3. 对每个像素应用线性 `3x4` 颜色校正矩阵。
4. 对校正后的每个通道应用 `gamma=0.5`。
5. 得到 `CV_8UC3 BGR` 结果矩阵，并按与 depth 相同的 rectified `(u,v)` 绑定到点云。

## 目标

- 使用真实 `SourceImg/L15.bmp/L16.bmp/L17.bmp` 生成 rectified 彩色纹理。
- 将辅助帧 projector index、`3x4` 颜色矩阵和 gamma 参数化。
- 复用现有 CUDA rectification 数学，不增加 OpenCV 运行依赖。
- 只在 `materializeVertices=true` 时生成和回传颜色，count-only 热路径保持不变。
- 重新生成 `D:\Data\output260707_gypsum\Upper` 彩色 PLY，供人工盯帧。

## 配置契约

新增字段：

```json
"colorTextureEnabled": true,
"colorTextureProjectorIndices": [16, 17, 18],
"colorCorrectionMatrix": [
  1.17255, -0.0715125, -0.168186, 23.8505,
  -0.143254, 1.20397, -0.308086, 35.2089,
  -0.077533, 0.0993569, 1.39308, 28.8695
],
"colorGamma": 0.5
```

- projector index 使用项目内部 one-based 语义；`SourceImg` loader 映射为 `L15/L16/L17`。
- 三个输入通道固定为 `B/G/R`，矩阵三行输出也固定为 `B/G/R`。
- 旧配置缺少 `colorTextureEnabled` 时默认关闭，不改变旧调用方。
- 启用后要求三个 projector index、12 个矩阵值和正 gamma。

## 数据流

1. `InputManifest` loader 读取三张左相机辅助灰度 BMP，合并为一个 packed `UInt8 BGR` buffer，并写入 `StripeFrameGroup::leftColor`。
2. `PointCloudReconstructorCuda` 在完整物化路径中上传 raw BGR。
3. CUDA kernel 对 rectified 输出像素复用左相机 `KK_L/Dist_L/R_L/P_L`，双线性采样 raw BGR。
4. kernel 应用参数化 `3x4` 矩阵和 gamma，输出 rectified `uchar3 BGR`。
5. D2H 后按像素索引写入 `PointCloudVertex.r/g/b`。
6. 配置关闭时保留现有灰度 fallback；count-only 路径不加载、上传或生成颜色。

## 验证

- 配置测试：合法矩阵通过，错误数量、错误 projector index 和非正 gamma 被拒绝。
- loader 测试：启用颜色时加载 BGR 三通道，禁用时保持原图数量。
- CUDA/真实数据：
  - frame 100 点数必须保持 `84,833`。
  - 新 PLY RGB 三通道完全相等比例显著低于原来的 `100%`。
  - 与当前 Legacy frame 100 PLY 比较颜色均值、通道标准差和颜色数量。
  - 生成 frame `0..465` 按帧子目录彩色 PLY。
- 最终执行 Release 构建、CTest 和 `git diff --check`。

## 边界

- 不修改 Legacy。
- 不迁移 AI texture mapper。
- 不实现跨帧纹理融合、曝光融合或右相机补色。
- 不提交、不推送。

## 实施结果

- 配置、loader、CUDA 颜色整流/矩阵/gamma、测试和真实数据重算均已完成。
- loader 增加 `includeColor` 消费边界：无 PLY、无 Legacy compare 时不读取 RGB 辅助帧。
- frame 100 保持 `84,833` 点，equal RGB ratio=`0.0`，unique colors=`5,148`。
- Legacy frame 100 mean RGB=`[253.208,203.734,180.411]`；新实现 mean RGB=`[253.656,205.767,182.559]`。
- Upper `0..465` 解析 466 帧，生成 442 个彩色 PLY；24 个无支撑帧保持 `ReconstructionInsufficient`。
- 466 帧点数与此前 filter-only 基线逐帧完全一致。
- 输出根目录：`build/color_texture_upper_20260710/full_v1`。
- 汇总文件：`build/color_texture_upper_20260710/full_v1_summary.json`。
- `includeColor` 无默认值，调用方必须显式声明颜色消费需求；两个 loader 在索引前本地校验 projector index 数组。
- 规格审查复核 `PASS`；代码质量审查提出的两个 P2 已修复。
- 最终 Release 构建成功、CTest `10/10`、`git diff --check` 通过。
