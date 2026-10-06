# P2A：affine Layer 混合本地验收记录

日期：2026-10-06。范围：本地实现、源码审查、portable 回归；新 P2A 的 Apple/GPU/真机结果待验。用户在开始本阶段前报告已有版本测试暂未发现异常，未提供设备、场景、OS 或新基线日志；该反馈不替代本阶段新增代码的设备验证。

## 版本与工作区

实施开始时三层工作区均干净：

| 仓库 | HEAD |
| --- | --- |
| 主仓库 | `0045cca1482b06160f929085849b10c827fb32b1` |
| Runtime/host | `72d930b8c7534b6dfc4a8157f96fd36a76709900` |
| Core | `703be0c16d131075e26b23225d09d5f5dd9b8abd` |

本轮未提交或推送。主仓库负责测试、Swift 诊断和文档，host 追加 C 统计字段，core 负责契约、manager 和 Metal 实现。没有新增注册名或 kind；70 个注册名、70 个描述符、0 个软件独占描述符缺口、15 个未注册历史名称及 Count=48 保持。

## 支持域与语义

在原 Copy 快路之外，19 个 kind 接入 affine 混合：Alpha、ConstAlpha、AdditiveAlpha，以及 PsMul/Overlay/HardLight/Screen/ColorDodge5 和 P1B 的 11 个 Ps kind。30 个实际方法/别名参与矩阵测试；`PerspectiveAlphaBlend_a` 的共享对象可以描述 affine 能力，但 `OperatePerspective()` 仍回退。

新几何 bit 与 `TVPLayerOperationSupportsAffine()` 明确方法/flags/opacity 契约；0–47 的稳定编号、Count、MSL 展开和原矩形几何 bit 保持。Copy 仍要求 flags=0；混合接受既有 HOLD_ALPHA/DEST_ALPHA/DEST_PREMULTIPLIED/FULL_OPACITY_BRANCH 位和 opacity 0..255。

非矩形路径继续使用 `layer_affine::Prepare()` 的逆映射、双 float 坐标和软件 bilinear 字节取整。源 ROI 必须是正向、整数、合法矩形，目标为已裁剪合法 clip，两三角形组成满足软件 affine 分支的平行四边形。采样与混合分开：新 kernel 复用 `layerPixel()`，读取目标旧值，在完整 clip 上处理采样结果，包含透明 warp 边界。软件 `OperateTriangles()` 的 warp 分支调用 `DoRender(target, clip, target, clip, tmp)`，因此 reference 被忽略，包括 COW 后旧图 reference。

软件先完成 warp 再混合；非矩形源目标别名用 GPU 源快照保留此行为，目标旧值另存 clip 快照。Alpha `_d` 使用实际 alpha tables，SoftLight/Dodge/Burn 复用 P1B 的真实软件三表。资源、格式、非法 kind/flags/opacity、采样和逆映射预检在写目标前完成；缺资源和编码失败保留软件回退。HDA 用共用契约保留 alpha cache，`_d/_a` 仍失效；RGB 和 clip 区域随 GPU 操作失效。

矩形特判复用已有普通矩形后端与软件的整数源裁剪。Copy 的原 kernel 和别名快照行为保持；混合只接受同像素矩形自别名，错位或缩放自混合保留软件扫描线次序。

| 保留回退的最小反例 | 原因 |
| --- | --- |
| StretchType=3 的合法 quad | affine 限 0..2；普通矩形的更宽过滤器域不沿用到 affine |
| 源坐标 `(1.25,1)` 开始的 quad | 非整数源 ROI，不满足 Prepare |
| 平行四边形右下顶点单独偏移 1 | 软件选 perspective 分支，属于 P2B |
| 轴向目标从 x=16 映到 x=2 | 软件使用固定点镜像扫描线 |
| 源 ROI x=1 开始、自混合目标 x=2 开始 | 矩形扫描线重叠；新增 `AffineAlias` 拒绝计数 |
| CPU pin/lease 或 backend 编码失败 | 既有 CPU/资源域，精确回退与区域外像素保持 |

source/target 不可用、格式、方法/未支持 kind、采样、几何、opacity、alpha/PS 表、backend 和 alias 各有具体拒绝原因；triangle 总回退计数仍保留。`AffineAlias=15` 追加在现有原因之后，C stats 尾部新增字段，并经 Swift 诊断输出；宿主和 App 需一起重建。

## 执行路径与性能边界

非矩形 affine 混合首版使用 compute 与 GPU 快照。它关闭已有同目标 tile pass，之后普通矩形仍可恢复 tile/compute 复用；未实现 affine tile fragment。直接复用 tile 需要在 fragment 中验证同一逆映射、透明边界及 alias 次序，当前选择 compute 作为正确性路径。这一限制已明确保留给后续原生测量和 P5 优化。

没有新增每算子 submit/wait。目标/别名快照计入既有 GPU snapshot 字节，参数表上传与图像上传继续分开。支持域要求 warmed 图像无 CPU fallback/readback/reupload；用于像素比较的最终读回另计。

## 本地验证

执行平台为 Windows，Layer 使用同步 device double，nativeCompiled=false。两套构建成功，CTest 的 MetalLayer **2/2**、MetalRenderBackend **1/1** 通过，**0 skip**；Layer 共 **57,649 次精确比较、3 个 session**。原 Copy 369 个精确用例/每 session 与 full-HD 驻留 burst 保持通过。

P2A 每 session 包含 17,280 个方法/opacity/采样/几何/ROI/clip/alias 精确比较，72 个单像素/单行/单列边界比较，9 个回退比较，9 个 COW/cache 场景，以及 alpha/PS 表失败注入。连续 24 操作批次覆盖目标切换、clip/crop 大小改变、16 次源目标别名和快照复用，GPU 中途无读回，最后再比较；native 运行时额外检查无 wait、至多一次正常预算提交。

原 scalar 精确像素仍为 UnivTrans 3,145,728、既有 blend/conversion 1,835,008、P1A 21,757,036、P1B 46,137,346，容差未放宽。非法 kind/flags/opacity/sampling、NaN/infinity/巨大/退化逆映射和非法 clip/crop 在 backend 修改目标前拒绝。Release 检查仍有效。

Portable double 执行提取的生产坐标/字节 helper，再用初始化后的软件方法混合采样帧；这证明 manager 的采样、路由、驻留和生命周期契约。旧 scalar 测试独立验证共享像素 helper；Windows 不执行新增生产 MSL kernel、Objective-C++、真实 GPU 纹理绑定或命令顺序。

## 证据与待验项

- [修改前 Layer](native-metal-p2a-baseline/metal-layer-before.log) / [backend](native-metal-p2a-baseline/metal-render-before.log)。
- [修改后 Layer](native-metal-p2a-baseline/metal-layer-after.log) / [backend](native-metal-p2a-baseline/metal-render-after.log)。
- [Layer 构建](native-metal-p2a-baseline/metal-layer-build.log) / [backend 构建](native-metal-p2a-baseline/metal-render-build.log)。
- [能力 JSON](native-metal-p2a-baseline/capabilities.json) / [机器记录、三层 HEAD 和文件哈希](native-metal-p2a-baseline/metadata.json)。

Git 原始字节 patch 保存在被忽略的 `build/native-metal-p2a-baseline/{main,runtime,core}.patch`，含新增文件，排除 metadata 自身，全部只做 reverse-check。CTest 日志由 PowerShell 保存，编码由文件标记识别；命令完整记录在 metadata 中。修改前日志来自既有构建目录的实际重跑，未先重建；修改后输出来自本轮重建后的代码。

待验：新 P2A macOS Objective-C++/MSL、默认 tile 与强制 compute 整套回归、真实快照复用/顺序/提交计数，iOS device/simulator framework/App 重建，以及新真机画面、透明/输入、换游戏/前后台、内存、稳定帧时间和热状态。此前提交的 CI 或用户反馈不能作为新增 P2A 的原生像素/性能基线。P2B perspective、P3 通用三角形及 P4/P5 尚未实施。
