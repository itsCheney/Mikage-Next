# 原生 Metal P0 实施与基线

日期：2026-10-05（Asia/Shanghai）。按本会话确认的验收范围，P0 本地代码与 portable 验证完成；Apple 和真机验证保留为待验项。

## 代码版本与结果

| 仓库 | 基线 HEAD | 本轮状态 |
| --- | --- | --- |
| vn-sim | `1d0a4358fda835926beda6247ecd46054bd91dc8` | 测试、fixture 与文档改动未提交 |
| Runtime/host | `e0bb78a91fa0549a1588d751334ef7b01e68b595` | host 源码无改动；嵌套 core 工作区有改动 |
| Core | `49bf9f70fb4866515a26e6dd9a7aa89fa2447083` | P0 生产代码改动未提交 |

HEAD 仅标识基线，不能独自重现本轮未提交代码。[机器记录](native-metal-p0-baseline/metadata.json)保存平台、命令、结果、文件 SHA-256 和本地 patch 位置。完整工作区差异快照保存在被忽略的 `build/native-metal-p0-baseline`，包括新文件 patch；这是本地验证证据，不是已发布的 commit。

- MetalLayer：Release 构建成功，CTest **2/2 通过，0 skip**。主测试完成 4,627 次 surface 比较、3 次 session；新增的一次比较验证非法入口和审计不改变驻留目标。
- 生产 MSL scalar helper 提取为 C++：UnivTrans **3,145,728** 个精确像素，其他混合/转换/通道 **1,835,008** 个精确像素，数量与修改前相同，未放宽容差。
- MetalRenderBackend：构建成功，CTest **1/1 通过，0 skip**；在 Windows 仅编译 diagnostics 与 software transfer 检查，native Metal 未编译/未测试。
- [能力 JSON](native-metal-p0-baseline/capabilities.json)：85 项清单、70 项注册、46 项描述符、24 项软件缺口、15 项未注册；六对共享别名及独立 SD_a 检查通过。`nativeCompiled=false, backendAvailable=true, deviceDouble=true` 表示设备替身可绑定，不表示实际 GPU 可用。

本轮在 Windows/MinGW GCC C++17 上运行。portable 设备替身验证生产 manager 的路由、COW、缓存、租约、别名和计数；scalar 提取验证对应整数 helper。它们都不能证明 Objective-C++/MSL 的实际编译或真实 Metal 纹理执行。

## 契约与安全回归

共享 `.def` 保持 operation ID 0–26；enum、Count、traits、MSL 具名常量与诊断数组来自同一来源。普通、in-place、tile shader 均使用同源前缀；独立 fixture 追加 TestKind=27/Count=28，验证源依赖、traits、MSL 与统计尾项，测试 kind 不进入生产渲染。

审计查询与枚举无弹框、不注册、不设置参数；重复 JSON 稳定，方法对象/名称/描述符参数、渲染计数与驻留像素不变。GPU 描述符仍只是受约束的语义元数据；JSON 将名称/描述符、参数限制与 backend 状态分别记录。24 项软件缺口的输入、绑定和 reference/alpha 规则单独维护。

合法 manager 调用的路由、像素公式、拒绝顺序和 alpha-table 初始化保持。转换从 reference 读源及 COW alpha、HDA 与 `_d/_a`、错位 alias、blur 与 affine 的既有测试继续通过。安全性新增两类失败：Unsupported/Count/越界 kind 编码前拒绝，以及 SD/UnivTrans 误入单源 backend 入口时拒绝；均不改变目标、不新增提交/等待。正确的双源、三源入口仍通过原有回归。

动态编译失败会记录名称/backend 并返回 null，不注册空对象；重复失败、带 hash hint 的重试、Release 空注册以及自定义编译器成功/缓存路径均有测试。动态 GLSL 功能没有新增。

## 执行与原始输出

实际执行命令（仓库根目录）：

```text
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel 2
ctest --test-dir build/metal-layer-tests --output-on-failure
build/metal-layer-tests/metal-layer-tests.exe --audit-capabilities

cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel 2
ctest --test-dir build/metal-render-tests --output-on-failure --verbose
```

保留原始文件字节，不修改其时间头或平台说明：

| 证据 | 来源与限制 |
| --- | --- |
| [修改前 Layer](native-metal-p0-baseline/before-metal-layer.log) | 10-05 14:30，4,626 comparisons/3 sessions；原日志未绑定源码 HEAD，不能冒充此次新执行 |
| [修改前 backend](native-metal-p0-baseline/before-metal-render.log) | 10-04 22:30，diagnostics/software 通过；原日志未绑定源码 HEAD |
| [修改后 Layer](native-metal-p0-baseline/after-metal-layer.log) | 本轮实际 Release CTest，包括 operation、scalar、路由与能力审计 |
| [修改后 backend](native-metal-p0-baseline/after-metal-render.log) | 本轮实际 CTest；输出明确 native Metal 未测试 |

## 历史真机证据与待验项

保存引用既有 [第三批真机汇总](third-round-layer-log-summary.json)及[分析](THIRD-ROUND-LAYER-PERFORMANCE.md)：来源应用 revision 为 `fd6f50fc44b0`，三个附件文件名、SHA-256 与 4,047 条记录数量可核对。设备/OS和与当前三层 HEAD 的对应关系并不完整，数据发生在此前修复之前，不是当前提交的基线。P0 未重新运行游戏，也没有测量新 p50/p95/p99、功耗或热状态改善。

- 待验：macOS 默认 tile、强制 compute 的生产 MSL 编译、纹理绑定、真实像素及顺序；无设备/返回 77 要保留 skip。
- 待验：iOS device 与 simulator 框架、App/IPA 构建以及最新 CI 结果。
- 待验：同设备、同场景、同设置的新 iPhone/iPad 正确性、帧时间、输入、内存和热状态基线。

以上待验项不影响本会话约定的 P0 本地签收，也不能被本地 pass 或历史摘要替代。
