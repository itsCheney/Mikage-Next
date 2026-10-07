# 原生 Metal 兼容性补齐计划：对照 Kirikiroid2 OpenGL 的源码审计

日期：2026-10-05；更新：2026-10-07。状态：**P0 已验收；P1A/P1B/P2A/P2B 已提交；P2C C0 的 origin 聚合、逐帧基线接口和 App 构建 HEAD 已完成本地实现与 portable 验证，新 Apple/真机对照待验；C1–C4、P3–P5 尚未实施。**

## 1. 决策与完成目标

继续以原生 Metal 为正式后端。Kirikiroid2 的 OpenGL RenderManager 用来发现遗漏的能力、理解成熟的调用语义和构造兼容性样例；当前软件实现提供可执行的像素参照。遇到两者不一致时，先裁决行为，再实现 GPU 路径。

本计划的完成目标分为三个层次，不能以一个“覆盖率”代替：

1. **当前引擎的普通 Layer 能力**：补齐当前已注册软件方法的 Metal 路由及有效参数域，扩大 affine/perspective 的 GPU 支持，减少可避免的软件回退和像素传输。
2. **旧 GL 的额外兼容能力**：对当前未注册的染色、AlphaTest 等方法明确支持契约；只在确认调用方需求后接入。动态 GLSL 不纳入本轮默认承诺。
3. **真机性能与稳定性**：在同一设备、游戏、场景和设置下验证画面、输入、帧耗时、GPU 阶段、内存和长时间运行。GPU 算子覆盖增加本身不能证明持续卡顿或发热消失。

特别地，`layerCPUFallbacks=0`、GPU 算子 reject 为零、以及像素正确，都不能推出没有 CPU↔GPU 传输：上层仍可主动取得 scanline、lock 或 raw pointer。本计划将“扩大 GPU 算子支持域”与“消除已证实的 CPU 像素边界”分别验收，不能用其中一项替代另一项。


## 2. 审计基线与证据边界

### 2.1 固定版本

| 对象 | 本轮核实的版本 | 用途 |
| --- | --- | --- |
| 主仓库 `vn-sim` | `1d0a4358fda835926beda6247ecd46054bd91dc8` | 测试、诊断文档、iOS CI |
| Runtime/host 子仓库 | `e0bb78a91fa0549a1588d751334ef7b01e68b595` | 构建与宿主边界 |
| Core 子仓库 | `49bf9f70fb4866515a26e6dd9a7aa89fa2447083` | 本地 Metal、软件、GL 接口 |
| Kirikiroid2 | `d1c2b1259423542c893e0b65eaeb46c848848f2b` | 旧 GL 实现、接口和测试代码 |

审计开始时，主仓库及两个嵌套仓库的工作区检查均无改动。本轮只新增本计划；参考源文件下载到被忽略的 `build/kirikiroid-reference`，没有导入运行时代码。

旧 `RenderManager_ogl.cpp` 原始文件 SHA-256：`9d9a674b88b8136ca796524878dd548714f2c37d0afd7ca375b0f588606ebeff`。按文本行分割为 4,032 行；行数差异不作为能力判断依据。

### 2.2 核对方式

- 读取当前生产源码，而非仅依据文档或上一段对话。
- 下载固定提交的旧 GL `.cpp`、旧 `RenderManager.h`、`RenderManager_ogl_test.hpp`，核对注册、别名、几何、回读和测试容差。
- 静态提取旧文件中 `CompileRenderMethod`、`CompileAndRegScript`、`CompileAndRegRegularBlendMethod`、`RegisterRenderMethod` 的字面名称；去除注释、去重并合并条件分支。
- 展开当前软件注册的 `REGISER_BLEND_*` 宏，核对共享方法对象及 `ConfigureGpuOperation()` 的首次配置规则。
- 阅读现有测试实现和 CI 配置；**本轮没有重新运行 CTest、Apple 构建、旧 GL 程序或 iPhone 场景**。源码中存在测试，不等于当前提交在真实设备上已通过。
- 注册名称统计只代表名称与语义描述符的静态关系；不能代表所有输入、几何、格式和设备都能 GPU 执行。

### 2.3 主要源码索引

下文证据编号均指向固定提交；本地相同文件可用于后续修改。

| 编号 | 源码与范围 |
| --- | --- |
| K1 | [旧 GL 方法注册、混合、转换和 blur][k-register] |
| K2 | [旧 GL scanline 回读][k-readback] |
| K3 | [旧 GL target-as-source/framebuffer fetch][k-fetch] |
| K4 | [旧 GL 三角形、透视、stencil][k-geometry] |
| K5 | [旧 RenderManager 接口][k-interface]、[旧 shader 测试][k-tests] |
| C1 | [当前方法注册与 GPU mapping][c-mapping] |
| C2 | [当前 Layer manager 路由及回退][c-manager] |
| C3 | [当前 Metal 资源与算子实现][c-metal] |
| C4 | [当前 MSL 像素、tile、blur、transition、affine 实现][c-shaders] |
| C5 | [当前软件几何实现][c-software-geometry]、[图像数学与 blur 工具][c-imageutils] |
| C6 | [当前操作结构和统计枚举][c-operation]、[后端接口][c-interface] |
| C7 | [当前 RenderManager 接口和参数机制][c-render-interface] |
| C8 | [软件函数绑定][c-blend-bindings]、[tvpgl 实现][c-tvpgl] |
| T1 | [普通 Layer 测试][t-layer]、[测试构建配置][t-layer-cmake] |
| T2 | [Metal backend 测试][t-backend]、[iOS CI][t-ci] |
| D1 | [第三批性能核对与已实施优化][d-third]、[此前 Layer 性能跟进][d-followup] |

### 2.4 2026-10-06 诊断样本：P2C 的证据与边界

以下是上传诊断日志的**现象基线**，不是对当前工作区或所有游戏的性能承诺。六个横向样本均标记为 `sourceRevision=6e5cbf10e7dd`；实施前必须在当时的三层 HEAD、同设备、同场景重新采样。它们的共同结果是 `layerCPUFallbacks=0`、GPU reject=0，因此这里的传输不是普通 Layer Metal 算子被拒绝后的 software fallback。

- 一个 transition 场景累计 upload 1,176.45 MiB、readback 171.45 MiB；其中 `transition.outputOverwrite` 为 133 次/1,052.05 MiB（89.43% upload），`transition.source` 为 16 次/158.20 MiB（92.28% readback）。该采样还记录 26 次同步等待、430.63 ms。它对应传统 `DivisibleTransHandler` 经 scanline 在 CPU 处理后整图写回的路径；现有 overwrite 优化避免了旧目标 readback，但不能避免每帧整图 upload。
- 六个不同游戏样本没有 `transition.source` / `transition.outputOverwrite`，故 transition 是严重但触发相关的路径，不能被用来解释所有游戏的卡顿。`shrinkCopy`/同类 lock readback 则更普遍：DRACU-RIOT! 为 155 次、5.74 MiB、271.4 ms wait；天使纷扰为 78 次、21.95 MiB、486.5 ms wait；9-nine 为 12 次、3.94 MiB、40.2 ms wait。小至约 13 KiB 的读取也观察到 10–40 ms 等待，优化目标不能只按 MiB 排序。
- 天使纷扰记录了同一张 1052×900 texture 的 `layerExDraw.write` 与 `layerExBase` 两次完整 GPU→CPU→GPU 往返，单轮约 14.45 MiB；其中 `layerExBase` readback 的 GPU wait 约 40.92 ms。这是 raw-pixel API 与 GPU-authoritative texture 交替使用的可复现样例，不应笼统归为“插件少量读回”。
- 六个样本还出现 7,304–32,131 次 `bitmap.update`。常见单次约 500 bytes，但部分样本达到约 4,650 次/s，且与 `blitEncoders` 高度相关。这是 encoder/命令编码 churn 的强信号，不足以单凭相关性断言调用链；P2C 必须先补齐 origin 与调用点证据，再做批处理。

诊断现有的 texture-ID top-N 输出会把多个来源折叠成 `read:other`/`upload:other`。例如天使纷扰中 `read:other` 为 67 次/4.58 MiB/289.2 ms wait；在按 `layerReadbackBySource` 的统计中，同一采样却显示 71 次 lock readback。故 P2C 的第一个可交付物是有界的按 origin 聚合，不能在 `other` 仍吞没 stall 来源时直接归因或设性能目标。

## 3. 对原对话结论的逐项核实

下表保留初始固定版本审计结论；P1B 实施后的最新数量见第 4.1 节，实际输出见 [P1B 记录](NATIVE-METAL-P1B-BASELINE.md)，Gamma 兼容修复见 [P1A 记录](NATIVE-METAL-P1A-BASELINE.md)。

| 原结论 | 核实结果 | 对实施的影响 |
| --- | --- | --- |
| 当前 GL 未接普通 Layer GPU 管线 | **成立**。基类 `SupportsLayerOperations()` 为 false，GL 未覆盖这一组接口，Metal 已覆盖。C2/C6 | 不把“先接 ANGLE”作为现有算子缺口的解决办法 |
| 当前有 41 条 GPU 映射 | **成立**，是 C1 的显式注册映射条目数 | 41 不等于独立 shader 数或完整支持的方法数 |
| 旧 GL 约 74 个名称、当前直接覆盖约 34 个、缺约 40 个 | **口径不完整**。本轮所有四类注册入口合计 77 个去重名称，与显式 mapping 的交集为 37；再考虑当前共享对象别名，为 38 | 使用第 4 节和附录的逐项清单，不按旧数字排工期 |
| Perspective 当前总是软件回退 | **成立**。C2 的 `OperatePerspective()` 无 GPU 分支 | 必须新增几何执行能力；不能只补方法名 |
| Triangles 仅有限 Copy 快路 | **成立，但条件更多**：count=2、单输入、Copy、flags=0、stretch 0..2，并受 Prepare、格式、裁剪等限制 | 优先扩大现有 affine 语义域，再考虑通用三角形 |
| 旧 GL 有通用三角形和透视 | **实现存在**。K4 提交三角形，透视计算 homography | 不代表所有组合正确；旧透视多 quad 调用还有静态风险，见第 5 节 |
| 旧 GL 可整份直接移植 | **不宜如此**。有旧缓存、纹理缩放、Cocos/OpenCV、GL 扩展和资源管理依赖 | 复用语义与样例，保留当前资源架构 |
| 旧 GL 一个 scanline 可能触发整纹理回读 | **成立**。缓存缺失时按 internalW×internalH 回读，`PixelDataCounter=5`，另有过期路径。K2 | 不恢复旧回读策略 |
| 旧 GL blur 是 9 个采样点 | **成立**。中心点加 8 个偏移，除以 9；偏移可较大，并非任意大核的逐点平均。K1 | 不用它替换当前 sliding-sum blur |
| 当前 BoxBlurAlpha 已是普遍意义上“更正确”的 alpha-aware blur | **表述过强**。当前两个软件方法实际都进入逐 RGBA 字节平均，Metal 跟随这一行为。C4/C5 | 区分“匹配当前软件”与“符合原始 Kirikiri 历史语义”，另立行为裁决 |
| target-as-source 都可以 snapshot/ping-pong | **只对明确定义为旧图快照的操作成立**。C2 对若干错位自混合保留顺序相关软件语义 | 不能删掉 alias 拒绝条件来制造零回退 |
| Metal 需要以后再做 framebuffer fetch/tile 优化 | **已过时**。C3/C4 已有 programmable blending、ROG、同目标 render pass 复用和 compute 回退 | 扩展现有像素函数；验收两条实际路径 |
| GPU stage timing 仍未实现 | **对当前源码不成立**。已有 stage-boundary 采样与 `metal.gpuStages`，是否设备可用需运行验证 | 使用现有诊断，不重复实现，不把 mixed command 时间按次数分摊 |
| 旧新 RenderManager 核心接口基本一致 | **成立于接口形状**，不是实现能力等价。C7 有动态脚本编译返回空、stencil/manual target 默认空实现 | 额外能力需单独审计调用方和错误处理 |

## 4. 能力账本：哪些真的缺失

### 4.1 数量关系

| 范围 | 数量 | 解释 |
| --- | ---: | --- |
| 当前软件层注册名称 | 70 | 展开注册宏后去重 |
| 当前显式 GPU mapping 条目 | 65 | P1A/P1B 新增 24 条；包含部分共享对象的重复名称 |
| 当前有 GPU 描述符的名称 | 70 | 65 条显式映射，加 5 个共享对象别名；均仍有执行限制 |
| 当前软件存在、但无 GPU 描述符 | **0** | P1B 11 项矩形描述符已补齐；不代表全几何 GPU 支持 |
| 旧 GL 注册名称 | 77 | 四类注册入口合并、去重；包括条件分支的名称集合 |
| 旧 GL 名称在当前有 GPU 描述符 | 62 | P1A/P1B 新增 24 项；P2B 已接入 `PerspectiveAlphaBlend_a` 的受限透视执行 |
| 旧 GL 名称在当前只有软件实现 | 0 | 15 个未注册历史扩展仍未恢复 |
| 旧 GL 名称当前软件层也未注册 | **15** | 染色/AlphaTest 兼容能力；不能算普通 shader mapping 缺失 |

核对恒等式：当前 `70 + 0 = 70`；旧 GL `62 + 0 + 15 = 77`。两边名称并集 85，见附录 A；P0 `46/24` 与 P1A `59/11` 记录保存在各自独立基线中。

额外 5 个共享对象别名是 `AdditiveAlphaBlend_HDA`、`PsMulBlend_HDA`、`PsOverlayBlend_HDA`、`PsHardLightBlend_HDA`、`PerspectiveAlphaBlend_a`。`AlphaBlend_HDA` 已在显式 mapping 中，不再重复加。

**特别注意 `ConstAlphaBlend_SD_a`：**旧 GL 把它注册成 `ConstAlphaBlend_SD` 的同一个对象；当前软件却是两个独立对象、分别调用不同函数。初始审计确实缺 mapping；P1A 已添加独立 flags 与双源公式，不能按旧 GL 的对象别名关系实现。见 K1 2788–2793 与 C1 2965–2976。

### 4.2 初始 24 个算子缺口与实施状态

| 组 | 方法 | 数量 | 工作性质 |
| --- | --- | ---: | --- |
| 普通混合 | `SubBlend`、`MulBlend`、`MulBlend_HDA`、`ColorDodgeBlend`、`DarkenBlend`、`LightenBlend`、`ScreenBlend` | 7 | P1A 本地完成；矩形、参数与 alias 域已接入 |
| Photoshop 混合 | `PsAlphaBlend`、`PsAddBlend`、`PsSubBlend`、`PsSoftLightBlend`、`PsColorDodgeBlend`、`PsColorBurnBlend`、`PsLightenBlend`、`PsDarkenBlend`、`PsDiffBlend`、`PsDiff5Blend`、`PsExclusionBlend` | 11 | P1B 本地完成；真实软件三表、整数公式与独立变体 |
| 图像与透明度 | `RemoveOpacity`、`AdditiveAlphaToAlpha`、`AdjustGamma`、`AdjustGamma_a` | 4 | P1A 本地完成；R8 限同尺寸正向合法 ROI；Gamma owned LUT 与软件索引修复 |
| SD 语义 | `AlphaBlend_SD`、`ConstAlphaBlend_SD_a` | 2 | P1A 本地完成；分别为单源 RGB/alpha=0、双源完整 ARGB 插值 |

上述 24 项是初始审计时的软件方法缺口，P1A/P1B 均已完成其矩形描述符与本地实现。许多方法出现在 `tRenderMethodCache` 的 bm* 对应关系中；实际 GPU 路由仍有格式、几何、alias、lease、参数资源和 backend 限制。

### 4.3 已有能力仍受哪些条件限制

| 能力 | 已有实现 | 仍需处理/验证 |
| --- | --- | --- |
| 普通矩形 | copy/fill、alpha、普通混合、部分 Ps、灰度/反预乘、Gamma、alpha/channel 等 | P1A 新源 wrapper 限正向矩形；格式、缩放、裁剪、错位别名、CPU pin/lease 域与原生结果待验 |
| 双源 SD | `ConstAlphaBlend_SD`、`_d`、`_a`，同尺寸矩形 | 错位源目标别名保留回退；单输入 `AlphaBlend_SD` 的 alpha=0 契约另行处理 |
| Universal transition | 普通、`_d`、`_a` 的三源 GPU 路径 | 两个 RGBA 源加 R8 rule、同尺寸与合法源范围；不是所有 CPU transition 都由此覆盖 |
| affine | 两三角形、Copy 及 Alpha/ConstAlpha/AdditiveAlpha/Ps 单源兼容路径 | P2A 本地完成；镜像扫描线、非整数源、其他过滤器保留回退；原生顺序/像素及 tile affine 优化待验 |
| perspective | P2B：Copy 与 Alpha/ConstAlpha/AdditiveAlpha/Ps 单 RGBA 源逆映射 compute | 本地完成；其它方法、过滤器、超限批次、矩形错位自别名等仍软件；新 Apple/真机待验 |
| blur | 两 pass sliding sums；两个名称共用当前软件公式 | 64 MiB sums 限额、无缩放、完整合法 ROI；大图/裁剪等仍可能回退 |
| R8 | mask/rule 资源及 ColorMap/RemoveOpacity 输入 | RemoveOpacity 限同尺寸正向合法 ROI；不等于任意 Gray/province 目标均支持 GPU 运算 |
| 显式 CPU 像素边界 | `DivisibleTransHandler` scanline、`shrinkCopy` lock、LayerEx raw pointer、bitmap dirty update 均可请求 CPU 可见像素 | 不是普通 Layer operation descriptor 的缺口；P2C 必须逐一裁决 GPU 路径、完整覆盖写、必要 CPU 边界与批处理，不能声称任意插件/截图/命中测试零回读 |
| CPU/GPU 一致性 | COW、dirty region、point cache、CPU lease、会话隔离 | 新算子必须正确声明 alpha 变化、源依赖及写 ROI，不能绕过这些机制 |
| tile/compute | 支持设备上 tile；in-place compute 或 snapshot compute | 新算子在所有路径的像素和顺序一致性，不能只测默认 tile |

### 4.4 15 个历史扩展方法

- `AlphaBlend_color`、`AlphaBlend_color_a`、`AlphaBlend_color_d`。
- 上述三个对应的 `_AlphaTest` 变体，以及独立 `AlphaTest`。
- `PsAddBlend_color`、`PsSubBlend_color`、`PsMulBlend_color`、`PsScreenBlend_color`，及各自 `_AlphaTest` 变体。

本轮对当前 core/plugins 的 C++/头文件检索未找到这些方法的直接调用，也未找到旧 stencil/manual-target/动态编译接口的外部调用；这不排除运行时字符串或第三方插件需求。先记录能力缺口，后续结合插件清单、脚本入口及失败样例确认。

当前 Emote 的 mesh/mask/color modulation 使用另一套 backend API。旧 `AlphaBlend_color*` 或 stencil 的缺失，不能直接推导为当前 Emote 不支持染色或蒙版。

## 5. 必须先固定的语义规则

### 5.1 判定正确性的顺序

1. 对已有行为，使用实际初始化后的软件 RenderMethod/函数指针作基线，覆盖当前的参数与资源契约。
2. 对混合公式，同时检查 `gl/tvpgl.cpp` 与 `gl/blend_function.cpp` 的绑定。不能仅拷贝一个名字相近的 `_c` 实现，就假设运行时调用的是它。
3. 使用 Kirikiroid GL 查漏、识别调用习惯、构造边界样例；如有 GL 运行环境，再作为第三方图像对照。
4. 软件、旧 GL、已有游戏表现冲突时，保留最小输入、三个结果和参数，单独记录兼容决策。不要同时“修软件定义”和“新增 Metal 加速”却不标明行为变化。

旧 GL 的测试默认通过注释禁用了 `TEST_SHADER_ENABLED`；检查函数允许 alpha 误差和按 alpha 加权的 RGB 误差，还存在忽略 alpha 的测试分支。因此不能把“旧代码里有 TEST_SHADER”当作所有像素逐字节正确的证据。K5。

### 5.2 混合、颜色与取整

- 普通 Layer 的颜色语义与 Emote mesh 混合分开；不复用一个相似编号的 mesh blend mode 代替。
- 明确 ARGB 数值与内存字节、RGBA8Unorm 纹理、R8 mask、straight/premultiplied alpha，以及 HDA、`_d`、`_a` 的不同公式。
- 逐个保留 `>>8` 与 `/255`、opacity=255 特支、查表、饱和和舍入顺序。
- 新 ordinary Layer 方法优先扩展同一份 `layerPixel()` 整数语义。只有证明固定功能混合在规定输入域精确等价，才允许用它替换。
- 不以全局“允许 ±1”掩盖不同算子错误；现有 compositor 的 ±1 测试标准，不自动应用于 ordinary Layer 的精确像素测试。

### 5.3 几何与回退

- 现有软件 `OperateTriangles()` 断言单输入、两个三角形及特定 quad 结构。**它不是任意三角形的通用参考/回退实现。**C5。
- 已支持 affine 域继续对软件做逐像素比较；新增任意三角形域需要独立的 rasterization 契约和参照。
- 透视不得用两个 affine 三角形近似。必须保留 homography、正确插值、像素中心、源端 `+1` 边界与 clip 相对坐标规则。
- K4 的旧透视代码在每个 quad 循环中构造 6 个顶点，却以 `nQuads * 2` 调 `OperateTriangles()`；当 nQuads>1 时有静态越界风险。这里只确认源码形状，未运行复现；移植时必须使用多 quad 测试，不能复制这一控制流。
- 无效、退化、NaN/Inf、越界和未知参数必须在写目标前拒绝；只向已知支持该输入域的软件路径回退。

### 5.4 别名、COW、参数寿命

- 区分 source==target、reference==target、共享底层资源、完全重合和偏移重叠。
- GPU snapshot 表示读取操作开始时的旧图；软件逐扫描线写入可能表示依赖前面已经写过的像素。两者不能默认互换。
- COW 后 `reference` 可能是原图，target 是新图；转换、Gamma 和混合需要逐方法确定其读源，不能统一当“原地处理 target”。
- 不缓存用户传入的短命 `SetParameterPtr()` 指针到 GPU。Gamma/LUT、矩阵等参数在编码时固定版本，并保证提交完成前资源有效。
- 保持 CPU 原始指针、读写 lease、异常退出和 session detach 的既有行为。同步脚本像素 API 不能擅自返回上一帧。

## 6. 实施阶段与验收门槛

建议顺序：**P0 → P1A → P2A → P1B → P2B → P2C → P3 → P5**。P4 是按调用需求启用的兼容分支。P2C 先建立可归因的基线；默认依次处理更普遍的 `shrinkCopy` 同步、LayerEx raw-pixel ping-pong、tiny `bitmap.update` 批处理和 CPU transition。若目标游戏的同条件新日志显示 `transition.source`/`transition.outputOverwrite` 为主因，可将 transition 子项提升到 P2C 的最前面。

### P0：把能力契约和测试基线固定下来

目标：后续每新增一个算子，都能知道它支持哪些输入以及怎样证明正确。

2026-10-05 签收：按本会话确认的范围，本地代码契约、审计与测试完成即可签收 P0；Apple/真机新结果单独列为待验项。执行平台、三层 HEAD、实际输出和历史证据边界见 [P0 基线记录](NATIVE-METAL-P0-BASELINE.md)。本轮不增加 24 个待补算子的 GPU 描述符，不扩大合法调用的支持域。

工作：

- [x] 将附录清单转成可维护的能力描述/审计输出，记录名称、canonical 对象、operation、输入数、格式、参数、几何、alias 和 alpha 写入规则。
- [x] 用运行时注册检查验证 70 个名称及 46 个已映射名称；为 `_HDA` 和 `PerspectiveAlphaBlend_a` 验证对象共享，为 `ConstAlphaBlend_SD_a` 验证对象独立。
- [x] 固定 `TVPLayerOperationKind` 编号 0–26；共享 `.def` 驱动 Count/traits/MSL 定义及诊断存储，消除数字 switch、输入区间和 `[27]` 联动风险。
- [x] traits 覆盖输入数量/格式、读取目标、写 alpha、alias、参数资源及几何；保留 reference/COW、HDA、blur、SD 与 affine 的现有例外。
- [x] 复用确定性像素、参数边界、ROI/别名/租约与 GPU 计数测试，增加非法 kind、安全失败及测试用新 kind 的 Count 扩展验证。
- [x] 保存当前本地测试与历史真机证据，记录平台、HEAD、skip 与 device double；新原生 Metal/真机基线列为待验项。
- [x] 动态编译空结果返回明确失败且不注册，注册入口拒绝空对象；验证重复失败及 Release 路径，不提供动态 GLSL 功能。
- [x] 更新 backend 与 Layer README，明确已加速矩形及 affine Copy 子域和软件回退限制。

修改位置：C1/C2/C6/C7、`MetalRenderBackend.h/.mm`、C4、T1/T2、相关 README。

验收：已有渲染结果和路由不变；能力清单能区分“未注册 / 无描述符 / 参数不支持 / backend 不可用”；添加一个测试用新 kind 时不出现统计越界或 shader 编号漂移。

### P1A：补高确定性的矩形语义

2026-10-05 本地签收：13 项已实现，MetalLayer 2/2 与 MetalRenderBackend 1/1 通过；新增 21,757,036 个精确 scalar 像素。按用户选择同步修复 Gamma_a 的 LUT[256] 越界，除此之外跟随实际软件绑定。原生 MSL、iOS 构建与本轮真机回归仍待验，详见 [P1A 基线](NATIVE-METAL-P1A-BASELINE.md)。

#### A. 普通混合 7 项

- [x] 实现第 4.2 节普通混合组，分别核对 HDA 和 opacity=255 的具体分支。
- [x] 保持 `MulBlend` 与 `_HDA` 对象和 flags 独立，各自与真实软件绑定比较；当前两个绑定均保留目标 alpha，不人为制造差异。普通 Screen/ColorDodge 不复用 Ps 公式。
- [x] 将格式、alias、alpha-cache invalidation 与像素实现一起接入 C2/C3。

#### B. 透明度、转换与 Gamma 4 项

- [x] `RemoveOpacity`：按当前软件读取 R8 mask，只改变 alpha；穷举 mask/opacity/alpha 组合。同尺寸正向合法 ROI 支持；缩放/镜像在写入前明确拒绝，不进入 RGBA 寻址的软件 resize。
- [x] `AdditiveAlphaToAlpha`：精确整数实现与 TVPDivTable 等价，覆盖 alpha=0、1、254、255 和 RGB>alpha；沿用 ApplySelf/reference/COW 契约。
- [x] `AdjustGamma` / `_a`：复用软件生成的 owned LUT，以不可变版本快照编码，不在 MSL 重算 pow。
- [x] Gamma `_a` 独立核对 alpha/超 alpha RGB/上下限/COW/reference/连续改参数；软件与 GPU 同步钳制 LUT 索引至 255，并记录兼容性修复。
- [x] Gamma 不新增纹理、每算子提交或同步；两个最近版本缓存和命令资源持有保证寿命，参数上传单独计数。

#### C. SD 2 项

- [x] `ConstAlphaBlend_SD_a`：扩展现有双源公式，按 input0/input1 顺序完整 ARGB 插值，保持独立方法对象。
- [x] `AlphaBlend_SD`：单显式 source 与目标旧值执行 RGB 插值、输出 alpha=0，不要求两个显式输入。
- [x] 双源及新单源混合的错位目标别名继续按顺序相关软件语义回退，保留反例测试。

修改位置：C1 的参数类与映射、C2 的输入/格式路由、C6 参数结构、C4 像素函数与双源函数、C3 LUT 资源绑定及执行。

验收：13 个名称在声明支持域内 GPU 执行；预先驻留的 RGBA/R8 输入不产生 fallback/readback/整图上传；Gamma 允许必要的小型参数/LUT 上传，必须与图像上传分开统计。离开支持域仍有正确结果及准确拒绝原因。

### P1B：补 Photoshop 混合 11 项

2026-10-06 本地签收：fetch 后三层 metal_dev 与 origin/metal_dev 均 0/0、文件 diff 为空、子模块指针一致；随后实施 P1B。MetalLayer 2/2 与 MetalRenderBackend 1/1 通过，新增 46,137,346 个精确 scalar 像素；原生 MSL/Apple/真机结果待验，见 [P1B 基线](NATIVE-METAL-P1B-BASELINE.md)。

- [x] 实现第 4.2 节 Ps 组全部 11 项，共用 layerPsP1BPixel 整数 helper。
- [x] SoftLight/Dodge/Burn 导出实际软件初始化表，196608-byte 三表包只读上传/缓存，命令保留旧资源，不重算 pow。
- [x] 验证 ColorDodge/ColorDodge5、Diff/Diff5 与普通/Ps 的公式/alpha/opacity 次序，增加独立反例及端点断言。
- [x] tile、in-place 与 snapshot compute 均绑定 buffer(3)；RGBA 正向单源矩形、same-pixel alias、错位软件回退与 pin/lease 域有测试。

验收：与 P1A 合计 24 个缺口均完成矩形支持与反例回退；当前 70 个注册名称都有明确的 GPU 能力描述或经记录的限制。这里不宣布其所有几何和所有输入均 GPU 化。

### P2A：把现有 affine Copy 扩展到 Layer 混合

目标：优先解决现有游戏可到达的 affine quad 回退，不把通用网格设计作为阻塞项。

2026-10-06 本地签收：19 个混合 kind、30 个方法/别名接入准备后的 affine 域，MetalLayer 2/2 和 MetalRenderBackend 1/1 通过。Copy 快路、矩形与 affine 各自采样域保持；原生验证和设备上的 tile 性能评估仍待验，详见 [P2A 基线](NATIVE-METAL-P2A-BASELINE.md)。

- [x] 梳理既有约束，将 manager 入口扩展为 `GPUAffine()`，保留 `TVPLayerAffineCopy`、Prepare 与原 Copy kernel。
- [x] 分离采样与混合，新 compute kernel 复用 `layerPixel()`；接入注册的 Alpha/ConstAlpha/AdditiveAlpha 变体与全部 Ps 家族。
- [x] 软件 warp 读取 target 旧值并忽略 reference；完整 clip 的透明 warp 边界参与混合，复用 alpha/PS tables 与源/目标 GPU 快照。
- [x] 精确 portable 回归覆盖旋转、错切、非整数目标位置、非矩形反射、源 ROI、目标 clip、单像素/单行/列、alias/COW/cache/lease；特殊扫描线和其他几何仍回退。
- [x] affine 限 StretchType 0..2；矩形既有过滤器域不变。新增 `AffineAlias` 诊断区分矩形重叠。
- [x] 明确首版非矩形 affine 使用 compute 与快照，会关闭 tile pass；矩形特判保留 pass 复用。未实现 affine tile fragment，设备成本评估留作待验。

验收：声明支持的 affine quad 与软件逐像素一致；仍不支持的几何有最小反例；无额外 CPU 中转；triangle diagnostics 区分 GPU 成功、方法缺失、采样/几何/alias 拒绝。

### P2B：新增真正的 Perspective GPU 路径

2026-10-06 本地签收：Copy 与 P2A 的 19 个混合 kind 接入单 RGBA 源 perspective，32 个名称/别名参与测试；MetalLayer 2/2、MetalRenderBackend 1/1 通过，共 78,622 次精确表面比较，见 [P2B 基线](NATIVE-METAL-P2B-BASELINE.md)。支持域外的有效输入仍软件；新原生/真机验证待验。

- [x] 新增 quad 描述与默认 false 的通用 batch 入口；最多 256 quad 的 Metal 事务，失败在真实目标提交前返回。
- [x] 共用原 Gaussian 求解与 3×3 inverse，固定 LT/RT/LB/RB、右/下 +1、whole-source、clip-relative centers 与透明边界；不引入 OpenCV。
- [x] 使用真正逆 homography compute 与共用像素函数；按 quad 次序更新 scratch，最后提交真实目标，不用 affine 两三角形替代透视。
- [x] 沿用 `PerspectiveAlphaBlend_a` 共享对象与描述符，不新增注册或 kind 编号。
- [x] 精确回归覆盖 identity/变换/强透视/反射、源目标裁剪、分母、混合多 quad/alias、失败原子性、COW/cache/lease 与无中途读回的排队批次。
- [x] 修复软件矩形 point2/point3 错误、不相交误 warp、晚 quad 非法输入与未初始化矩阵、负过滤器和非有限采样；独立参照确认新行为。

验收：当前合法单输入透视场景留在 GPU；多 quad 不越界、不相互污染参数；覆盖边缘和采样的差异有逐项结论；异常输入不会在部分修改目标后再次软件渲染。

### P2C：消除已证实的 CPU/GPU 边界与 tiny-upload churn

目标：把先前 P5 中泛化的“检查读源”变成可交付的运行时路径。这个阶段不以“所有 CPU pixel API 必须消失”为目标；CPU 算法、截图或插件 ABI 仍可能合理地需要边界。目标是让声明支持的生产路径保持 GPU resident，或将不可避免的 CPU 边界收敛为一次、可归因、语义正确的操作，而不是默许整图往返、同步 lock 或每个小 dirty rect 一个 encoder。

#### C0. 先补齐传输归因与可比较基线

2026-10-07 本地签收：64 槽 origin（40 个保护槽/24 动态槽）与显式 overflow、2048 对真实 runtime 帧间隔/CPU wall 样本、v2 C/Swift/分析接口和设置页构建 HEAD 已接入。Layer CTest 4/4、backend 1/1、frame 检查通过；78,628 精确表面比较、3 session。按用户选择，真机配对基线仍待验，不宣称性能改善，见 [C0 基线](NATIVE-METAL-P2C-C0-BASELINE.md)。

- [x] 在 `metal.layerWork` 中增加与 texture-ID top-N 并列、但不按 texture ID 拆分的**有界 origin 聚合**：每个 origin 的 calls、bytes、wallNS、sync waitNS；保留 top-N texture 明细用于定位，不能再由 `other` 隐藏 stall 来源。
- [x] 为 `transition.source`、`transition.outputOverwrite`、`shrinkCopy.read/write`、`layerExBase`、`layerExDraw.write`、`bitmap.update` 和未分类 lock/pixels 保持稳定且可测试的命名；新来源超长/超容量显式进入 overflow，原字符串不改写。
- [x] 计数器只观察既有工作；生产 texture/cache/lease 路径与 device double 验证实际次数、bytes、wait、generation、开关工作量一致，未新增 GPU 同步。原生分支的 submit/wait 断言需 Apple 执行。
- [x] 逐帧采样、整窗口分析、nearest-rank 分位数、旧格式下界/缺失/丢失标识，以及设置页与诊断共用 Bundle HEAD 均有本地连线/数学测试；App XCTest 与 UI 编译待 Apple。
- [ ] 以第 2.4 节的游戏/场景重放采集改动前后样本，同时记录设备、OS、三层 commit、分辨率、热状态、运行时间、输入步骤和 frame-time 分位数。没有同条件的 before/after，不宣称改善。

#### C1. `shrinkCopy`：优先消除同步 lock 路径

- [ ] 从 `shrinkCopy.read` 和 `shrinkCopy.write` 反查全部调用链、ROI、filter、源/目标驻留、读旧目标需求和调用频率。将“名称为 write”与“可以完整覆盖目标”分开：前者并不自动允许 overwrite。
- [ ] 对已有 GPU-resident 源/目标且语义等价的 resize/copy，走 GPU blit/render/compute 路径，保留当前像素、边缘、clip、alias/COW 和操作顺序。只在**证明整个写入区域完全定义**时使用 overwrite contract；部分 read-modify-write 禁止借此跳过旧目标读取。
- [ ] GPU 路径不可用时保留正确 CPU fallback，并以 origin 记录其数据量与同步等待；不得为消除 readback 改变缩放滤镜、区域外像素或重叠顺序。

验收：对可重放的、源和目标均已驻留的 `shrinkCopy` 支持域，`shrinkCopy` 归因的 readback calls/bytes/sync wait 均为零，且无整图 CPU upload；完整覆盖写不会先读旧目标。DRACU-RIOT!、天使纷扰和 9-nine 的同条件日志分别报告次数、bytes、p50/p95 wait 与长帧数；仍需 CPU 的调用有明确原因，不能混入 `other`。

#### C2. LayerEx/raw-pixel：收敛 GPU↔CPU ping-pong，而非假定插件可自动 GPU 化

- [ ] 将 `LayerExBase`、`LayerExDraw`、`ScopedLayerPixels` 等调用按只读、完整覆盖写、局部读改写和真 CPU 算法分类，并捕获调用栈/功能样例。每类明确 CPU cache、dirty ROI、lease、COW、alpha cache 和跨会话资源语义。
- [ ] 对有已知 GPU 等价实现的功能接入专用 GPU 路径；对 ABI 必须提供 raw pointer 的功能，设计单次、区域化的 acquire/release 与上传边界。不能通过延迟 dirty、复用失效 CPU buffer 或虚构 full overwrite 改变插件可见像素。
- [ ] 以天使纷扰的 1052×900 `layerExDraw.write` → `layerExBase` 交替访问为回归样例。重构前先证明两次完整往返由同一调用序列造成；重构后要么消除其中可 GPU 化的一段，要么记录为何 ABI/算法必须保留边界。

验收：样例的像素、raw-pointer 可见性、区域外内容、COW/lease 和资源释放保持正确；一次逻辑插件处理不会在没有新的 CPU 消费者需求时对同一完整纹理产生两次 GPU→CPU→GPU 往返。若语义确实要求多次 CPU 阶段，报告每段 origin、区域和 wait，而不是把它标成“已优化”。

#### C3. `bitmap.update`：按帧聚合小更新与 encoder 生命周期

- [ ] 先用 C0 origin、调用点和 dirty-rect/texture 数据验证 `bitmap.update` 与 blit encoder 的关系；相关性不是充分的调用链证据。区分启动资源加载、字形/文本、小 UI、movie frame 与普通 bitmap 写入。
- [ ] 在不改变可见次序的前提下，把同一帧中可合并的 dirty rect 放入 staging arena/ring，并复用尽可能少的 blit encoder；跨目标、跨依赖或中间读取时保留顺序，不能为减少 encoder 数而延迟本帧必须可见的内容。
- [ ] 约束 staging 资源寿命、背压、最大合并大小、内存上限和 submission 预算；场景切换、后台恢复和跨会话销毁不得引用旧 staging buffer。

验收：小 bitmap 的像素、dirty ROI、可见时序及 CPU/GPU 所有权正确；在选定的高频 UI/文本场景中，encoder/submit 数不再与 `bitmap.update` 次数近似一一对应（受真实依赖限制的例外必须记录）。报告每秒 update、encoder、submit、CPU encoding 时间和 frame-time，而不是只报告累计 MiB。

#### C4. CPU transition：对高频 `DivisibleTransHandler` 建立 GPU 保留路径

- [ ] 先统计真实游戏使用的 `DivisibleTransHandler` 类型、规则/源数、frame 尺寸、持续帧数和 fallback 条件；优先映射高频且能与现有 Universal transition 或新增 Metal kernel 语义等价的类型。不能把“已有 Universal transition”误称为覆盖任意 CPU transition。
- [ ] 在支持域内让 source、rule 和 output 全程 GPU resident，并保持 alpha、clip、时间步进、随机性、目标别名、COW 与中断/场景切换语义。未知或不等价 transition 仍走正确 CPU 路径，且诊断必须标明类型和原因。
- [ ] 不把 `LockCPUWriteForOverwrite()` 当作 transition GPU 化的替代方案：它仅省去旧 output readback；CPU 生成的每帧 full-frame output 仍会产生 upload，必须由 GPU 计算路径消除。

验收：对已支持的 1920×1080/1440 transition 重放，`transition.source` readback bytes、`transition.outputOverwrite` upload bytes、该 transition 的 CPU fallback 和归因 sync wait 均为零；无新增每帧 submit 或等待，并与软件参照逐像素一致或有预先定义的容差。未支持类型必须正确回退且可从 origin 聚合中辨认，不能以总 readback 为零掩盖它。

### P3：通用三角形能力

目标：覆盖旧 GL 的一般 `OperateTriangles()` 形状；这是扩展引擎能力，不只是删除 `count!=2` 检查。

- [ ] 明确顶点/UV/clip 数据结构、每输入坐标、三角形覆盖规则、绕序、重叠次序和裁剪边界。
- [ ] 首批支持单输入、已验证的 RGBA blend 和 Copy；多输入 mesh 必须逐种算子扩展，不能接受任意数量后静默丢弃输入。
- [ ] 构造独立的受控 CPU rasterizer/数学样例，结合旧 GL 输出检查通用域；不向现有仅接受 quad 的软件函数传任意 mesh。
- [ ] 对严格匹配当前软件 affine 的情况继续保留旧快路；通用 rasterization 的 top-left/像素中心规则与软件不一致时分开描述。
- [ ] 对没有安全 CPU 实现的新输入域，明确返回能力不足/受控错误，或先提供正确回退；禁止 release 下依赖被移除的 assert 保证安全。
- [ ] 保持同目标重叠绘制的顺序，验证 tile/compute 切换、snapshot 复用、暂存顶点寿命与提交预算。

验收：单个三角形、多个不连通三角形、共享边、重叠三角形、裁剪和极小三角形均有确定结果；声明支持域在原生 GPU 上验证；软件不支持域不会发生越界或误调用。

### P4：按需恢复历史扩展接口

触发条件：发现真实插件/脚本调用、准备恢复旧插件，或明确要求旧 GL 接口全兼容。未触发时保留缺口清单和错误提示，不占核心路线的首轮工期。

- [ ] 15 个方法按 color modulation 与 alpha-test 两个维度实现；给颜色、threshold、丢弃像素是否保留目标 alpha 建立契约。
- [ ] 先建立软件/数学参照或可运行旧 GL 样例，再接 Metal；当前软件没有这些名称，不能直接复用其空指针查找结果。
- [ ] `BeginStencil/EndStencil/SetRenderTarget` 若要恢复，必须恢复完整调用协议、状态复位和目标尺寸变化；不把它简单等同于现有 Emote mask。
- [ ] 动态 GLSL 若有真实需求，单独决策：支持有限已知 shader 的显式注册，或开展完整编译兼容项目。不能声称原生 Metal 自动支持任意 GLSL。

验收：每项扩展有实际调用样例、参数契约和错误路径；没有调用证据的能力标记“延后”，而非标成已完成。

### P5：已有路径的性能、资源和最终验收

- [ ] 复测当前 tile 合成；新增像素族继续共享整数逻辑，记录实际活动路径和 shader 初始化失败。
- [ ] 分析真实 alias snapshot 面积、生命周期、分配和 pass 切换；优先减少可证明冗余的复制。GPU ping-pong 本身不是失败。
- [ ] 对 blur 超 64 MiB 临时预算的输入评估分块/条带算法，保留正确 halo 和 source==target 语义。没有明确需求前不取消预算。
- [ ] 将大核/偶数核/非对称 area 与当前软件规则锁定；是否恢复另一种 alpha-aware BoxBlurAlpha 作为单独兼容变更评估。
- [ ] 复核 P2C 之外的低频 CPU 边界：截图、命中测试、异步 alpha 和尚未触发的插件。CPU transition、`shrinkCopy`、LayerEx/raw-pixel、`bitmap.update` 的实施与专项验收归 P2C；这里仅处理新证据和跨路径回归，不把“未触发”写成零回读。
- [ ] 保存原有异步 alpha 的 presentation gating、不可变显示版本和 simulator guard；新算子正确失效 alpha cache，HDA 仅在真正不改变 alpha 时保留缓存。
- [ ] 测试场景切换、前后台、截屏、分辨率变化、显存压力、资源销毁、异步回调与跨会话缓存。
- [ ] 沿用 `metal.gpuStages`、`metal.layerWork`、fallback/readback/upload 来源及 triangle profile；必要时新增方法×拒绝原因×像素量统计，保持有界且不添加同步。

验收：见第 8 节。发布结论必须区分“功能正确”“避免了某类传输”“同条件性能改善”，不合并为一句“全 GPU 已优化”。

## 7. 建议提交拆分与依赖

| 提交包 | 内容 | 前置 | 风险/验证重点 |
| --- | --- | --- | --- |
| A | 清单、别名/能力测试、kind 与统计约束、动态编译失败保护 | 本文 | 小；原行为不变 |
| B | RemoveOpacity、反预乘、Gamma LUT 参数支持 | A | 中；mask 格式、alpha、参数寿命 |
| C | 普通混合 7 项、SD 2 项 | A | 中；整数与输入契约 |
| D | affine alpha/const/additive 扩展 | A、对应像素族 | 高；采样边界与别名 |
| E | Ps 11 项及 affine 中适用的像素族 | A、D 的接口 | 中；表函数、变体差异 |
| F | 单源 perspective | D、语义裁决样例 | 高；坐标/多 quad/退化输入 |
| G0 | P2C origin 聚合、无同步诊断和场景基线 | F、现有 `metal.layerWork` | 中；计数正确性与诊断不能改变时序 |
| G1 | `shrinkCopy` GPU 路径与完整覆盖写裁决 | G0、resize/copy 语义样例 | 高；filter、ROI、alias、旧目标读取 |
| G2 | LayerEx/raw-pixel 边界收敛 | G0、插件样例 | 高；ABI 可见性、COW/lease、CPU cache |
| G3 | `bitmap.update` staging/encoder 批处理 | G0、调用点证据 | 高；更新顺序、可见时序、资源寿命 |
| G4 | 高频 CPU transition GPU 保留路径 | G0、实际 handler 样例 | 高；时间/规则语义、三源与回退边界 |
| H | 通用三角形与安全支持域 | D、独立参照 | 高；软件回退不通用 |
| I | 按需历史 color/AlphaTest/stencil | 明确调用需求、H 中所需能力 | 高；扩展旧协议 |
| J | snapshot/blur/低频边界专项优化和真机收口 | 对应功能包稳定 | 高；收益只能实测 |

每包均包括必要的 production scalar 对照、路由与资源测试、原生 GPU 测试和文档更新。不要把所有 Metal 修改合成一个大提交，再补测试。

跨仓库位置：主要实现属于 core；host 只在新增诊断/配置确有需要时变更；测试/CI/计划属于主仓库。实现完成后的提交和 submodule 指针更新按 core → runtime/host → 主仓库顺序处理。本计划不执行提交或推送。

不在源码静态审计阶段承诺精确人日；最大不确定项是 raw-pixel ABI 的必要边界、transition 类型的真实覆盖、几何兼容裁决、旧接口真实需求及 Apple 设备验证。A/B/C 是确定性较高的首批，G1–G4/H/I 需阶段性评估。

## 8. 验证矩阵与完成标准

### 8.1 像素与参数

| 维度 | 最低覆盖 |
| --- | --- |
| 通道/透明度 | 0、1、127、128、254、255；边界组合、固定种子随机；独立透明 RGB |
| opacity | 0、1、127、128、254、255；无效值的受控处理 |
| 混合变体 | normal/HDA/`_d`/`_a`、满 opacity 特支；普通与 Photoshop 方法分开 |
| 表函数 | 可穷举的通道二元组合全部遍历；检查实际软件初始化后的绑定 |
| Gamma | identity、逐通道不同 gamma/上下限、连续改参数、两个版本在同一 command buffer |
| 格式 | RGBA8/R8、错误格式拒绝、padded pitch、通道顺序 |
| ROI | 1×1、单行/列、奇数尺寸、四边裁剪、空交集、区域外像素不变 |
| 几何 | 1:1、缩放、镜像、旋转、错切、透视、shared edge、多 quad 和退化输入 |
| alias/COW | 同像素别名、偏移重叠、独立 reference、共享图像、static image、COW 后转换 |
| 资源 | CPU pin/lease、dirty ROI、同一 buffer 多次操作、切换目标、销毁/解绑/重建 |

整数颜色转换和混合以逐字节一致为默认门槛。采样/几何若客观存在旧软件与硬件 rasterization 差异，先固定允许差异的坐标域和理由，再执行验收，不能事后放宽容差使测试通过。

### 8.2 四层验证

1. **Portable 数学与路由**：生产 shader helper 的可移植提取、当前软件方法、真实 manager+COW/缓存逻辑。设备替身只能验证路由/资源契约，不能验证 GPU 执行。
2. **macOS 原生 Metal**：编译生产 MSL，验证绑定、实际像素、alias/order、提交与等待。默认 tile 和强制 compute 分别运行。无法初始化 Metal/返回 77 必须报告 skip。
3. **iOS device + simulator 构建**：验证真实框架、插件、宿主与编译条件；两类构建都成功仍不等于手机性能通过。
4. **iPhone/iPad 场景**：同场景画面、透明与输入、帧耗时和温度条件、内存、前后台/换游戏。性能测试关闭 Metal validation，正确性测试开启。

### 8.3 GPU 驻留与同步门槛

- 先上传测试输入并建立资源，再清零计数。对声明支持域内的操作，要求 `cpuFallbacks=0`、像素 `readbackBytes=0`、无重新上传整图。
- 最后为像素比较读取结果的 readback 单独计量，不将验收读回算成生产路径回退。
- 参数/LUT/顶点上传是必要 GPU 输入，单独记录；不能用“绝对零上传”误拒正确实现。
- 正常提交预算触发允许保留；不新增每算子提交或 `waitUntilCompleted`。诊断开启不得增加额外提交/等待。
- 对应禁用条件和设备失败必须有测试，确保回退保留输入、区域外像素和参数状态；失败不能先改一部分目标，再重复执行整个软件操作。
- 对 P2C，先同时查看按 origin 聚合与 texture-ID top-N，报告 calls、bytes、wallNS、sync waitNS；`other` 只能是有界的补充桶，不能作为关键 stall 的最终归因。对不需要 CPU 的指定支持域，逐 origin 断言为零；对 ABI/算法确需 CPU 的路径，断言一次边界的区域、原因和顺序正确，而不是用全局总数掩盖它。
- 小传输验收必须同时记录次数、每秒峰值、encoder/submit 数、CPU encoding 时间和 frame-time 分布。小于 1 MiB 的 readback 仍可能等待前序 GPU 工作，禁止以累计 bytes 小而跳过同步指标。

### 8.4 真机场景与指标

| 场景 | 主要验证 | 基线来源/限制 |
| --- | --- | --- |
| 9nine UI/效果 | Gamma 与 Ps/普通混合、透明和短时尖峰 | D1 曾记录少量 Gamma 回退；需新版本同条件复测 |
| 天使纷扰设置/鉴赏 | Stretch、复制/混合、fallback 和传输 | 已有修复不能列为本计划新成果 |
| 天使纷扰后期剧情 | tile 与 compute 成本、持续帧时间及热状态 | D1 有低回读下 GPU/展示等待证据，不能只看回退数 |
| 天使纷扰 LayerEx 触发流程 | raw-pixel boundary、同 texture ping-pong、CPU wait | P2C C2 重放 1052×900 往返样例；需先确认实际插件功能和调用栈 |
| DRACU-RIOT! 缩放触发流程 | `shrinkCopy` 小读回的 p50/p95 wait、长帧 | P2C C1；现有样本 155 次/271.4 ms 仅作待复测基线 |
| 高频 UI/文本流程（少女领域、DRACU-RIOT!、9-nine、青空下的加缪各至少一个） | `bitmap.update` rate、blit encoder/submit、CPU encoding | P2C C3；先验证调用点，不将相关性当成因果 |
| 已知 CPU transition 流程 | `transition.source`/`outputOverwrite`、画面和时间步进 | P2C C4；仅在新日志确认触发时进入最高优先级 |
| 千恋万花流程图首次/再次打开 | 图像加载、脚本时间与渲染的分离 | 解码/首次加载成本不由 Metal 算子覆盖自动解决 |
| affine/perspective 专用场景 | 正确坐标、边缘、COW、回退来源 | 若游戏缺少稳定样例，先制作可重放最小脚本 |
| Emote 静置及连续点击 | mesh、mask、capture、显示版本与输入 | 与 ordinary Layer 缺口分开归因；保留 async alpha 行为 |
| 重复换游戏/前后台/截图 | 资源寿命、内存与 session cache | 不把单场景通过视为全生命周期通过 |

记录设备/OS、三层 commit、渲染开关、场景与输入步骤、分辨率、诊断模式、初始/末尾热状态、运行时间。帧时间记录 p50/p95/p99 和长帧数；固定热状态下的冷启动与稳定运行分别比较，至少重复 3 次。

同时记录 fallback 按原因/方法分布、像素回读/上传、GPU snapshot 字节、GPU stage 与 command 时间、CPU/nextDrawable 等待、内存峰值。stage 可能重叠，不能相加为 command 总预算；旧累计 MB 不直接换算成物理带宽或功耗。

### 8.5 现有验证入口

以下是实施阶段的命令模板，**不是本轮已执行结果**。在仓库根目录运行；各平台补齐现有 CMake 依赖。

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure --verbose

cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel
ctest --test-dir build/metal-render-tests --output-on-failure
```

Apple 配置参照现有 `.github/workflows/ios.yml`，提供 SDL3 的 `CMAKE_PREFIX_PATH`。T1 的 Apple CTest 已注册默认和 `MIKAGE_METAL_LAYER_TILE_RENDERER=0` 两套；现有 CI 还包含 Release composition workload。继续使用，不重复搭一套只验证测试替身的流水线。

新增更细的 fallback/采样强制开关时，必须保证它只用于验证，并记录实际路径；当前强制 compute 不代表已经覆盖不支持 read-write texture 的 snapshot compute 设备路径，必要时补可控注入。

### 8.6 分阶段签收

- [x] P0：静态清单与运行时名称/对象关系一致，已有 portable 像素/路由回归通过；原生 Metal 与新真机基线待验，见签收范围。
- [ ] P1：24 个矩形语义缺口完成，全部支持域/回退域有证据。
- [ ] P2：常用 affine 混合及单输入 perspective 留在 GPU，几何边界裁决完成。
- [ ] P2C：按 origin 的无同步诊断与同条件基线齐全；已选择的 `shrinkCopy`、LayerEx、tiny update、transition 子项分别满足其专属正确性和传输/同步门槛。未选择或仍必须 CPU 的子项有具名原因。
- [ ] P3：通用三角形域有独立参照与安全拒绝/回退。
- [ ] P4：逐项标明“未触发/延后/已验收”，不作为当前核心功能完成的隐含条件。
- [ ] P5：Apple 测试与 device/simulator 构建通过，目标真机场景达到预先保存的正确性和性能门槛。

允许声明核心路线完成的条件是 P0/P1/P2/P2C/P3/P5 验收齐全，P4 有明确处置记录。允许先发布较小功能包，但只能声明该包已覆盖的输入域，不能写“Kirikiroid 全兼容”。

## 9. 初始审计与 P0/P1A/P1B/P2A/P2B 实施状态

初始审计已完成：固定版本源码获取、旧 GL/当前软件/Metal 注册与别名核对、接口及几何/blur/回读审计、现有测试与 CI 阅读、实施依赖和验收计划。

P0/P1A/P1B/P2A/P2B 与 ARC 补修已提交；用户报告 P2A 测试暂未发现问题。P1B 能力审计为 70/70/0/15；P2A/P2B 的几何实现和证据各自保存。P2C C0 在三层与远端一致的 HEAD 上完成诊断聚合、逐帧采样、解析工具、版本 HEAD 和 portable 回归，本轮未提交，真机基线待验，见独立 C0 记录。

仍未完成：C0 的 Apple/真机对照、C1–C4、P3–P5，以及新原生像素/性能/热状态基线和旧 GL 实际运行。普通 Layer fallback 为零不排除 scanline/lock/raw-pointer 边界；C0 观察能力不等于已优化。后续按同条件实测优先级处理 shrinkCopy、LayerEx、tiny update 与 CPU transition；既有版本反馈不替代新增代码验收。

## 附录 A：完整名称对照

“已映射”只表示名称能取得非 Unsupported 的描述符；实际 GPU 路由还受第 4.3 节约束。几何列中的例外优先于名称状态。

| 名称 | 旧 GL 注册行 | 当前软件注册 | Metal 描述符/差距 | 工作项 |
| --- | ---: | --- | --- | --- |
| `AddBlend` | 2987 | 有 | 已映射：`Add` | 保留并回归 |
| `AdditiveAlphaBlend` | 3013 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaBlend_HDA` | — | 有 | 共享 `AdditiveAlphaBlend` 对象 | 保留别名测试 |
| `AdditiveAlphaBlend_a` | 3018 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaToAlpha` | 3246 | 有 | 已映射：`AdditiveAlphaToAlpha` | P1A 本地完成；Apple/真机待验 |
| `AdjustGamma` | 2731 | 有 | 已映射：`AdjustGamma` | P1A owned LUT；Apple/真机待验 |
| `AdjustGamma_a` | 2771 | 有 | 已映射：`AdjustGamma`，premultiplied flag | P1A owned LUT/索引修复；Apple/真机待验 |
| `AlphaBlend` | 2881 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_HDA` | — | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_SD` | 2912 | 有 | 已映射：`AlphaSD`，单输入 | P1A 本地完成；alpha=0 |
| `AlphaBlend_a` | 2932 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_color` | 2887 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_AlphaTest` | 2893 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_a` | 2939 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_a_AlphaTest` | 2945 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_d` | 2973 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_d_AlphaTest` | 2975 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_d` | 2963 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaTest` | 2897 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaToAdditiveAlpha` | 3256 | 有 | 已映射：`AlphaToAdditiveAlpha` | 保留并回归 |
| `ApplyColorMap` | 2590 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `ApplyColorMap_a` | 2608 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `ApplyColorMap_d` | 2598 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `BoxBlur` | 3277 | 有 | 已映射：`BoxBlur` | P5：预算/语义边界 |
| `BoxBlurAlpha` | 3292 | 有 | 已映射：`BoxBlur` | P5：预算/语义边界 |
| `ColorDodgeBlend` | 3026 | 有 | 已映射：`ColorDodge` | P1A 本地完成；Apple/真机待验 |
| `ConstAlphaBlend` | 2709 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_HDA` | — | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_SD` | 2788 | 有 | 已映射：`ConstAlphaSD` | 保留双源路径 |
| `ConstAlphaBlend_SD_a` | 2793 | 有 | 已映射：`ConstAlphaSD`，premultiplied flag | P1A 独立双源 ARGB 公式 |
| `ConstAlphaBlend_SD_d` | 2796 | 有 | 已映射：`ConstAlphaSD` | 保留双源路径 |
| `ConstAlphaBlend_a` | 2714 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_d` | 2722 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstColorAlphaBlend` | 2644 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `ConstColorAlphaBlend_a` | 2649 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `ConstColorAlphaBlend_d` | 2665 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `Copy` | 2587 | 有 | 已映射：`Copy` | 保留并回归 |
| `CopyBlueToAlpha` | — | 有 | 已映射：`CopyBlueToAlpha` | 保留并回归 |
| `CopyColor` | 2699 | 有 | 已映射：`CopyColor` | 保留并回归 |
| `CopyMask` | 2703 | 有 | 已映射：`CopyMask` | 保留并回归 |
| `CopyOpaqueImage` | 2692 | 有 | 已映射：`CopyOpaque` | 保留并回归 |
| `DarkenBlend` | 3034 | 有 | 已映射：`Darken` | P1A 本地完成；Apple/真机待验 |
| `DoGrayScale` | 3267 | 有 | 已映射：`GrayScale` | 保留并回归 |
| `FillARGB` | 2633 | 有 | 已映射：`Fill` | 保留并回归 |
| `FillColor` | 2636 | 有 | 已映射：`FillColor` | 保留并回归 |
| `FillMask` | 2639 | 有 | 已映射：`FillMask` | 保留并回归 |
| `LightenBlend` | 3040 | 有 | 已映射：`Lighten` | P1A 本地完成；Apple/真机待验 |
| `MulBlend` | 2999 | 有 | 已映射：`Mul` | P1A 独立对象/flags；当前保留 alpha |
| `MulBlend_HDA` | 3006 | 有 | 已映射：`Mul`，HDA flag | P1A 独立对象/flags；保留 alpha |
| `MultiplyAlpha` | — | 有 | 已映射：`MultiplyAlpha` | 保留并回归 |
| `PerspectiveAlphaBlend_a` | 2923 | 有 | 共享 `AlphaBlend_a` 对象 | P2B：单 RGBA 源透视支持域本地完成，Apple/真机待验 |
| `PsAddBlend` | 3058 | 有 | 已映射：`PsAdd` | P1B 本地完成；Apple/真机待验 |
| `PsAddBlend_color` | 3066 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAddBlend_color_AlphaTest` | 3068 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAlphaBlend` | 2917 | 有 | 已映射：`PsAlpha` | P1B 本地完成；Apple/真机待验 |
| `PsColorBurnBlend` | 3175 | 有 | 已映射：`PsColorBurn`，三表资源 | P1B 本地完成；Apple/真机待验 |
| `PsColorDodge5Blend` | 3168 | 有 | 已映射：`PsColorDodge5` | 保留并回归 |
| `PsColorDodgeBlend` | 3159 | 有 | 已映射：`PsColorDodge`，三表资源 | P1B 与 Dodge5 分开验证 |
| `PsDarkenBlend` | 3190 | 有 | 已映射：`PsDarken` | P1B 本地完成；Apple/真机待验 |
| `PsDiff5Blend` | 3202 | 有 | 已映射：`PsDiff5` | P1B 源先 fade 再 difference |
| `PsDiffBlend` | 3196 | 有 | 已映射：`PsDiff` | P1B difference 后插值 |
| `PsExclusionBlend` | 3209 | 有 | 已映射：`PsExclusion` | P1B 本地完成；Apple/真机待验 |
| `PsHardLightBlend` | 3143 | 有 | 已映射：`PsHardLight` | 保留并回归 |
| `PsHardLightBlend_HDA` | — | 有 | 共享 `PsHardLightBlend` 对象 | 保留别名测试 |
| `PsLightenBlend` | 3184 | 有 | 已映射：`PsLighten` | P1B 本地完成；Apple/真机待验 |
| `PsMulBlend` | 3110 | 有 | 已映射：`PsMul` | 保留并回归 |
| `PsMulBlend_HDA` | — | 有 | 共享 `PsMulBlend` 对象 | 保留别名测试 |
| `PsMulBlend_color` | 3120 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsMulBlend_color_AlphaTest` | 3123 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsOverlayBlend` | 3135 | 有 | 已映射：`PsOverlay` | 保留并回归 |
| `PsOverlayBlend_HDA` | — | 有 | 共享 `PsOverlayBlend` 对象 | 保留别名测试 |
| `PsScreenBlend` | 3219 | 有 | 已映射：`PsScreen` | 保留并回归 |
| `PsScreenBlend_color` | 3228 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsScreenBlend_color_AlphaTest` | 3231 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSoftLightBlend` | 3151 | 有 | 已映射：`PsSoftLight`，三表资源 | P1B 使用真实初始化软件表 |
| `PsSubBlend` | 3083 | 有 | 已映射：`PsSub` | P1B 本地完成；Apple/真机待验 |
| `PsSubBlend_color` | 3091 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSubBlend_color_AlphaTest` | 3093 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `RemoveConstOpacity` | 2683 | 有 | 已映射：`RemoveConstOpacity` | 保留并回归 |
| `RemoveOpacity` | 2619 | 有 | 已映射：`RemoveOpacity`，R8 mask | P1A 同尺寸合法 ROI；缩放/镜像拒绝 |
| `ScreenBlend` | 3047 | 有 | 已映射：`Screen` | P1A 本地完成；Apple/真机待验 |
| `SubBlend` | 2992 | 有 | 已映射：`Sub` | P1A 本地完成；Apple/真机待验 |
| `UnivTransBlend` | 2805 | 有 | 已映射：`UnivTrans` | 保留三源路径 |
| `UnivTransBlend_a` | 2856 | 有 | 已映射：`UnivTrans` | 保留三源路径 |
| `UnivTransBlend_d` | 2832 | 有 | 已映射：`UnivTrans` | 保留三源路径 |

## 附录 B：实施时最容易漏掉的修改点

| 修改 | 必须联动检查 |
| --- | --- |
| 新增 operation kind | 枚举编号、MSL switch、输入判断、统计数组/循环、日志解析、设备替身与 native tests |
| 新增 source 类型 | C2 格式/输入数检查、C3 资源绑定、MSL channel、R8 pitch、错误格式回退 |
| 新增透明度变体 | canonical/alias 对象、SetParameterOpa、HDA/`_d`/`_a`、point/alpha cache invalidation |
| 新增 Gamma/LUT | 参数快照、版本缓存、资源存活、tile/compute 绑定、连续多次编码 |
| 新增几何 | backend 默认能力、manager 路由、坐标/clip、支持域、fallback 安全性、诊断 |
| 放宽 alias | 明确旧图 snapshot 或顺序相关语义，COW/reference，以及部分写入后失败 |
| 优化 blur | 64 MiB 预算、halo、边缘分母、整数溢出界限、偶数核、源目标重叠 |
| 改合成 pass | 目标切换、upload/blit/compute/readback 顺序、呈现/截图、批处理和提交预算 |
| 改资源缓存 | pin/lease、dirty ROI、CPU cache、会话解绑、游戏切换和异步回调 |
| 改 CPU pixel 边界 | 调用栈/用途分类、完整覆盖证明、ROI、source/target 驻留、CPU cache、COW/lease、alpha cache、同步 wait 与回退可见性 |
| 合并 tiny upload | dirty-rect 顺序、同帧可见性、中间读取、staging 生命周期/背压/上限、encoder/submit 预算、场景切换与跨会话销毁 |
| 改传输诊断 | 按 origin 聚合与 texture 明细同时保留、calls/bytes/wallNS/waitNS、一致的 origin 名、`other` 上限、关闭诊断后的时序不变 |

[k-register]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2587-L3307
[k-readback]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2114-L2143
[k-fetch]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2175-L2208
[k-geometry]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L3759-L4032
[k-interface]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/RenderManager.h
[k-tests]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl_test.hpp
[c-mapping]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L2705-L3122
[c-manager]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/MetalLayerRenderManager.cpp
[c-metal]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/backend/MetalRenderBackend.mm
[c-shaders]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/backend/MetalLayerShaders.h
[c-software-geometry]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L3674-L5030
[c-imageutils]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L244-L539
[c-operation]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/LayerRenderOperation.h
[c-interface]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/TVPCompositor.h
[c-render-interface]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.h
[c-blend-bindings]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/gl/blend_function.cpp
[c-tvpgl]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/gl/tvpgl.cpp
[t-layer]: ../Tests/MetalLayer/MetalLayerTests.cpp
[t-layer-cmake]: ../Tests/MetalLayer/CMakeLists.txt
[t-backend]: ../Tests/MetalRenderBackend/MetalRenderBackendTests.cpp
[t-ci]: ../.github/workflows/ios.yml
[d-third]: THIRD-ROUND-LAYER-PERFORMANCE.md
[d-followup]: FOLLOWUP-LAYER-PERFORMANCE.md
