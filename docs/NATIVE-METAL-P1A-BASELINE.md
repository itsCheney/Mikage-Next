# 原生 Metal P1A 实施与基线

日期：2026-10-05（Asia/Shanghai）。P1A 的 13 项矩形实现及本地 portable 验证完成；本轮原生 Metal、iOS 与真机回归待验。用户报告 P0 真机暂未发现异常，该反馈不替代 P1A 新代码的设备结果。

## 实现与兼容行为

普通 Sub/Mul/HDA/ColorDodge/Darken/Lighten/Screen、R8 RemoveOpacity、反预乘、Gamma/Gamma_a、单源 AlphaBlend_SD 及双源 ConstAlphaBlend_SD_a 均已接入描述符、manager、shared pixel helper 和 backend。注册仍为 70 名，映射从 46 增至 **59**；剩余 **11** 个 Ps 方法和 15 个未注册历史扩展没有新增支持。

operation ID 0–26 保持，追加 27–36，Count=37。Gamma 两种方法共享 kind、以 premultiplied flag 区分；SD_a 复用双源 kind，两个 Mul 方法保留独立对象和 flags。当前真实 Mul 绑定均保留目标 alpha，P1A 不人为制造不同结果。

**经本会话确认的软件修复：** Gamma_a 在 `alpha=channel ∈ {4,8,16,32,64,128}` 时，`recip*channel>>8` 原先等于 256。软件的 R 表访问越出整个 768-byte temp，G/B 表访问跨入下一张表。三个通道现统一将索引限制到 255；MSL 使用同一规则。独立回归验证各自 LUT[255] 端点，不仅与修复后的软件互比。这是明确的兼容行为修复，未尝试复现越界读到的随机数据。

Gamma 使用软件生成的 B/G/R 三张 LUT；低字节仍查 R，中字节查 G，高字节查 B。普通 Gamma 对 alpha=0 保持完整像素，两个方法都保留 alpha。默认零 LUT 保持。`SetParameterPtr` 完成 LUT 生成和新快照分配后才提交 temp/version/owner；相同内容复用 owner，短期用户指针不被保留。

Metal 的两个最近版本缓存只创建不可变 buffer，不重写已编码的旧版本；tile、in-place 和 snapshot compute 都在 buffer(2) 绑定，非 Gamma 用既有 alpha buffer 占位。参数上传次数/字节经 backend→session→C bridge→Swift 日志独立记录，图像 `uploadedBytes` 含义保持。缺失 LUT/分配失败在目标写入前拒绝，没有新增每算子 submit/wait。

反预乘整数公式与 TVPDivTable 精确等价，无新增 64 KiB 表上传，保持 ApplySelf/reference/COW。单源 SD 的输出 alpha=0；双源 SD_a 按 input0/input1 顺序对完整 ARGB 插值，opacity=255 也按 denominator=256。普通混合与 SD 的错位目标别名继续保留顺序相关软件回退。

RemoveOpacity 的 GPU 域是 R8 源、RGBA 目标、正向同尺寸且完整合法 ROI。缩放、镜像、越界在写入前明确拒绝；不会进入按 RGBA 地址处理 R8 的不安全软件 resize。合法形状的 CPU pin/lease、不可用 backend 等保留原软件回退。

新增普通混合、单源 SD 与反预乘也要求正向源矩形：其软件 wrapper 对反向源没有可靠参照，manager 与 backend 在写入前拒绝。新方法的非 RGBA 目标/不兼容输入同样不进入 32-bit 软件访问；RemoveOpacity 的已有 RGBA byte-input 软件回退保持，其他不兼容格式明确拒绝。镜像及 R8→RGBA 混合错误格式均有不改变目标/不产生传输的回归。

## 实际验证

| 检查 | 本轮结果 |
| --- | --- |
| MetalLayer Release build / CTest | 构建通过，**2/2 pass，0 skip**；5,026 次主 surface 比较、3 个 session |
| P1A 生产 MSL helper 提取为 C++ | **21,757,036 精确像素**，对照初始化后的实际软件指针 |
| 原 UnivTrans / extended blend scalar | **3,145,728 / 1,835,008** 精确像素继续通过，未放宽容差 |
| MetalRenderBackend build / CTest | 构建通过，**1/1 pass，0 skip**；Windows 仅 diagnostics/software transfer，native Metal 未编译 |
| 能力审计 | **85 / 70 / 59 / 11 / 15**，对象共享/独立及只读参数快照通过 |

新增测试覆盖 mask 的全部 256³ alpha/mask/opacity 组合，普通混合完整通道对与 opacity/alpha 边界，反预乘全部 alpha/channel，Gamma 不同通道/上下限/透明度与越界修复，SD 输入顺序和取整。

manager/device-double 测试保持驻留输入零 fallback/像素读回/图像重上传，验证 ROI 外像素、别名顺序、R8 拒绝和资源故障回退。Gamma A/A/B/A 与保留旧 A 在 pending work 中拥有独立数据；重复同版本无参数上传，变更版本仅计必要 768-byte 上传。测试还使用真实 bitmap wrapper/GetTextureForRender 的 COW、原始快照保留，以及 Gamma CPU pin/lease 回退。

缩放/裁剪新增用例使用统一源，以分开验证取样路由和整数混合；这不等于对任意图像线性取样差异作原生 GPU 容差裁决。现有取样实现与既有测试保留。

执行平台：Windows，MinGW GCC 13.2.0，C++17，MetalLayer Release。`nativeCompiled=false, backendAvailable=true, deviceDouble=true` 表示设备替身成功绑定；上述数字没有证明生产 Objective-C++/MSL 编译或真实纹理执行，也不是游戏 FPS、功耗或热状态结果。

## 版本与证据

| 仓库 | P1A 开始时 HEAD |
| --- | --- |
| main | `5c349988f3ad5d74a12b6245365f25293996f0a7` |
| runtime/host | `3c71a4495ecb04e21e1c87cc33f69ee3dc5b324e` |
| core | `36d7a88bcbece02f75ae5a782334145f1cf8a486` |

P1A 为这些 HEAD 上的未提交改动，三个层次都有相关源文件修改。机器记录保存 [版本、命令、SHA-256 与 patch 检查](native-metal-p1a-baseline/metadata.json)；完整原始字节差异快照保存在本地 `build/native-metal-p1a-baseline`，包括新测试/文档文件。patch 执行 `git apply --reverse --check`，只验证、不修改工作区。

- [修改前 Layer 输出](native-metal-p1a-baseline/before-metal-layer.log) / [修改前 backend 输出](native-metal-p1a-baseline/before-metal-render.log)：保存已有 P0 日志；原日志不绑定三层 HEAD，不冒充新执行。
- [修改后 Layer 输出](native-metal-p1a-baseline/after-metal-layer.log) / [修改后 backend 输出](native-metal-p1a-baseline/after-metal-render.log)：本轮实际执行日志，保留字节和平台说明。
- [能力 JSON](native-metal-p1a-baseline/capabilities.json)：逐项名称、canonical、kind/flags、参数、几何/alias/alpha、Gamma snapshot 与 backend 状态。

实际命令（仓库根目录）：

```text
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel 2
ctest --test-dir build/metal-layer-tests --output-on-failure
build/metal-layer-tests/metal-layer-tests.exe --audit-capabilities
cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel 2
ctest --test-dir build/metal-render-tests --output-on-failure --verbose
```

## 待验项

- macOS 生产 Metal 默认 tile、强制 compute，以及 snapshot compute：MSL 编译、真实像素/纹理绑定、版本资源保留与提交/等待预算。
- iOS device/simulator、框架、App/IPA 与 Swift/C 统计桥完整构建。C stats 追加字段，需与更新后的宿主/App 一起重建。
- 新真机同场景回归，重点 Gamma 效果、R8 mask、SD、透明输入、长时间运行、内存和热状态。

本地代码与 portable 签收完成；未执行的 Apple 和设备结果继续明确保留，P0 真机反馈不替代 P1A 验证。
