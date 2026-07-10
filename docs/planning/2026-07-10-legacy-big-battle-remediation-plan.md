# Legacy 大比拼差距修复计划

> 日期：2026-07-10
> 数据集：`D:\Data\output260707_gypsum\Upper`
> 基线：Legacy `Upper\<frame>\0.exr`

## 1. 结论

当前结果在**正常帧点数下限和热路径效率上没有重大落后**，但尚不能宣布整体追平 Legacy。

最需要确认的问题不是“点数太少”，而是**Legacy 稀疏/零点区域中新算法恢复的大量增量点是否真实有效**。这些点可能是误点，也可能是相较 Legacy 的有效提升，不能只根据 Legacy 点数直接定性。

### 已证明没有重大落后的指标

- 全量 466 帧完成，447 帧的 Legacy 点数大于 0。
- 可比帧总点数比例为 `1.166876`。
- 单帧点数比例 median `1.099007`，P10 `1.017720`。
- 有 11 帧点数少于 Legacy，但只有 2 帧低于 Legacy 的 98%；最低比例 `0.977822`。
- `point_count_losing_frames=0` 实际表示“没有低于 Legacy 95% 的帧”，不等于所有帧点数都不少于 Legacy。
- 新算法同进程热运行 frame 100..104 的 median 为 `56.35ms`；Legacy app median 为 `434.659ms`。

### 尚未证明追平的指标

- 全量 gypsum 数据只比较了点数，没有逐帧几何误差。
- `singleStripe` group 1 的 nearest RMS `0.0626mm`、P95 `0.1029mm` 只证明一个样本接近。
- 当前 post-unwrap phase remap 只是 Legacy pre-phase intensity remap 的近似，洞分布和边界行为可能不同。
- 当前性能比较包含不同运行外壳，仍需统一 warm-up、I/O、输出开关和进程生命周期。

## 2. 核心待证：增量点云真实性

- Legacy 零点帧 `447..465` 共 19 帧，新算法每帧仍输出约 `85k..90k` 点。
- Legacy 点数小于 100 的帧有 21 帧；小于 1,000 的帧有 23 帧。
- 点数比例超过 2 倍的帧有 30 帧，超过 10 倍的帧有 5 帧。
- frame 0：Legacy `50` 点，新算法 `74,923` 点，`frameQualityScore=0.577159`，`boundaryRatio=0.912936`。
- frame 445：Legacy `13` 点，新算法 `75,253` 点。
- frame 446：Legacy `244` 点，新算法 `77,446` 点，`frameQualityScore=0.457082`，`boundaryRatio=0.913411`。
- frame 447：Legacy `0` 点，新算法 `85,256` 点，`frameQualityScore=0.171903`，`boundaryRatio=0.903807`。

总点数高出 Legacy 16.7% 可能包含误输出，也可能代表有效覆盖提升。Legacy 只能作为历史行为基线，不能作为几何真值；下一步必须把 Legacy 重合点和新算法增量点分开验证。

## 3. 修复优先级

### P0：修正 benchmark 口径

1. 扩展 `benchmark.csv`：
   - 低于 Legacy 100%、98%、95% 的帧数。
   - Legacy zero/new nonzero 和 Legacy sparse/new dense 的帧数。
   - 每帧 quality score、coverage、connected score、boundary ratio、P90 quality。
   - cold/warm 和各 CUDA/CPU stage 时间。
2. 重命名 `point_count_losing_frames`，明确其 95% 阈值。
3. benchmark 默认使用同进程批量模式，前 5 帧作为 CUDA warm-up。
4. 将帧分为 normal、sparse、zero-output 三组分别报告。

### P0：建立增量点独立验证

1. 将点云划分为 Legacy 邻域重合点和新算法 extra-only 点，单独统计 extra-only 的数量、空间分布和质量。
2. extra-only 点使用独立证据验证，不以 Legacy 是否存在对应点作为唯一标准：
   - coverage / center coverage
   - largest connected component ratio
   - connected surface score
   - boundary ratio
   - point quality P50/P90
   - high-confidence ratio
   - phase residual、matching cost、candidate uniqueness、左右一致性
   - 与相邻帧变换到同一坐标系后的表面支持率
3. 如果数据集中存在最终融合模型、mesh 或可靠多帧点云，优先用其作为独立参考。
4. 先导出 466 帧质量特征和 extra-only 特征 CSV，再决定是否需要整帧或点级 reject。
5. 不使用单一 `frameQualityScore`，也不把 frame 0、445、446、447 的 Legacy 点数直接当成标签。

### P1：补齐全量几何对比

1. 扩展 benchmark 读取 Legacy XYZ EXR，不只统计非零点数。
2. 输出双向 nearest/Chamfer、RMS、P50/P95/P99、centroid、AABB、median Z bias 和 XY coverage overlap。
3. 首批代表帧：
   - 启动稀疏段：0、1、2、3
   - 正常段：37、100、133、222、362
   - 中间异常段：291、293、295、302
   - 尾部稀疏/零点段：445、446、447、465
4. 对 extra-only 点单独计算跨帧支持率、局部平面残差和法向一致性；若这些指标稳定，应认定为相对 Legacy 的有效提升。
5. 如果误差或无支持点集中在边界和洞区域，再迁移 exact intensity remap、matching validity 和 hole/filter 规则。

### P1：收敛算法差异

1. 将 rectification/remap 从 post-unwrap phase 近似改为 Legacy 同时序的 intensity remap。
2. 对齐 matching uniqueness、candidate、left-right consistency 和 disparity range。
3. 对齐洞边缘、局部一致性和 compact point 输出规则。
4. 保持每频率独立 `phaseStepCounts`，不退回固定全局步数。
5. 每次改动同时检查正常帧点数、extra-only 有效点保留率、无支持点比例和几何 RMS/P95。

### P2：性能与工程化

当前效率不落后，放在正确性之后：

1. 引入可复用 CUDA workspace，减少逐帧分配。
2. 分开统计 cold-start、文件 I/O、日志和算法 stage。
3. 只有 profile 证明 CPU quality 成为瓶颈后，才迁移 quality map 到 CUDA。
4. 保留效率门槛：hot median 不慢于 Legacy，目标 hot P90 不高于 `80ms`，首帧初始化单独报告。

## 4. 验收标准

### 增量点真实性

- Legacy zero-output 的 19 个尾帧必须完成独立验证，不能直接要求归零。
- 有跨帧或独立模型支持的 extra-only 点必须保留，并计为相对 Legacy 的提升。
- 缺少相位、匹配、局部几何或跨帧支持的点才进入 reject 候选。
- sparse 帧建立独立报告，既不以“点数越多越好”，也不以“Legacy 点少即为正确”判断胜负。

### 点数

- normal 可比帧点数比例 P10 不低于 `0.95`。
- normal 可比帧低于 Legacy 95% 的帧数为 0。
- 报告所有低于 1.0 的帧。

### 几何

- 代表性正常帧双向 nearest RMS 目标不高于 `0.10mm`。
- P95 目标不高于 `0.20mm`。
- centroid、AABB 和 Z bias 不出现系统性漂移。

### 效率

- 使用同进程、相同 I/O/输出开关、固定 warm-up 的公平口径。
- 新算法 hot median 不慢于 Legacy。
- correctness 修复后必须运行性能回归。

## 5. 实施顺序

1. 修 benchmark 报告口径和全量质量特征导出。
2. 导出 extra-only 点及其相位、匹配、局部几何和跨帧支持证据。
3. 增加 Legacy EXR 全量几何比较。
4. 根据几何和洞分布证据迁移 exact remap 与 matching validity。
5. 运行全量 466 帧回归，最后再做 CUDA workspace 优化。

## 6. 当前建议

下一步优先实施 P0，不应先继续优化点数或速度。首先需要证明 Legacy 稀疏区域中的新增点究竟是有效恢复还是无支持点，再决定是否增加点级或整帧裁决。
