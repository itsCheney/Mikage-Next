# C2B：GPU 命中与路由统计完整性修复

2026-10-09 本地验收。修复 `drawLine`、`drawPath`、`drawImageStretch` 的 native 参数识别，并将全量路由聚合与有界代表详情分开。没有改动 shader 数学或渲染算法，没有处理 Nekopara，没有提交或推送。Apple 联合构建及新真机 GPU 命中待验。

## 修改前基线

| 仓库 | HEAD（修改前后均不变） |
| --- | --- |
| 主仓库 | `0b557122fca5dfabafb2993af00a09507e24d975` |
| Runtime | `860d3de7e804def1f2dce195c7d754a678e8cb86` |
| Core | `6e75bbb32739a7ea8e2d851a88b8acb82c7179a3` |

三仓开始时干净。原计划、三个空 diff、原 LastTest 输出、平台、八份 `perf_log/After_P2C_C2B` 输入哈希先于修改保存在 `build/native-metal-c2b-hit-fix-baseline`。历史输出只保留来源，不替代本轮重新构建测试。

旧 C2B 包已有上述提交。其日志记录千恋 11 条、天使 32 条 CPU/arguments 路由；天使单窗口另有 39 条因旧详情预算省略，不能推断这 39 条的 GPU/CPU 分布，也不能由未出现 GPU 样例断言所有调用均 CPU。八份原始日志均未修改，本轮分析器仍保留 43 条已记录 CPU 路由和 39 条未知路由。旧记录不作为修复后基线。

## 根因与修复

生产 `NCB_REGISTER_SUBCLASS(Appearance)`、`NCB_REGISTER_SUBCLASS(Path)` 使用直接 native adaptor；旧预检却通过 `GdipWrapper<T>` 查询，真实对象因此被拒绝。原测试人为提供了同样的错误 wrapper/转换，掩盖了生产注册与预检的不一致。现在两类使用 `ncbInstanceAdaptor<T>`，`GdipImage` 继续使用生产 `GdipWrapper<GdipImage>` / `ImageConvertor`。

测试使用生产零参数 constructor 注册、默认 boxing/unboxing 及真实 NCBind/TJS；extractor 检查生产宏/转换形态，三项注册漂移测试必须拒绝提取。用真实注册运行旧预检产生预期失败（eligible binding 获取了 target CPU pixels），修复后通过精确像素、typed 返回值及 GPU 路由检查，见 `hit-repro.log` 与 `hit-final-diagnostics.log`。Layer/纹理/backend 仍为依赖替身；此处证明注册匹配，不证明 Apple kernel 执行。

预检没有增加脚本转换或属性 getter；对象类型、数值、paint、path、image/source 校验各有具体原因。PlainObject、record、租约、借用源、逃逸别名、采样、Clip/ROI、资源和预算限制保持。提交前拒绝只有一次 CPU 续跑；提交后异常不重放；返回对象仅装箱一次。

## 全量聚合与窗口协议

固定表为 3 个方法 × GPU/CPU/noop × 25 个具名原因，计数先于样例选择。每个路由决策累计 `calls、spanCount、sourceBytes、parameterBytes、scratchBytes`；重复调用与详情容量不足仍完整累积。计数使用受检饱和累加，溢出或未知字典项显式报告，分析器禁止把它当完整账。

32 个代表位置中，9 个保护位置保留每个方法/路由的首例，其余 23 个保留不同原因组首例。CPU 突发不会占用后来的 GPU 保护位置。只有被接纳的代表才查询可用的 texture/session/version 元数据，保存纯数值，不保存资源引用；无 identity 输出 null。详情跨窗口/会话的迟到元数据不会污染新窗口。

现有 `layer_work::Take()` 输出 `metal.layerSpan` version=2 的三类记录：

- `phase=aggregate`：所有非空方法/路由/原因组及五项指标。
- `phase=sample`：代表详情、sampleIndex、traceID、可用 identity 和单次指标；不参与总量累加。
- `phase=window`：窗口五项总量、GPU/CPU/noop 调用数、预期聚合行/样例数、重复省略、容量省略、invalidRecords 和 overflow。零调用窗口也有总账。

每条记录不超过 900 UTF-8 bytes，使用相同 generation 和递增 windowID。原 CPU consumer budget 保留旧字段；v2 完整性以新窗口总账为准，不以旧 spanRouteRecords 判定。`sourceBytes` 是 `parameterBytes` 的子集，`scratchBytes` 是逐调用 GPU 工作区需求之和，不是传输量或显存峰值；均不重复计入 C0 像素传输。CPU 预检拒绝未生成/提交 GPU packet 时这些 GPU 资源指标为零。calls 表示路由决策，不保证之后 legacy CPU 调用成功。

新增 `MikageKRKRLastLayerWorkProfileWindowID()` 只读 C accessor。它返回同线程最近一次成功 Take 的 ID，失败、关闭、换代或其他线程返回 0。现有 C profile typedef 逐字保持，profile version、旧字段、周期及 HUD 不变。App 在成功 Take 后立即捕获 ID，在原 `layerWorkProfile` 中附加字符串 `spanRouteWindowID`；host 与 App 必须一起重建。没有增加 GPU 查询、readback、submit 或 wait。

分析器按选定完整 work 窗口的 ID 对应 native 行，不用详情时间戳重选窗口。总量来自窗口总账，原因分布来自聚合，详情仅用于定位。分别提供 `totalsComplete`、`breakdownComplete`、`representativeCoverageComplete`：主动省略的代表不破坏完整聚合；缺行、重复、和不符、换代不符、覆盖不符或日志损坏会显式报告。v1 保持下界/未知语义，缺失数据不补零。

## 本地验证与证据

Windows 11 / MinGW GCC 13.2，MetalLayer Release/Ninja；GPU 为 device-double，数学对照为生产 MSL helper 的 C++ 提取。plutovg 1.3.3 原版和 overlay 的真实 C oracle 沿用，精确容差未放宽。本地测试没有 skip；Apple 专项没有在本机执行。

- MetalLayer CTest **6/6**、MetalRenderBackend **1/1**、TJS shutdown **1/1**。
- 三个真实绑定入口的精确像素、typed 返回、GPU 命中与零 target CPU acquire；诊断开关的 backend attempts、更新、获取/释放和像素完全相同，C0 传输不增加。
- 71 次同组全量指标、5,000 次 CPU 后出现 GPU、225 原因组（32 样例/193 容量省略）、空窗口、数值溢出、开关/会话重置，以及 metadata callback 内强制 Take/reset 的迟到 identity 隔离。
- **51** 个解析 fixtures，覆盖日志缺行、重复、聚合总账、代表保护位置、零窗口、覆盖范围和 v1 兼容。
- 实际生产 C++ emitter → Python analyzer 对接：**5,071** 次路由、2 个代表、2 个窗口（含空窗）精确对账。
- C11 header importer 签名、生产 C bridge 的成功/失败/线程/换代行为、App 接线/HUD静态检查；现有 C profile typedef 与旧 HEAD 逐字一致。
- frame-timing、point trace debug/release、strict C++17 `-Wall -Wextra -Werror` 诊断 TU，以及完整 native Layer、LayerExBase、LayerExDraw 生产 TU syntax。
- 八份旧日志 SHA256 不变，解析零 issues，43 条观察到的 CPU 路由与 39 条未知保持。

实际命令、三层 HEAD、状态、测试输出及哈希见 [manifest](native-metal-c2b-hit-fix-baseline/manifest.json)。完整本地补丁、未跟踪源快照、原计划和较大解析结果留在 `build/native-metal-c2b-hit-fix-baseline`，manifest 记录路径与哈希。受控 fixture 是合成验证，不能冒充真机数据；旧预检复现的失败是预期结果。

## Apple 与重采门槛

待验新增 accessor 的 Swift importer、host/App 联合构建、完整插件、Objective-C++/原生 MSL、default/forced compute、device/simulator、App 测试，以及诊断开关真实 submit/wait 不增加。

重采千恋、天使流程图，要求支持域出现真实 GPU 样例，所有完整窗口满足 `calls = gpuCalls + cpuCalls + noopCalls = 聚合 calls 之和`，五项指标同窗口对账，预期行数无缺失。每个 work 窗口包含匹配 ID 的 native 总账，零调用也要匹配。检查代表覆盖和容量省略，并保留仍走 CPU 的具体原因；目标无回读或整图 output upload，必要输入/参数传输另报。每组同设备/OS、场景、步骤、分辨率、设置、热状态、时长重复三次，未配对不宣称性能改善。Nekopara 留待之后处理。
