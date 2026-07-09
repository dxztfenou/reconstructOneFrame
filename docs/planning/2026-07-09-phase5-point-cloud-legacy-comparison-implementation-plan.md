# 第五阶段 CUDA 点云重建与 Legacy 对比实施计划

> 日期：2026-07-09
> 项目：`D:\code\reconstructOneFrame`
> 阶段定位：在第四阶段真实数据 CUDA absolute phase 后，进入 CUDA disparity/reproject 点云重建，并与历史 Legacy `depth_points.ply` 做脚本化对比。

## 1. 目标

- 使用真实数据 `D:\Data\Calib\2607011016_mach6\singleStripe`，不退回 synthetic 或 CPU-only。
- 支持真实 `D:\Data\Calib\2607011016_mach6\calibResult.json` 与 Legacy `D:\Data\Calib\2607011016_mach6\calibParams.yml` 的 OpenCV matrix 读取；最终贴近 Legacy 的基线采用 `calibParams.yml`。
- 保留 `phaseStepCounts=[3,5,5]` 的每频率独立相移步数契约。
- 将 pipeline 从 `phase_unwrap_cuda` 推进到 `point_cloud_reconstruct_cuda`。
- 输出 `depth_points.ply`，并支持 `--compare-legacy <ply>` 对比历史 Legacy 输出。

## 2. Legacy 对比基线

- Legacy CUDA 入口：`D:\code\teethscanalgorithm3x4\src\calcDepthImage.cu::computePointCloudFromPhase()`。
- 历史输出：`D:\Data\Calib\2607011016_mach6\singleStripe\output\<group>\depth_points.ply`。
- 本阶段先迁移 disparity + Q reproject + 3x3 点云平滑 + 非连通孤岛过滤 + 法向估计。
- 暂不迁移金属模式 hole fill、AI semantic mask、完整 quality_info_map。

## 3. 实施步骤

| 步骤 | 状态 | 内容 |
|---|---|---|
| 1 | complete | 扩展 `calibResult.json` 解析，支持 OpenCV matrix object 的 `data` |
| 2 | complete | 扩展重建/匹配配置字段读取 |
| 3 | complete | 新增 CUDA 点云重建模块和 PLY IO/比较模块 |
| 4 | complete | 接入 pipeline/sample 的 `--output` 与 `--compare-legacy` |
| 5 | complete | 用真实 group 1 对比 Legacy，记录点数、bbox、centroid、NN 距离 |
| 6 | complete | 根据差异定位并修正缺失 rectification/remap、过滤默认值和 Legacy 标定矩阵来源问题 |

## 4. 验证命令

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibResult.json --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --output build\phase5\group1 --compare-legacy D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply
build\Release\reconstructSample.exe --config D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json --calib D:\Data\Calib\2607011016_mach6\calibParams.yml --single-stripe-root D:\Data\Calib\2607011016_mach6\singleStripe --group 1 --output build\phase5\group1_calibparams_yml --compare-legacy D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply
```

## 5. 验收口径

- Release build 通过。
- CTest 通过，新增 CUDA 点云 smoke。
- 真实 group 1 能输出 `depth_points.ply`。
- 对比输出至少包含点数差、centroid、bbox、row-order 距离和近邻距离。
- 如果无法贴近 Legacy，必须记录根因假设和下一步修正，而不是只报告“能跑通”。

## 6. 完成结果

- Release build 通过。
- CTest 通过，11/11 tests passed。
- 真实 group 1 已输出 `build\phase5\group1_calibparams_yml\depth_points.ply`。
- 与 Legacy `D:\Data\Calib\2607011016_mach6\singleStripe\output\1\depth_points.ply` 对比：generated `96160` 点，legacy `101528` 点，点数差 `-5368`。
- 最近邻误差：mean `0.0535mm`，RMS `0.0626mm`，P95 `0.1029mm`。
- 主要修正：加入 rectification/remap 近似、默认关闭会显著改变点数的点云 smoothing/filter，并支持直接读取 Legacy `calibParams.yml`。

## 7. 遗留风险

- 当前 remap 在 absolute phase 后做近似，而 Legacy 是强度图 remap 后再算相位；最近邻几何已贴近，但无法保证逐点完全一致。
- `calibResult.json` 可以运行但不如 `calibParams.yml` 贴近历史 Legacy 输出，说明历史基线实际更接近 `calibParams.yml` 的矩阵集合。
- 当前颜色来自左侧条纹图灰度填充，不等价于 Legacy 的完整 RGB/纹理来源。
