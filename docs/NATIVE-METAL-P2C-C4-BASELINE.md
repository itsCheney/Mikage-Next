# P2C C4：内置 extrans GPU 驻留记录

日期：2026-10-07。状态：本地实现、审计和 portable 验证完成；Apple Objective-C++/MSL/Swift、device/simulator 构建、App XCTest 与新真机配对待验。没有提交或推送，不宣称性能改善。本包不实施 C1–C3。

## 版本、原修改与日志来源

| 仓库 | 实施前和当前 HEAD |
| --- | --- |
| 主仓库 | `4943c0e47665433ef0c1d139d9043c408ef5c33a` |
| Runtime | `9efe69b102db7fa00f446efbe79c18a225b5b7ab` |
| Core | `860204a34f5d52f96df989bb82210f72bd45233d` |

实施前两个子仓库干净，主仓库仅计划文档已有 70 行新增、25 行删除。原 diff 原样保存在 [main.before.patch](native-metal-p2c-c4-baseline/main.before.patch)，原计划 SHA256 为 `85fca5d6955ff5512c1b08240c7bf81684bfbb33fcd934f92ed103257206bc64`。本轮在该版本上追加签收，没有撤销原修改。

输入为 `perf_log/After_C0` 的八份 JSONL，revision 均为 `4943c0e47665`；文件哈希见 [before.json](native-metal-p2c-c4-baseline/before.json)。千恋样本 `194874F4…` 有 263 次 / 2,181,427,200 bytes output upload、40 次 / 420,249,600 bytes source read、763,422,711 ns read wait；DRACU 样本 `9128675A…` 有 14 次 / 51,609,600 bytes output upload、17 次 / 83,558,400 bytes source read、132,926,002 ns wait。两份均从 thermalState=2 采样，分别含 109、52 个 work profile 窗口，帧样本丢失为零。

旧日志没有 handler 名称，不能据此断言具体效果。用户选择直接覆盖全部七个内置 extrans；后续须用新诊断确认真实类型、存档与输入步骤。这批仅是现象和归因证据。

## 实现与支持域

独立 `.def` 保持 `Unsupported=0`、七种 handler 为 1–7、`Count=8`；运行时同源注入 MSL，无生产构建期代码生成。普通 Layer 编号、70 个注册名称和 GPU 描述符不变。backend 增加默认关闭的 transition 能力与执行入口、只读最后拒绝结果；没有修改 `iTVPDivisibleTransHandler` 虚表。

| Handler | GPU 契约 |
| --- | --- |
| mosaic | 原块中心、块偏移、裁剪与独立整数 RGBA Blend |
| wave | 每个 Process 按原 rad 累加生成位移；原普通 / alpha / add-alpha 三种双源混合 |
| ripple | 原 displacement/drift 表、四象限、signed-byte 位移及反射；匹配 Apple C 分支的清零 alpha 行为 |
| turn | 原 64×64 phase/line 表、16.16 定点采样、末端背景及 gloss |
| rotatezoom / rotatevanish / rotateswap | 原 CalcPosition/AddSource/DrawData，按原区域次序执行 Copy/StretchCopy/LinTransCopy |

逻辑画布、源纹理及输出尺寸分别传递；不把 1920×1440 / 1280×960 的源重新缩放成输出画布。保留 StartProcess、EndProcess、MakeFinalImage，不新增首尾帧快捷返回。

支持同会话 RGBA8、定义良好的参数和已检查的 ROI。源/目标 pinned 或 lease、实际目标别名、资源或管线不可用时 CPU 回退。provider 别名在 COW 前拒绝；目标 COW 后重新查询源，防止读旧快照或悬空指针。共享 bitmap 的正常 COW、区域外像素及 point/alpha/CPU cache 语义保留。

所有资源、参数与采样边界检查发生在目标编码前。静态表最多四项、总计 64 MiB；动态行参数复用 staging 生命周期，同一不可变 row snapshot 在同一 command buffer 内复用，跨 command buffer 不覆写旧数据。uniform、row/table 和必要 alpha table 上传单列。沿用 16 MiB / 2048 ops 提交预算，没有逐帧专用 submit 或 wait。

handler 的 bad_alloc 捕获只覆盖参数构造；GPU 调用位于其外。backend 在 dispatch 前的分配失败返回 CPU，dispatch 后异常传播为引擎失败，禁止重复 CPU 执行已写目标。

**原软件不安全域的明确处置：**mosaic `maxsize<2`（`maxsize=1` 的 signed/unsigned HalfTime 运算可产生负块尺寸和偏移溢出）；ripple `maxdrift=0` 的空表；奇数 ripple 画布落在旧 floor-midpoint 小半区、导致 map 不够的中心；空尺寸、非法 wave type、非有限相关参数及 rotate 超出定点尺寸上限。它们提前报错，不送回不安全 CPU 路径；其余定义良好输入保持原结果。

## 诊断和接口

C0 的 origin/top-N、frame samples、采样周期和 workProfileVersion=2 不变。新增独立 transitionProfileVersion=1，64 条按请求/实际 provider、参数、尺寸及原因聚合的记录；每条包含独立帧、GPU/CPU/passthrough 调用、像素、实际 read/upload 四指标及参数上传。帧 token 跨 Layer 唯一；多区域同帧不会重复累计。超长名称/metadata、容量耗尽显式 overflow，不截断合并。

scope 在传输开始捕获的 generation 下归因；关闭诊断、换会话、异常和嵌套恢复均有测试。已有 crossfade/universal/scroll 通过真实 RenderManager 成功/拒绝记录路由，不因存在 GPU 描述符就宣称 GPU 成功。

C profile 尾部追加 `transitionProfiles[131072]`、独立版本、丢失与 overflow；静态边界和 worst-case uint64/escape 测试保证完整 NUL 结尾。只读 accessor `MikageKRKRLayerWorkProfileTransitions()` 返回借用视图，Swift 在 profile 存活期间复制。host 和 App 必须一起重建。分析器优先使用现有完整 origins，不重复叠加转场细分；旧数据标记类型未知。

## 本地验证

平台：Windows 11，MinGW GCC 13.2，Release，portable/device-double；不是 Apple GPU 结果。修改前保存两套通过输出，见证据目录。修改后：

- MetalLayer CTest **4/4**，MetalRenderBackend **1/1**，无 skip。
- **1962** 个真实七种 handler CPU / 生产 MSL scalar 精确 frame-region 案例；full/reversed 7-row/tiny ROI、非零偏移、两种画布和更大源/目标、time endpoints/jumps、所有 turn phases、wave 三 alpha、ripple 四波宽及大 drift。
- 七种真实 handler 的 pipeline 拒绝后 CPU 像素回归，以及 dispatch 后 bad_alloc 不重跑 CPU；真实 bitmap COW、dirty overwrite、alias、lease、非法 kind、诊断开关回归通过。
- 原有 **78,628** 次 surface 比较、三 session 及 P0/P1A/P1B/P2A/P2B 回归保留，精确容差未放宽。C4 在独立 session 测试，避免预热 alpha 表破坏原有冷表故障 fixture。
- 分析器 **17** 项 fixtures、bridge 完整边界/只读测试、frame-timing `-Wall -Wextra -Werror` 检查通过；八份 C0 旧日志经扩展分析器重新读取。
- 完整生产 `TVPTrans.cpp` / `tjsNativeLayer.cpp` 的 syntax 编译通过，原始命令与编译器输出保留。bridge/profile 栈和结构大小另存；当前主要 Triangle bridge 栈链约 426 KB。

主要命令（根目录 PowerShell；TEMP/TMP 设为工作区 build 路径）：

```powershell
cmake --build build/metal-layer-tests --parallel 2
ctest --test-dir build/metal-layer-tests --output-on-failure
cmake --build build/metal-render-tests --parallel 2
ctest --test-dir build/metal-render-tests --output-on-failure
python scripts/check-krkr-frame-times.py
& build/native-metal-p2c-c4-baseline/production-syntax.ps1
```

前两套构建目录沿用既有 CMake Release 配置；完整配置和编译器路径见日志。原始命令/输出、输入哈希及签收元数据保存在 [证据目录](native-metal-p2c-c4-baseline/manifest.json)。三层完整本地 patch 与所有新增源文件快照另存 `build/native-metal-p2c-c4-baseline/`；其路径和哈希见 manifest。

## 待验与真机清单

- Apple Objective-C++ / MSL 编译和真实 GPU 执行；默认 tile 与强制 compute 路由、实际 submit/wait、缓存淘汰及 command-buffer/staging 生命周期。Windows scalar 不能替代它们。
- device/simulator framework + App 构建、扩展后的真实 Swift/C importer smoke、App XCTest 和设置页；131072-byte bridge 的 SDK 导入/栈行为需实际检查。
- 千恋主回归、DRACU 交叉回归：保存 handler、存档、输入顺序、源/输出尺寸、设备/OS、三层完整提交、包哈希、诊断/渲染设置、预热和时长。每组重复三次，优先 thermalState=0，逐窗记录 thermal 变化。
- 先建立驻留输入再计量：支持域的 transition.source readback、transition.outputOverwrite upload、CPU fallback 及归因 wait 为零；必要参数首次/逐帧上传分开计量。未知/未支持类型必须有具名 CPU 原因。
- 合并完整窗口的原始帧样本，nearest-rank p50/p95/p99、超过 50 ms 的数量及缺失/overflow/丢失显式报告；不平均窗口 p95，不把历史 thermal=2 样本混入配对百分比。

本地 HEAD 尚未变化，因此任何未提交构建仍显示原提交的 HEAD；判断具体构建还须结合三层 diff 和包哈希。本轮没有新设备数据，不补零或推断 FPS、温度、功耗改善。

## AppleClang 严格警告补修：2026-10-07

用户提供的 CI 日志中，`tjs-shutdown-tests` 已构建并通过 1/1；失败发生在其后的 `check-krkr-frame-times.py`。AppleClang 21 在 `-Wall -Wextra -Werror` 下拒绝 `TransitionScope` 构造函数使用布尔按位 `&`，报 `-Wbitwise-instead-of-logical`。补修开始时本地短 HEAD 为主仓库 `29c2084900a2`、Runtime `d51f5de158f7`、Core `e5d66dcb61bc`；日志未提供 CI checkout SHA，不将其推断为上述提交。

三个 `TransitionString()` 调用改为分别保存布尔结果，再用 `&&` 合并，保留三项始终校验和填充的语义，不关闭严格警告。本地 Windows/GCC 已重新构建并通过 TJS shutdown 1/1、MetalLayer 4/4、frame-timing 及该 CI 步骤后续五项脚本。补修未提交；实际 AppleClang CI 重跑及其后 Apple 专项验证仍待确认。原 C4 验收日志和 manifest 保持为历史证据。
