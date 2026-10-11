# 普通 Layer 专用管线与 binder 临时图优化

2026-10-11。本轮实现 Copy / Fill / CopyColor / 完整不透明度 HDA Alpha 的专用片元管线，以及普通无缓存 binder 的无用临时图消除。改动留在工作区；开始时主仓库兼容性计划已有的设备分析修改保持原样。

当前结论：Windows portable 回归通过，Apple 原生编译、GPU 像素比较及 iPhone 性能/温升配对待验。不以着色器长度、逻辑像素占比或 portable 结果承诺加速倍数。

## 1. 专用片元的支持域

`core/render/backend/OrdinaryLayerPipeline.h` 在资源、格式、几何、初始化及 alias 检查之后选择管线：

| 操作 | 条件 | 片元路径 | 写通道 |
| --- | --- | --- | --- |
| Copy | flags=0 | 现有 `layerSample`，无目标颜色输入 | RGBA |
| Fill | flags=0，已有 clear/省略规则未接管时 | 单色，无目标输入、源采样或通用 switch | RGBA |
| CopyColor | flags=0 | 现有 `layerSample`，无目标颜色输入 | RGB |
| Alpha HDA | kind=Alpha、opacity=255、flags 恰为 HOLD_ALPHA\|FULL_OPACITY_BRANCH | 目标 RGB fetch 与单条整数公式 | RGB |
| 其它已接受操作 | 原支持域 | 原通用片元 | RGBA |

HDA 保留 `d.rgb + (((s.rgb-d.rgb)*s.a)>>8)`。不把 alpha=255 直接改成 Copy，不把负数算术右移替换成截断除法，不使用固定浮点混合。Copy/CopyColor/Alpha 沿用软件兼容采样器，包括等尺寸、缩放、反向、裁剪、线性外推及 byte 转换。CopyColor 的逻辑 readsTarget、初始化、COW 与 alias 契约仍保持原状；RGB 写掩码只消除片元程序的显式目标输入。

所有输出保留原 `raster_order_group(0)`。同目标切换 PSO 不关闭 render encoder；延后 Fill 物化时重新绑定 Fill PSO，后继立即绘制再绑定自己的 PSO。原 full Fill→load clear、Fill 省略、目标切换、source alias snapshot、compute 回退、提交及呈现策略不变。

`MIKAGE_METAL_LAYER_SPECIALIZED_PIPELINES=0` 恢复原通用 tile 管线，可与默认开启状态配对。compute 路线不使用这些专用片元。专用 library 编译失败时重试不含专用入口的原 tile library；单个专用 PSO 不可用时也回退通用 tile。原生测试要求符合条件设备的四个专用 PSO 全部可用，不能把这种回退当作专用路径验证成功。

## 2. 实际执行计数

`GetLayerPipelineStats()` 按 generic / copy / fill / copyColor / alphaHDA 分别累计实际编码的 shader draws 与矩形覆盖像素。load clear、未物化的 deferred Fill、已 omitted Fill 都不进入这些计数。它们不表示 DRAM 流量、实际 GPU invocation 数或某条管线的耗时。

诊断开启时，后端正常销毁、提交完 pending work 后输出一条 `metal.layerPipelines version=1 final=1`，使用现有热点输出预算。`availableMask` 的 bit0..4 分别对应上述五条管线；完整 tile 专用支持为31，仅 generic 为1，compute 为0。`enabled` 表示 tile 初始化时是否请求专用路径；是否真正命中还须看 availableMask 与各类 Draws。输出可能受已有预算抑制，缺失不能当作零命中。计数覆盖该 backend 生命周期，不从跨游戏累计值推导单游戏占比。

旧 `rectPixels` / kindPixels 仍是逻辑操作口径，不能直接替代这些实际执行计数，也不能把整个 command 的 GPU 时间按像素比例分摊。

## 3. binder 支持域

`tjsNativeLayer.cpp` 的普通 `Draw` 与 `InternalDrawNoCache_CPU` 重叠区域路径，在以下条件同时成立时不申请/复用自己的临时图，也不对它做透明 Fill：

- Type 为 binder 或当前退化到 binder 的 effect/filter，且 DisplayType=ltBinder。
- 没有 MainImage、没有 cache、未处于转场。
- 没有 TransSrc / TransDest，也没有两个转场 handler。

子层仍经原 GetDrawTargetBitmap / DrawCompleted 转发到父目标；更新区、坐标、遍历顺序及异常传播不变。保留 CopySelf 对 image-less alpha presentation 的 CaptureAlphaForPresentation 副作用。普通 Alpha/Opaque 图层、缓存、转场和其它排除域继续走原路径，不增加 effect/filter 功能。

这消除的是满足条件的更新区临时图工作。临时池本可复用，更新区也未必整屏；不将本轮实现描述成每帧省掉一次整屏新分配。

## 4. 验证

Windows 已运行：

- `Tests/MetalRenderBackend` 构建与4项 CTest 全部通过。新测试检查严格路由、逻辑 alpha 契约和实际执行计数格式。
- binder fixture 提取并执行生产 Draw、CPU no-cache、DrawSelf、CopySelf、目标取用/完成回调以及真实子层遍历宏。覆盖三种 binder Type、裁剪/坐标/顺序、缓存/转场/handler/源目标/MainImage 排除域、普通 Alpha/Opaque、嵌套临时池层级、重复绘制及子层异常。独立 `-Wall -Wextra -Werror` 构建运行通过。
- 新原生测试正文通过 Windows C++ compile-only 检查；这不编译 Objective-C++ 或 MSL，也不执行 GPU。
- `Tests/MetalLayer` 的9项 portable CTest 全部获得通过结果。首轮5项因 sandbox 默认临时目录无法写入失败，设置 TEMP/TMP 到工作区后仅重跑失败项，全部通过；核心软件/设备替身比较在首轮已通过。
- 主仓库及嵌套 cpp `git diff --check` 通过。

可复现的 Windows 命令：

```powershell
New-Item -ItemType Directory -Force build/test-temp | Out-Null
$env:TEMP='D:\vn-sim\build\test-temp'
$env:TMP='D:\vn-sim\build\test-temp'
cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel 4
ctest --test-dir build/metal-render-tests --output-on-failure
cmake -S Tests/MetalLayer -B build/metal-layer-tests
cmake --build build/metal-layer-tests --parallel 4
ctest --test-dir build/metal-layer-tests --output-on-failure
```

Apple 待验矩阵已接入现有 workflow/test targets：

- backend 的原生 suite 运行专用开关关/开 × Fill 合并关/开。检查同一 encoder 内 CopyColor→Fill→HDA→通用 FillMask→Copy→通用 Alpha 的精确 bytes、submit/wait 数，以及 masked PSO 后 pending Fill 在提交边界的物化。
- 强制核对 availableMask=31，并精确核对各 PSO 的实际 draw/pixel 计数；检查 load clear 不计 shader hit、omitted Fill 不计实际 draw。
- HDA 边界 `(d,s,a)=(0,255,255)→254`、`(200,0,1)→199` 及目标 alpha 哨兵；专用/通用路径的 nearest/linear、缩放、反向源矩形和裁剪逐字节比较。
- 已有 native 初始化、alias、pending Fill 拒绝/读/提交/销毁及 Layer 软件参照矩阵继续运行；Layer suite 新增 generic-only CTest，与默认专用和强制 compute 分开。
- release performance 阶段新增 generic-only tile 对照；保留默认专用与原 compute 结果。runner 无 Metal 设备时的77/skip不构成原生签收。

完整 iOS/App 编译、游戏输入/alpha 命中测试、缓存/转场/前后台与 A→B→A 生命周期仍需要 Apple/设备验证。性能验收应固定设备、包、分辨率、存档/输入、时长与 nominal 热状态，独立比较 fragment/command/frame 耗时及温升；不改 FPS cap，不从单次 CI 或有界 command 样本推断 iPhone 功耗收益。
