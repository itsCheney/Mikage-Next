# P2C C0：传输归因、逐帧基线与版本标识

日期：2026-10-07。按本会话选择，本轮签收本地代码、portable 回归与可复测流程；Apple 编译、设置页实机显示与同条件真机 before/after 待验。不宣称性能改善，不实施 C1–C4。

## 实施起点与证据保留

三层 `metal_dev` HEAD 与实时 `git ls-remote` 一致，submodule 指针匹配：主仓库 `6e5cbf10e7dd58aa5d2c847213ecea3d82823dd7`，runtime `e0b0d3826bad2d3ff3ffa0c0a8df46cc444b6c29`，core `b8800eee7a831aa3ce8a51284c65389e9fe08dcf`。

两个子仓库干净；主仓库已有计划修改 81 行新增、9 行删除，SHA-256 为 `95cd9facfb3e19e75c893594d167225b30fe6f88b935035f37c43a4e0cd4f84c`。原文与原始 patch 保存于本地忽略目录 `build/native-metal-p2c-c0-baseline/plan-before.md`、`plan-before.patch`，本轮在其内容上追加签收，未重置或丢弃。未提交或推送。

## 已实现契约

同一次成功传输记录更新两个视图：旧 texture-ID top-8 保持格式；新 origin 视图按方向和名称聚合 calls/bytes/wallNS/waitNS，不按纹理或 lease 拆分。固定 64 槽，其中 40 槽保护 20 个既有名称的两个方向，24 槽用于动态来源；保留 `layerExBase`、`layerExBase.write` 等实际字符串。

来源最长 47 bytes，输出以 percent 编码处理分隔符和 UTF-8。超长、容量耗尽进入独立 read/upload overflow，并报告 oversizeRecords/capacityRecords；已有受保护组继续累计。按 waitNS、bytes 降序及名称/方向排序，低 bytes 的 stall 不受旧 top-8 影响。两视图同窗口总量（包含 overflow）一致。

四个实际路径在 backend 调用前捕获 generation：完整 read、point region read、direct Update、dirty ROI 延迟上传；禁用或代际失效不记入新旧 profile。缓存命中、空 ROI、失败不计成功传输。长来源在 Scoped acquire/release 与延迟 upload 中保留超长证据，包括同一 raw lease 的长→短来源，不静默截断合并；只有诊断字段变化，lease、dirty ROI、像素和 GPU 路由不变。

host `recordFrame()` 使用已有时间值记录 runtime 帧间隔/CPU wall，每窗口最多 2048 对 ns 样本。首帧未知间隔为 0；Take 保留上帧时间，resetStats、会话与诊断切换清历史，clock 回退不会 unsigned 下溢，满容量显式记录丢失数量。这里不是 GPU 时间或显示器呈现延迟。

C profile 在尾部追加字段，旧字段保持。`layerWorkProfile` v2 新增 `transferOrigins`、`originOverflow`、`frameSamplesNS`、`frameSamplesDropped`；桥接检查容量后完整复制，不剪断行。origin C 缓冲 16384 bytes，静态证明及最大值 fixture 验证容量；host/App 必须一起重建。没有新增 GPU query/readback/submit/wait，采样周期不变，HUD 不消费 profile。

设置页底部现为实际 Bundle 版本和等宽 `HEAD <12hex>`，共用 `AppBuildInfo` 与诊断 environment 的来源。合法完整 SHA 取前 12 位；缺失/占位/无效值显示 `HEAD 未知`。沿用 Release 构建的 Info.plist 注入，CI simulator 同样注入，并检查生成 App 的 plist。没有运行时 Git/网络查询、dirty 标记或构建时间。

解析器以新 origin 聚合为归因来源，不重复相加旧纹理数据；旧格式标为名称下界，overflow/missing/coverage 不一致显式报告。`--from-seconds/--to-seconds` 相对每个 game.begin，只选择完整 profile 窗口。合并原始帧样本后以 nearest-rank 求 p50/p95/p99；帧间隔排除未知 0、CPU wall 保留 0，长帧阈值严格大于 50 ms；不平均窗口 p95，不用 heartbeat 平均帧时冒充逐帧样本。

## 本地验证

Windows，GCC/MinGW，Release；Layer 使用 device double，nativeCompiled=false。

| 检查 | 结果 |
| --- | --- |
| 修改前 CTest | Layer 2/2，backend 1/1；78,622 表面比较、3 session |
| 修改后 CTest | **Layer 4/4，backend 1/1，0 skip**；**78,628 精确表面比较、3 session** |
| Collector/真实 C bridge | 跨纹理归因、保护/动态容量、转义、超长、最大数值、嵌套/异常、epoch、关闭、frame 连续/reset/2048 drop/worker 通过 |
| 生产 facade/device double | 7 组真实 read/write scope、默认 lock/scanline、tiny update、ROI/缓存、延迟长名/raw lease、四路径 mid-backend generation 切换、失败上传通过 |
| 开关无副作用 | 同负载开/关诊断的精确像素、GPU/fallback/bytes 和实际 backend 调用相同；native 分支额外检查 submit/wait，Apple 待执行 |
| Python 解析 fixture | **12 项通过**：新旧格式、对账/不双计、overflow/missing、多 session、整窗口与分位数反例 |
| frame-timing 检查 | 实际 production recordFrame/reset、窗口连续性、峰值与原 Emote 限流检查通过 |
| App 版本 | Bundle/设置/diagnostic/CI 静态连线检查通过；新增 4 项 XCTest，Windows 无 Swift/Xcode，未执行 |

原 UnivTrans/P1A/P1B/affine/perspective 像素回归及注册 85/70/70/0/15、kind Count=48 保持，容差未放宽。portable 不证明 Objective-C++、SwiftUI 或实际 GPU 顺序/性能。

## 命令与保存结果

Windows sandbox 的默认 temp 不可写，测试明确设置 TEMP/TMP 到上述 build 目录；这不是解析器失败。测试命令与具体工具路径保存在 metadata。

```text
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel 2
ctest --test-dir build/metal-layer-tests --output-on-failure --verbose
cmake --build build/metal-render-tests --parallel 2
ctest --test-dir build/metal-render-tests --output-on-failure --verbose
python scripts/check-krkr-frame-times.py --cxx C:/Strawberry/c/bin/c++.exe
```

- [修改前 Layer](native-metal-p2c-c0-baseline/metal-layer-before.log) / [backend](native-metal-p2c-c0-baseline/metal-render-before.log)：既有构建目录的实际重跑，未先重建。
- [修改后 Layer](native-metal-p2c-c0-baseline/metal-layer-after.log) / [backend](native-metal-p2c-c0-baseline/metal-render-after.log) / [帧检查](native-metal-p2c-c0-baseline/frame-timing-after.log)。
- [能力 JSON](native-metal-p2c-c0-baseline/capabilities.json) / [HEAD、平台、命令、diff 与哈希](native-metal-p2c-c0-baseline/metadata.json)。

三层 raw Git patch 包含新增文件，保存在忽略目录 `build/native-metal-p2c-c0-baseline`，只 reverse-check；metadata 不自哈希。

## 真机配对采样清单（待执行）

每组记录设备/OS、三层完整 commit 与 diff/包哈希、App 显示 HEAD、分辨率、renderer/validation/diagnostic 设置、游戏与存档、场景和每个输入步骤、预热与计量起止、运行时长、初始/末尾及沿途热状态，保留原始 JSONL 和 SHA-256。before/after 每组至少三次，使用相同设置与完整窗口，分别报告数据覆盖及丢失情况。

重放计划第 2.4 节的 transition 场景、DRACU-RIOT!、天使纷扰、9-nine 和其余横向样本；未提供确切存档/步骤的项标为缺失，不编造流程。源于 `6e5cbf10e7dd` 的历史日志仍是现象证据，旧 origin 下界/平均帧时不能替代新的完整聚合/逐帧基线。

报告每 origin 的 calls/bytes/wallNS/waitNS、overflow、帧间隔和 CPU wall p50/p95/p99/长帧、总体 fallback/reject、上传/回读、encoder/submit、内存与热状态。C0 本身只增加观察能力，不以这些数据宣称优化；C1–C4 各自另验。

待验：Apple 默认 tile/强制 compute、真实诊断开关无额外 submit/wait、iOS device/simulator host/App 重建、4 项 App XCTest、设置页窄屏显示与包内 HEAD 一致、上述真机配对基线。
