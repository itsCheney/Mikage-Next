# P2B：Perspective GPU 路径本地验收记录

日期：2026-10-06。范围：软件契约修复、Metal 实现、源码审查和 portable 回归；新增 P2B 的 Apple/GPU/真机验证待验。用户在开始本阶段前报告 P2A 测试暂未发现问题，未提供设备、OS、场景或新日志；该反馈不替代 P2B 验证。

## 版本与工作区

实施开始时三个仓库均干净，P2A 已提交：

| 仓库 | HEAD |
| --- | --- |
| 主仓库 | `4f7f6ea63add99aee1eb30897cec60471122db1d` |
| Runtime/host | `ff02d9c2fd1882cc1f42506bbd4d88ef5006acee` |
| Core | `9cb5b9bb8ac2e9556ea207042f556eae5d477add` |

本轮未提交或推送。保持 85 项 fixture、70 个注册名、70 个描述符、0 个注册描述符缺口、15 个未注册历史扩展及 Count=48。没有重复注册 `PerspectiveAlphaBlend_a`，它沿用共享 Alpha_a 对象。

## 软件契约与行为裁决

输入顺序为 LT、RT、LB、RB，每 quad 四点，源与目标数组逐 quad 前进四点。正向 homography 保留源点右/下 +1 约定，逆 3×3 矩阵计算 clip 相对的目标像素中心。非矩形 warp 采样完整 RGBA 源，源坐标可为小数、部分越界或反向；采样范围外及 `abs(denominator)<1e-12` 为透明，再与目标旧值混合。reference 被忽略。

新增 `LayerPerspectiveGeometry.h` 共用原 Gaussian 消元与原 3×3 求逆顺序，没有引入 OpenCV。软件 warp 与 GPU 准备消费同一 inverse；MSL 用双 float 坐标和补偿除法，最后转 float，再复用软件对应的 nearest/bilinear 字节函数和 `layerPixel()`。

先修复软件参照中的明确问题：

- 矩形把点 2（左下）用作右下导致宽度为零，改为点 3，以普通矩形 Copy 构造独立像素参照。
- 不相交矩形不再进入完整 clip warp；clip 在读写前与目标相交，区域外像素保持。
- 全批次预检求矩阵失败、晚 quad 非法及格式/数量/非有限点，避免读未初始化矩阵或写完前缀后才失败。
- 保留其它矩形/triangle 既有 StretchType clamp，只为 Perspective 记录原负值，防止 unsigned 比较把 -1 夹成 cubic。
- 非有限采样直接透明，避免 NaN 转整数坐标。

这些是软件参照修复，不宣称保持旧错误结果。合法非矩形的矩阵和字节运算顺序保持。

## GPU 域与事务

新通用 `OperateLayerPerspective()` 默认 false。支持单 RGBA 源与 RGBA 目标，Copy 与 P2A 的 19 个 Alpha/ConstAlpha/AdditiveAlpha/Ps kind，共 32 个名称/别名；其它方法仍软件。几何 bit=8 与 helper 声明能力，编号 0..47 不变。

每调用最多 256 quad，StretchType 0..2；backend 使用 nearest 或软件式 bilinear。超限、其它非负过滤器及有限但 inverse 系数绝对值超过 1e6 等有效域继续软件。正向矩形沿用整数 rect 和源裁剪；矩形错位/缩放自别名（包括 Copy）整批回退，保留扫描线次序；非矩形反射和自别名支持 GPU。

这是真正逆 homography compute，不是 affine 两三角形替代透视。旧目标先复制到 GPU scratch；逐 quad 从前序结果取目标旧值/别名源快照，完成 warp 后混合。全部编码成功才用一次 blit 提交写区域 union，间隙保持旧图。所有 false 都在真实目标写入前返回，失败后软件执行不会重放已写前缀。

复用 alpha/Ps 不可变表，alpha 缓存按 flags 保留或失效，RGB 与写区域更新。新增 `PerspectiveAlias=16` 经 C stats 尾部与 Swift 日志贯通，旧原因编号保持；宿主与 App 需一起重建。

首版 compute 使用整目标 scratch/自别名快照，不复用 tile pass；内存与 blit 成本待 P5 测量。没有新增每 quad/操作 submit 或 wait，仅在成功批次末检查既有预算；GPU 快照计入现有 snapshot 字节，参数与图像传输计数分开。

## 实际验证

Windows，Layer 为同步 device double，nativeCompiled=false，两套构建成功。

| 检查 | 最终结果 |
| --- | --- |
| MetalLayer CTest | **2/2 pass，0 skip；78,622 次精确表面比较、3 session** |
| MetalRenderBackend CTest | **1/1 pass，0 skip**；软件 transfer/diagnostic，不编译 Metal |
| P2B 每 session | 6,912 单 quad、30 混合批次、3 个独立软件矩形参照、16 非法晚 quad、9 软件回退、6 COW/cache 场景及资源/empty/排队检查，共 **6,991 次表面比较** |
| 生产透视坐标 | 每 session **1,300 精确 float 坐标**，另计；零/近零分母与独立 nearest pole 像素参照 |
| 原 scalar | UnivTrans 3,145,728、既有 blend/conversion 1,835,008、P1A 21,757,036、P1B 46,137,346 精确像素保持 |
| 原 P2A | 主矩阵、边界、回退、COW/cache 与 24 操作排队批次保持通过 |

覆盖强透视/反射、小数/越界源、目标裁剪、混合 rect/warp 多 quad 与别名顺序、lease/COW/alpha 缓存、资源/default backend/非法 kind/flags/opacity/map、257 quad 回退。20 次两 quad 调用切换两目标、clip、源尺寸及 alias，中途无读回；native 额外检查提交预算和零 wait。

中段失败注入直接读取物理 backend 目标和独立源，验证初始字节未变，避免 CPU cache 掩盖部分提交。Native 显式排除 double 失败注入。首轮发现 Double 临时帧污染全局软件过滤器，已修复替身；生产回退未为此更改，保留失败日志，精确容差未放宽。

Portable double 执行生产坐标/采样 helper，再用初始化的软件方法混合帧，证明数学、路由、驻留和生命周期；不执行新 MSL，不证明真实 GPU 顺序、吞吐或功耗。

## 证据与待验

- [修改前 Layer](native-metal-p2b-baseline/metal-layer-before.log) / [backend](native-metal-p2b-baseline/metal-render-before.log)：既有构建目录实际重跑，未先重建。
- [首次失败](native-metal-p2b-baseline/metal-layer-first-failure.log) / [最终 Layer](native-metal-p2b-baseline/metal-layer-after.log) / [最终 backend](native-metal-p2b-baseline/metal-render-after.log)。
- [Layer 构建](native-metal-p2b-baseline/metal-layer-build.log) / [backend 构建](native-metal-p2b-baseline/metal-render-build.log)。
- [能力 JSON](native-metal-p2b-baseline/capabilities.json) / [HEAD、命令、平台与哈希](native-metal-p2b-baseline/metadata.json)。

三层原始 patch 在被忽略的 `build/native-metal-p2b-baseline/{main,runtime,core}.patch`，包含新增文件、排除 metadata 自身；只做 reverse-check，不修改 Git。CTest 由 PowerShell 保存。

待验：新 P2B macOS Objective-C++/生产 MSL、默认 tile 与强制 compute 整套原生回归、真实批次快照/顺序/提交及资源失败；iOS device/simulator framework/App 重建；新真机画面、透明/输入、生命周期、内存、帧时间和热状态。旧 CI/P2A 反馈不替代新增 P2B 验收。P3 通用三角形与 P4/P5 未实施。

## Apple CI 编译补修：2026-10-06

用户提供 Xcode 26.6 / AppleClang 21 的 `Build and test native Metal backend` 日志：`fullTarget(id<MTLTexture>&)` 参数被 ARC 推导为 autoreleasing，与三个 strong 缓存成员不匹配，产生三个编译错误。现显式声明 `id<MTLTexture> __strong&`，匹配缓存成员所有权；分配、复用、失败事务和渲染算法不变。`fastMathEnabled` 的弃用警告不是此次失败原因。

补修开始时三层 HEAD 为主仓库 `83c343b9ad2c4a0af74f36a3778c496b822703f3`、runtime `4bb67e1f46e8d7b86940b440efd23119ee28727f`、core `105a7be86b600bb251d3c846c17eff2b51eed92e`，工作区干净；补修尚未提交或推送。用户日志未包含 run URL/完整 SHA，不能据此绑定原生测试结果到新修复。

同类引用检查只找到这一处。Windows 两套 CTest 回归通过（2/2、1/1）；日志位于本地忽略目录 `build/p2b-arc-layer-tests.log` 与 `build/p2b-arc-backend-tests.log`。Windows 不编译 Objective-C++，这些回归不验证 ARC 修复。尝试配置的 Mac 只读验证时，SSH 连接被关闭；Apple 完整构建和 MSL/GPU 验证仍待后续 CI。
