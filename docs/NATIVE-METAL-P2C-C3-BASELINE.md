# P2C C3：字形 atlas 与小更新合批

2026-10-10。本地实现及 portable 验证完成，尚未提交或推送。接续主仓库 `875ef9f`、Runtime `f48a68c011c83fa601e301f9759e87a7536c6b07`、Core `31c743e7188c88c9698cf535b0bd9aa3d3706acb`；起始三仓干净。Apple Objective-C++/MSL、App、设备/模拟器及 nominal 真机配对仍待验，本记录不宣称 GPU 性能或功耗改善。

## 实现与兼容边界

`InternalBlendText` 新增可选 `TryBlendGlyph`，只取得一次原 COW 目标。Metal facade 检查当前 session、RGBA GPU 目标、CPU lease、opacity、pitch/尺寸、stretch 与 alpha tables。成功复用普通 Layer 的缓存失效和 GPU 操作计数；拒绝只记优化原因，旧 scratch Update/OperateRect 决定最终执行路线。异常不重放；后端在目标 draw/dispatch 后设置 `targetWritten`，即使后续隐式 Submit 抛错，facade 仍失效目标缓存并只计一次已执行上传。

后端独占 1024×1024 shared R8 atlas 页，以原 cropped bytes 追加快照，立即用原 `OperateLayerRect` 绘制。没有跨命令字形缓存、CharacterData 指针键、LayerTexture 包装或 CPU mirror。原字节解释、四边裁剪、HDA、阴影/正文顺序、布局和 Gray 行为保持；原左裁剪只消费 top 的行为未夹带修复，也未新增 FullColored 支持。

每页只供一个未提交命令写入，所有 Submit 均封存；完成后才能复用。16 MiB atlas 预算按实际 allocatedSize（至少页像素大小）核算，包含 Reset 后尚由命令持有的旧页。Reset 不提交或等待；旧页转入退休账，Completed/Error 后才释放预算，新 session 不取旧 wrapper。shared R8 不可用、页尺寸超限或容量不足时回原路径，不能覆盖未完成槽位。此实现依赖 Apple GPU shared texture 能力；其它设备保留原路径。

普通 Layer 小更新在对齐 staging 数据 ≤64 KiB 时使用 1 MiB arena 页，当前命令的 arena 预留上限为16 MiB。arena 不拿更大的旧池 buffer 充当小页。输入在 Update 返回前复制，切片不覆盖；连续 copy 按原顺序共用 upload blit encoder，重叠 ROI、不同纹理也不重排。render、compute、其它 blit、clear/readback/capture、Submit 与销毁边界关闭上传 encoder。大更新/arena 不足使用既有 staging，原16 MiB有效 staging 字节、2048操作提交预算和64 MiB空闲池保持；arena 上限不等于所有 active/in-flight/idle staging 的合计内存。

## 诊断与复采

C0 仍把每次成功 shared 字形写入记为 `bitmap.update`（或继承明确 origin），使用真实 atlas texture ID/页尺寸和逻辑字节；与 `layerUploadedBytes` 同账。逻辑物化字节不是实测总线流量。false/no-op不虚构上传，优化拒绝不重复计 CPU fallback。

新增 `metal.tinyUploads version=1`，每秒最多一条累计聚合，解绑/销毁有 final。独立 epoch 隔离 backend/session reset；记录 glyph 调用/命中/固定拒绝原因、逻辑 bytes、atlas当前/峰值占用、tiny copy/batch、arena页取得/回退、encoder结束原因及CPU准备/编码时间。计时扣除 Commands 的队列阻塞；诊断不新增提交、等待或逐字日志。

`scripts/analyze-c3-uploads.py` 校验完整字段、路由总账、单调性、预算、final与代际，报告最后累计值及两个已观测 snapshot 之间的差值/速率。首条累计值可能包含日志开启前工作，缺 final 表示尾部不完整；无 C3 事件明确为 unavailable。C0、逐帧 encoder/submit/wait 与热状态仍用原分析器独立核对。

两个验证 SDL hint 默认开启：`MIKAGE_METAL_GLYPH_ATLAS`、`MIKAGE_METAL_TINY_UPLOAD_BATCHING`；置0分别恢复旧字形上传或旧单更新encoder，用于同构建 A/B。`MIKAGE_METAL_DIAGNOSTICS` 仅控制诊断采样/输出。

## 本地验证

- MetalLayer **7/7**：生产 `InternalBlendText/InternalDrawText` 抽取、变化字节与颜色、legacy裁剪/HDA、COW、CPU租约/目标、负opacity、容量拒绝、no-op、提交后异常、C0精确一次与代际；原C0–C2/几何/transition/绑定/解析回归全部通过。
- Backend **2/2**：生产atlas/arena分配器的非重叠、边界、耗尽与owner条件；原transfer/诊断；C3解析7项（含真实生产ReportUploads抽取编译→emitter→parser、开关/限频/累计/final）。
- 三个生产 TU `LayerBitmap.cpp`、`MetalLayerRenderManager.cpp`、`RenderManager.cpp` syntax通过；frame-time、point-trace debug/release、session-exit检查通过。
- 原生 C3 测试正文也在 Windows 作为未链接object进行C++类型检查，**不等于编译或运行Metal backend**。日志在 `build/native-metal-p2c-c3-baseline/`；Windows临时目录权限错误通过将TEMP/TMP设到workspace build解决，不是像素失败。

## 原生与真机待验

Apple CTest已接入普通及forced compute。新增native用例验证32次连续更新共用一个encoder、借用输入立即复制、重叠/交错目标及中间consumer看到正确版本；128字形与旧scratch算子精确对照，tile支持域零字形blit/一个pass，无新增submit/wait。snapshot-compute设备保留必要目标snapshot，只断言比旧路径少128次上传blit。

另覆盖未提交Reset保留资源与物理预算、容量拒绝无提前Submit、完成后恢复。Apple SDK编译、真实共享资源同步、device/simulator、诊断开关submit/wait及App生命周期尚未在本机运行。

千恋/DRACU skip或即时显示分别至少3组 nominal 配对，固定设备/OS、三层HEAD、包、存档、输入和计量窗口，报告update/s、glyph命中/拒绝、blit/render/compute、submit、CPU encoding、帧时间分位/长帧、内存及沿途thermal。不能由portable结果或既有thermal2日志计算改善百分比。C3与P2D overdraw、clear pass、目标切换及静止present分别签收。
