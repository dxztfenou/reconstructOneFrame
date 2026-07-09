# 第六阶段 单帧质量评价实施计划

> 日期：2026-07-09
> 项目：`D:\code\reconstructOneFrame`
> 阶段定位：在第五阶段真实 CUDA 点云重建后，仿照 Legacy 单帧点质量图与整帧质量统计，建立质量评价输出契约。

## 1. 目标

- 仿照 Legacy `point_reliability_map` 的点级 reason bits 和整帧质量 score。
- 输入使用第五阶段 CUDA 点云结果，不回退到 fake depth/normal/quality。
- 质量评价先做 CPU 汇总模块：点级质量、reason histogram、coverage、center ROI、连通性、整帧 score。
- pipeline 输出 `quality_evaluate` stage，并将 `FrameResult::qualityComputed` 置为 true。
- sample summary 输出整帧质量摘要；不接入 TensorRT/AI，不修改 DSSI。

## 2. Legacy 参考

- Legacy 头文件：`legacy/teethscanalgorithm3x4/include/point_reliability_map.h`。
- Legacy 实现：`legacy/teethscanalgorithm3x4/src/point_reliability_map.cpp`。
- 点级 reason bits：semantic background、low modulation、saturation、high match cost、candidate ambiguous、depth range、hole edge、local discontinuity 等。
- 整帧统计：valid coverage、center coverage、largest connected component、boundary ratio、mean/p50/p90/p95 score、frameQualityScore、lowQualityReasonBits。
- Sentinel 约定：Legacy 将整帧 score 写入 quality map `(0,0)`；本阶段以 `FrameQualitySummary` 字段承载，不强制写二进制图。

## 3. 实施步骤

| 步骤 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 新增 `src/quality/PointReliability.h/.cpp`，迁入 Legacy 点质量与整帧统计公式 |
| 2 | complete | 扩展 `PointCloudReconstructionResult`，保留 grid 点云用于质量图构建 |
| 3 | complete | 扩展 `ReconsConfig` 读取 `qualityInfo*` / `qualityMap*` 字段 |
| 4 | complete | 扩展公共 `FrameResult` 和 diagnostic summary 输出质量摘要 |
| 5 | complete | 接入 pipeline，在点云重建后执行 `quality_evaluate` |
| 6 | complete | 新增 `tests/point_reliability_test.cpp`，验证 reason bits、coverage、sentinel 等价口径 |
| 7 | complete | 运行 Release build、CTest、真实 group 1 smoke，并更新规划文档 |

## 4. 验证命令

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --output build\phase6\group1_quality --compare-legacy D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply
```

## 5. 验收口径

- Release build 通过。
- CTest 通过，新增质量评价单测。
- 真实 group 1 运行包含 `quality_evaluate` 阶段。
- `FrameResult` 输出 `qualityComputed=true` 与质量摘要，包括 frameQualityScore、coverage、centerCoverage、validPoints、highConfidenceRatio、lowQualityReasonBits。
- 文档记录与 Legacy 的等价点和差异点。

## 6. 完成结果

- Release build 通过。
- CTest 通过，12/12 tests passed。
- 真实 group 1 运行包含 `quality_evaluate` 阶段，并输出 `qualityComputed=true`。
- 真实质量摘要：`frameQualityScore=0.766212`、`pointQualityScore=0.851249`、`coverage=0.566984`、`centerCoverage=0.737728`、`connectedSurfaceScore=0.492734`、`validPoints=96160`、`highConfidenceRatio=0.0534006`、`lowQualityReasonBits=1922`。
- 输出点云仍为 `build\phase6\group1_quality\depth_points.ply`，Legacy 几何对比维持第五阶段水平：nearest RMS `0.0626mm`、P95 `0.1029mm`。

## 7. 差异与风险

- 本阶段质量评价是 CPU 汇总模块，点云重建仍是 CUDA；质量评价未迁成 CUDA kernel。
- 当前没有 TensorRT/semantic mask 输入，因此 `ReasonSemanticBackground` 不参与真实 group 1 评估。
- 当前点级质量图驻留在 `PointReliabilityResult::map`，sample 只输出整帧摘要，尚未落盘质量图文件。
- 高频调制由左侧最高频条纹图计算；与 Legacy intensity-remap-before-phase 的精确时序仍有差异。
