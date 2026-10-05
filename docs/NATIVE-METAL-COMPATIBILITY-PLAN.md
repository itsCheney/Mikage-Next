# 原生 Metal 兼容性补齐计划：对照 Kirikiroid2 OpenGL 的源码审计

日期：2026-10-05。状态：**P0 已完成本地实施与 portable 验证；P1–P5 尚未实施，Apple 原生 Metal 与新真机基线待验证。**

## 1. 决策与完成目标

继续以原生 Metal 为正式后端。Kirikiroid2 的 OpenGL RenderManager 用来发现遗漏的能力、理解成熟的调用语义和构造兼容性样例；当前软件实现提供可执行的像素参照。遇到两者不一致时，先裁决行为，再实现 GPU 路径。

本计划的完成目标分为三个层次，不能以一个“覆盖率”代替：

1. **当前引擎的普通 Layer 能力**：补齐当前已注册软件方法的 Metal 路由及有效参数域，扩大 affine/perspective 的 GPU 支持，减少可避免的软件回退和像素传输。
2. **旧 GL 的额外兼容能力**：对当前未注册的染色、AlphaTest 等方法明确支持契约；只在确认调用方需求后接入。动态 GLSL 不纳入本轮默认承诺。
3. **真机性能与稳定性**：在同一设备、游戏、场景和设置下验证画面、输入、帧耗时、GPU 阶段、内存和长时间运行。GPU 算子覆盖增加本身不能证明持续卡顿或发热消失。


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

## 3. 对原对话结论的逐项核实

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
| 当前显式 GPU mapping 条目 | 41 | 包含部分共享对象的重复名称 |
| 当前有 GPU 描述符的名称 | 46 | 41 条显式映射，加 5 个未列在表中的共享对象别名；均仍有执行限制 |
| 当前软件存在、但无 GPU 描述符 | **24** | 本轮主要算子补齐范围 |
| 旧 GL 注册名称 | 77 | 四类注册入口合并、去重；包括条件分支的名称集合 |
| 旧 GL 名称在当前有 GPU 描述符 | 38 | 37 个直接交集，加 `PerspectiveAlphaBlend_a` 共享对象；其透视执行仍回退 |
| 旧 GL 名称在当前只有软件实现 | 24 | 与本轮 24 个算子缺口相同 |
| 旧 GL 名称当前软件层也未注册 | **15** | 染色/AlphaTest 兼容能力；不能算普通 shader mapping 缺失 |

核对恒等式：当前 `46 + 24 = 70`；旧 GL `38 + 24 + 15 = 77`。两边名称并集 85，见附录 A。

额外 5 个共享对象别名是 `AdditiveAlphaBlend_HDA`、`PsMulBlend_HDA`、`PsOverlayBlend_HDA`、`PsHardLightBlend_HDA`、`PerspectiveAlphaBlend_a`。`AlphaBlend_HDA` 已在显式 41 条中，不再重复加。

**特别注意 `ConstAlphaBlend_SD_a`：**旧 GL 把它注册成 `ConstAlphaBlend_SD` 的同一个对象；当前软件却是两个独立对象、分别调用不同函数。它确实缺 GPU mapping，不能按旧 GL 的别名关系误判成已支持。见 K1 2788–2793 与 C1 2965–2976。

### 4.2 24 个当前算子缺口

| 组 | 方法 | 数量 | 工作性质 |
| --- | --- | ---: | --- |
| 普通混合 | `SubBlend`、`MulBlend`、`MulBlend_HDA`、`ColorDodgeBlend`、`DarkenBlend`、`LightenBlend`、`ScreenBlend` | 7 | 像素公式、opacity/HDA 分支、路由、alias 域 |
| Photoshop 混合 | `PsAlphaBlend`、`PsAddBlend`、`PsSubBlend`、`PsSoftLightBlend`、`PsColorDodgeBlend`、`PsColorBurnBlend`、`PsLightenBlend`、`PsDarkenBlend`、`PsDiffBlend`、`PsDiff5Blend`、`PsExclusionBlend` | 11 | 不同表函数、饱和/取整、源 alpha 与 opacity 顺序 |
| 图像与透明度 | `RemoveOpacity`、`AdditiveAlphaToAlpha`、`AdjustGamma`、`AdjustGamma_a` | 4 | R8 mask、反预乘、Gamma LUT 和参数生命周期 |
| SD 语义 | `AlphaBlend_SD`、`ConstAlphaBlend_SD_a` | 2 | 单输入与双输入执行契约分别处理 |

上述方法已经在当前软件层注册，许多还出现在 `tRenderMethodCache` 的 bm* 对应关系中；它们是明确的功能缺口，不需要先等某一款游戏报错才决定是否实现。

### 4.3 已有能力仍受哪些条件限制

| 能力 | 已有实现 | 仍需处理/验证 |
| --- | --- | --- |
| 普通矩形 | copy/fill、alpha、部分 Ps、灰度、alpha/channel 等 | 格式、缩放、裁剪、镜像、错位别名、持久 CPU 指针的支持域 |
| 双源 SD | `ConstAlphaBlend_SD`、`_d`，同尺寸矩形 | `_a` 缺失；错位源目标别名保留回退；不要与单输入 `AlphaBlend_SD` 混为一类 |
| Universal transition | 普通、`_d`、`_a` 的三源 GPU 路径 | 两个 RGBA 源加 R8 rule、同尺寸与合法源范围；不是所有 CPU transition 都由此覆盖 |
| affine | 两三角形、Copy、单源的兼容路径 | Alpha/Ps 等混合、镜像特例、更广参数域与数值/边缘行为 |
| perspective | 软件执行；方法对象可有 Alpha 描述符 | 无 Metal 几何入口，所有调用仍回退 |
| blur | 两 pass sliding sums；两个名称共用当前软件公式 | 64 MiB sums 限额、无缩放、完整合法 ROI；大图/裁剪等仍可能回退 |
| R8 | mask/rule 资源及特定输入用法 | 不等于任意 Gray/province 目标均支持 GPU 运算 |
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

建议顺序：**P0 → P1A → P2A → P1B → P2B → P3 → P5**。P4 是按调用需求启用的兼容分支。若新的真实日志表明某个未覆盖家族占主要成本，可调整 P1A/P1B 内部顺序。

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

#### A. 普通混合 7 项

- [ ] 实现第 4.2 节普通混合组，分别核对 HDA 和 opacity=255 的具体分支。
- [ ] `MulBlend` 与 `MulBlend_HDA` 不合并成同一个 alpha 结果；普通 `ScreenBlend`/`ColorDodgeBlend` 不复用 Photoshop 版本代替。
- [ ] 将格式、alias、alpha-cache invalidation 与像素实现一起接入 C2/C3，避免“描述符已支持但源格式检查仍拒绝”的半完成状态。

#### B. 透明度、转换与 Gamma 4 项

- [ ] `RemoveOpacity`：按当前软件读取 R8 mask；只改变相应 alpha，验证 opacity 和 mask 组合、字节寻址及与 RGBA source 的区别。
- [ ] `AdditiveAlphaToAlpha`：参考当前软件的反预乘/查表，明确 alpha=0、1、254、255 和 RGB>alpha 情况；不直接照搬旧 GLSL 的 `s.rgb / s.a`。
- [ ] `AdjustGamma` / `_a`：复用软件生成的 LUT/临时数据语义，冻结每次调用的参数；默认不在 MSL 中重新计算 pow 来近似同一张表。
- [ ] Gamma `_a` 单独核对预乘处理、alpha=0、RGB 超出 alpha、输入/输出上下限、COW/reference 和连续更改参数。
- [ ] 避免每次 Gamma 都分配纹理、强制提交或同步；LUT 有内容版本与明确寿命。

#### C. SD 2 项

- [ ] `ConstAlphaBlend_SD_a`：扩展现有双源参数/公式，匹配当前软件 `_a` 版本，不能沿用旧 GL 的名称别名。
- [ ] `AlphaBlend_SD`：当前软件包装器从一个显式 source 和目标旧值调用 SD 函数；按这一单输入契约接入，不强制要求两张显式源纹理。
- [ ] 双源错位自别名的顺序语义未裁决前继续回退；为被保留的拒绝情况写清原因及测试。

修改位置：C1 的参数类与映射、C2 的输入/格式路由、C6 参数结构、C4 像素函数与双源函数、C3 LUT 资源绑定及执行。

验收：13 个名称在声明支持域内 GPU 执行；预先驻留的 RGBA/R8 输入不产生 fallback/readback/整图上传；Gamma 允许必要的小型参数/LUT 上传，必须与图像上传分开统计。离开支持域仍有正确结果及准确拒绝原因。

### P1B：补 Photoshop 混合 11 项

- [ ] 实现第 4.2 节 Ps 组全部 11 项；按公式共享 helper，不按名称数量重复维护 shader。
- [ ] `PsSoftLightBlend` 先核对软件表生成；若用 LUT，在同一提交内保证参数不变。
- [ ] 分别验证 ColorDodge/ColorDodge5、Diff/Diff5、普通 blend/Ps blend，覆盖源 alpha 与 opacity 相乘先后的差别。
- [ ] 扩展 tile、in-place compute、snapshot compute 对同一像素 helper 的调用，以及支持域声明。

验收：与 P1A 合计 24 个缺口均完成矩形支持与反例回退；当前 70 个注册名称都有明确的 GPU 能力描述或经记录的限制。这里不宣布其所有几何和所有输入均 GPU 化。

### P2A：把现有 affine Copy 扩展到 Layer 混合

目标：优先解决现有游戏可到达的 affine quad 回退，不把通用网格设计作为阻塞项。

- [ ] 梳理 `GPUAffineCopy()`、`layer_affine::Prepare()`、`TVPLayerAffineCopy`、`affineCopyLayer` 的现有约束；保留已经验证的 Copy 快路。
- [ ] 建立采样与像素混合分离的 affine 路径，优先 Alpha/ConstAlpha/AdditiveAlpha 及 `_d/_a/HDA`，再扩展已验证的 Ps 家族。
- [ ] 确定目标旧值和 reference 的读取契约，复用 alpha tables 和新的参数资源。
- [ ] 验证旋转、缩放、错切、非整数位置、镜像、源裁剪、目标裁剪与单像素边界；保留软件 scanline 路径不同的场景，直到差异裁决完成。
- [ ] 对普通矩形和 affine 分别定义 StretchType 支持域，不能因为普通矩形已接纳较大的正值，就放宽 affine 的所有过滤器。
- [ ] 在支持 tile 的设备上评估同目标 pass 复用；compute 实现作为正确性路径，不以必须达到 tile 最优性能作为第一次上线条件。

验收：声明支持的 affine quad 与软件逐像素一致；仍不支持的几何有最小反例；无额外 CPU 中转；triangle diagnostics 区分 GPU 成功、方法缺失、采样/几何/alias 拒绝。

### P2B：新增真正的 Perspective GPU 路径

- [ ] 在通用 backend 接口中增加透视描述与能力入口，默认实现返回不支持，避免影响其他后端。
- [ ] 从现有 `GetPerspectiveTransform`/`WarpPerspectiveRGBA` 提取并测试输入坐标、矩阵方向和边界契约；不要为此重新引入 OpenCV。
- [ ] 选择逆映射 compute 或正确透视插值的 raster 路径；先满足当前单输入语义。选择依据是像素结果和样例，不把两三角形 affine 当作透视。
- [ ] `PerspectiveAlphaBlend_a` 已共享 Alpha 描述符；需要补的是执行路由与坐标处理，不是重复注册同名对象。
- [ ] 测试 identity、平移、倾斜四边形、强透视、多 quad、部分越界、退化矩阵、w/分母接近零、反向顶点与纹理别名。
- [ ] 审计现有软件矩形特判及 quad 顶点索引；若参考实现有问题，先记录并修复行为，再把它用于 Metal 验收。

验收：当前合法单输入透视场景留在 GPU；多 quad 不越界、不相互污染参数；覆盖边缘和采样的差异有逐项结论；异常输入不会在部分修改目标后再次软件渲染。

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
- [ ] 检查 CPU 转场、raw pixel 插件、截图、命中测试的实际读源；只迁移已知具体转场，不承诺任意 CPU 插件零回读。
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
| G | 通用三角形与安全支持域 | D、独立参照 | 高；软件回退不通用 |
| H | 按需历史 color/AlphaTest/stencil | 明确调用需求、G 中所需能力 | 高；扩展旧协议 |
| I | snapshot/blur/批处理专项优化和真机收口 | 对应功能包稳定 | 高；收益只能实测 |

每包均包括必要的 production scalar 对照、路由与资源测试、原生 GPU 测试和文档更新。不要把所有 Metal 修改合成一个大提交，再补测试。

跨仓库位置：主要实现属于 core；host 只在新增诊断/配置确有需要时变更；测试/CI/计划属于主仓库。实现完成后的提交和 submodule 指针更新按 core → runtime/host → 主仓库顺序处理。本计划不执行提交或推送。

不在源码静态审计阶段承诺精确人日；最大不确定项是几何兼容裁决、旧接口真实需求及 Apple 设备验证。A/B/C 是确定性较高的首批，F/G/H 需阶段性评估。

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

### 8.4 真机场景与指标

| 场景 | 主要验证 | 基线来源/限制 |
| --- | --- | --- |
| 9nine UI/效果 | Gamma 与 Ps/普通混合、透明和短时尖峰 | D1 曾记录少量 Gamma 回退；需新版本同条件复测 |
| 天使纷扰设置/鉴赏 | Stretch、复制/混合、fallback 和传输 | 已有修复不能列为本计划新成果 |
| 天使纷扰后期剧情 | tile 与 compute 成本、持续帧时间及热状态 | D1 有低回读下 GPU/展示等待证据，不能只看回退数 |
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
- [ ] P3：通用三角形域有独立参照与安全拒绝/回退。
- [ ] P4：逐项标明“未触发/延后/已验收”，不作为当前核心功能完成的隐含条件。
- [ ] P5：Apple 测试与 device/simulator 构建通过，目标真机场景达到预先保存的正确性和性能门槛。

允许声明核心路线完成的条件是 P0/P1/P2/P3/P5 验收齐全，P4 有明确处置记录。允许先发布较小功能包，但只能声明该包已覆盖的输入域，不能写“Kirikiroid 全兼容”。

## 9. 初始审计与 P0 实施状态

初始审计已完成：固定版本源码获取、旧 GL/当前软件/Metal 注册与别名核对、接口及几何/blur/回读审计、现有测试与 CI 阅读、实施依赖和验收计划。

P0 实施已完成：共享操作定义与 traits、无副作用注册审计、85 项能力 fixture/JSON、动态编译失败处理、扩展/边界测试和说明更新；本地 MetalLayer CTest 2/2、MetalRenderBackend CTest 1/1 通过。生产代码为三层基线 HEAD 上的未提交工作区改动，具体证据见 P0 记录。

仍未完成：P1–P5 算子与几何扩展、原生 MSL 实际编译与 GPU 像素、最新 Apple CI/device/simulator 构建、旧 GL 实际运行及新真机像素/性能/热状态基线。保存的历史真机资料不作为当前改动的验收证据。

## 附录 A：完整名称对照

“已映射”只表示名称能取得非 Unsupported 的描述符；实际 GPU 路由还受第 4.3 节约束。几何列中的例外优先于名称状态。

| 名称 | 旧 GL 注册行 | 当前软件注册 | Metal 描述符/差距 | 工作项 |
| --- | ---: | --- | --- | --- |
| `AddBlend` | 2987 | 有 | 已映射：`Add` | 保留并回归 |
| `AdditiveAlphaBlend` | 3013 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaBlend_HDA` | — | 有 | 共享 `AdditiveAlphaBlend` 对象 | 保留别名测试 |
| `AdditiveAlphaBlend_a` | 3018 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaToAlpha` | 3246 | 有 | 无 GPU 描述符 | P1A |
| `AdjustGamma` | 2731 | 有 | 无 GPU 描述符 | P1A |
| `AdjustGamma_a` | 2771 | 有 | 无 GPU 描述符 | P1A |
| `AlphaBlend` | 2881 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_HDA` | — | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_SD` | 2912 | 有 | 无 GPU 描述符 | P1A：单输入契约 |
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
| `ColorDodgeBlend` | 3026 | 有 | 无 GPU 描述符 | P1A |
| `ConstAlphaBlend` | 2709 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_HDA` | — | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_SD` | 2788 | 有 | 已映射：`ConstAlphaSD` | 保留双源路径 |
| `ConstAlphaBlend_SD_a` | 2793 | 有 | 无 GPU 描述符 | P1A：独立双源公式 |
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
| `DarkenBlend` | 3034 | 有 | 无 GPU 描述符 | P1A |
| `DoGrayScale` | 3267 | 有 | 已映射：`GrayScale` | 保留并回归 |
| `FillARGB` | 2633 | 有 | 已映射：`Fill` | 保留并回归 |
| `FillColor` | 2636 | 有 | 已映射：`FillColor` | 保留并回归 |
| `FillMask` | 2639 | 有 | 已映射：`FillMask` | 保留并回归 |
| `LightenBlend` | 3040 | 有 | 无 GPU 描述符 | P1A |
| `MulBlend` | 2999 | 有 | 无 GPU 描述符 | P1A |
| `MulBlend_HDA` | 3006 | 有 | 无 GPU 描述符 | P1A |
| `MultiplyAlpha` | — | 有 | 已映射：`MultiplyAlpha` | 保留并回归 |
| `PerspectiveAlphaBlend_a` | 2923 | 有 | 共享 `AlphaBlend_a` 对象 | P2B：透视执行仍回退 |
| `PsAddBlend` | 3058 | 有 | 无 GPU 描述符 | P1B |
| `PsAddBlend_color` | 3066 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAddBlend_color_AlphaTest` | 3068 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAlphaBlend` | 2917 | 有 | 无 GPU 描述符 | P1B |
| `PsColorBurnBlend` | 3175 | 有 | 无 GPU 描述符 | P1B |
| `PsColorDodge5Blend` | 3168 | 有 | 已映射：`PsColorDodge5` | 保留并回归 |
| `PsColorDodgeBlend` | 3159 | 有 | 无 GPU 描述符 | P1B |
| `PsDarkenBlend` | 3190 | 有 | 无 GPU 描述符 | P1B |
| `PsDiff5Blend` | 3202 | 有 | 无 GPU 描述符 | P1B |
| `PsDiffBlend` | 3196 | 有 | 无 GPU 描述符 | P1B |
| `PsExclusionBlend` | 3209 | 有 | 无 GPU 描述符 | P1B |
| `PsHardLightBlend` | 3143 | 有 | 已映射：`PsHardLight` | 保留并回归 |
| `PsHardLightBlend_HDA` | — | 有 | 共享 `PsHardLightBlend` 对象 | 保留别名测试 |
| `PsLightenBlend` | 3184 | 有 | 无 GPU 描述符 | P1B |
| `PsMulBlend` | 3110 | 有 | 已映射：`PsMul` | 保留并回归 |
| `PsMulBlend_HDA` | — | 有 | 共享 `PsMulBlend` 对象 | 保留别名测试 |
| `PsMulBlend_color` | 3120 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsMulBlend_color_AlphaTest` | 3123 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsOverlayBlend` | 3135 | 有 | 已映射：`PsOverlay` | 保留并回归 |
| `PsOverlayBlend_HDA` | — | 有 | 共享 `PsOverlayBlend` 对象 | 保留别名测试 |
| `PsScreenBlend` | 3219 | 有 | 已映射：`PsScreen` | 保留并回归 |
| `PsScreenBlend_color` | 3228 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsScreenBlend_color_AlphaTest` | 3231 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSoftLightBlend` | 3151 | 有 | 无 GPU 描述符 | P1B |
| `PsSubBlend` | 3083 | 有 | 无 GPU 描述符 | P1B |
| `PsSubBlend_color` | 3091 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSubBlend_color_AlphaTest` | 3093 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `RemoveConstOpacity` | 2683 | 有 | 已映射：`RemoveConstOpacity` | 保留并回归 |
| `RemoveOpacity` | 2619 | 有 | 无 GPU 描述符 | P1A |
| `ScreenBlend` | 3047 | 有 | 无 GPU 描述符 | P1A |
| `SubBlend` | 2992 | 有 | 无 GPU 描述符 | P1A |
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
