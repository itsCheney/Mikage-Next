# P2D 首轮：普通 Layer 归因与延迟初始化

2026-10-10。本地实现及 Windows portable 验证完成，未提交、未推送。实施起点三仓干净：Root `17d0773a528a59311de305b28c925c34899f2fbc`、Runtime `3a8179cfb8f24e0f4456aa5e98f5c45844442f51`、Core `484e92734f46accf3af50fb8ce79a3b3dbe3690f`。

这是 D0 与保守 D1 的实现记录。Apple Objective-C++/MSL 编译、原生 tile/forced-compute、App device/simulator 和 nominal 真机 A/B 尚未完成；不宣称 GPU 性能、功耗或道路热点资产归因已经签收。D3 行为没有修改。

## 修改前证据

原始输入为 `perf_log/After_P2C_C3/` 三份 iPhone JSONL，revision `80812c438037`，早于本轮起点。天使纷扰日志 `81620A42` 的 2244–2246 行记录 command 2822：301 render encoder、445 Layer rect、73,703,780 logical rect pixels；Copy 35,871,516、Fill 36,921,576，两者合计约98.76%。GPU command 52.114ms；stage status=partial、droppedPasses=173，故43.562ms otherFragment仅为已采集区间，不能视为完整 fragment 成本。

已有同目标 ordinary render/serial compute encoder 复用。主要实现候选是 `CreateLayerTexture` 单独 clear 引起的 pass 中断，以及可证明全通道完整覆盖的 Fill。本批旧日志没有 D0 身份映射，也不是受控性能配对。

## D0 改动包

`LayerHotspotContext.h` 保存稳定 Layer ID、texture/contentVersion、session、创建 Layer、来源父纹理/父session、角色及有界资产路径。完整规范化路径的hash区分截断后同前缀；成功 decode、缓存命中均登记资产。AssignTexture继续共享同一对象，COW复制保留来源，IndependNoCopy明确标为discarded并清除资产继承。资产表示来源，不能解释为合成后每个像素都属于该图。

Layer 的 load/cache/draw/drawCompleted/transition 入口设置可嵌套作用域。backend resource generation与原texture ID关联；内容失效时立即更新版本，直接读回也能看到最新版本。没有TJS注册变化、Swift/C profile结构变化、对象指针持久化、资源强引用或为取名新增GPU操作。

`metal.layerHotspot version=1`保存有序操作及目标/源、写前版本、Layer、rect/sourceRect/clip、逻辑像素、缩放、alias、snapshot、完整覆盖和已知不透明性。`metal.layerPasses version=1`为backend epoch内累计kind像素、render/compute/blit/draw、clear来源、encoder结束原因、初始化省略/融合及fast Fill总账。普通rect和affine的像素是有效ROI/包围矩形口径，不是测量得到的fragment invocation；其它消费者保留依赖事件，不混作rect像素。

每秒至多采集一个command；热点标记门槛为八屏逻辑像素、32 render encoder、16.7ms GPU command、8ms实际nextDrawable等待或33.3ms帧间隔。command首尾render frame一并保存；帧间隔不等于CPU frame wall。nextDrawable使用当前呈现实际等待，未观察到drawable时分析器输出null，不混入in-flight queue等待。

每个样本最多256操作、64资源。JSON使用base64分片通过原生日志传输，单条不超过960 UTF-8 bytes；所有P2D输出共用32KiB滑动一秒预算。详情容量、输出删减、缺分片、未知关联、stage溢出/失败均显式标不完整；未采样command仍进入累计总账。分配或格式化失败只丢诊断，不中止渲染。completion仅保留冻结CPU样本和输出预算器。

原`metal.gpuCommandBuffer`、`metal.layerWork`、`metal.gpuStages`新增兼容的尾字段`p2dEpoch`，避免同一进程重建backend后重复command ID交叉关联。GPU stage仍依赖既有采样机制，不新增query、readback、submit或wait，不将重叠stage相加。

分析器：

```powershell
python scripts/analyze-layer-hotspots.py <device.jsonl> --output build/p2d-device.json
```

输出源SHA256、总账差值、clear/pass来源、资源贡献下界、有序依赖、完整性及原GPU observations。旧日志明确available=false。缺final表示累计尾部不完整；首条累计值可能包含日志开启前工作。详情有截断时，不能将保留下来的操作序列当作完整重放或据此删绘制。

## D1 改动包

仅`CreateLayerTexture`的RGBA8/R8新资源使用逻辑零、尚未物化状态；普通Emote/window `Create`仍立即初始化。backend创建时读取`MIKAGE_METAL_LAYER_LAZY_INIT`，默认1；置0恢复原创建clear与普通Fill draw，用于同构建A/B。该开关独立于诊断开关。

- 首次ordinary tile绘制使用同一个pass的LoadActionClear；compute及其他读者保守物化。创建未使用Layer不编码clear，未使用销毁不需清屏。
- 验证通过的full upload/full GPU blit编码成功后取消初始化；partial update/copy保留区域外零值。alias先初始化旧源再snapshot。
- 满幅RGBA Fill、flags=0、没有同目标活跃pass时以准确RGBA clearColor打开ordinary pass，后续draw复用。已有同目标pass保持原draw；FillColor/FillMask/FillBlend不替换。没有引入DontCare或更改MSL像素公式。
- 同步/区域/异步读取、copy/snapshot、window/capture、mesh/mask、rect、affine、perspective、span、shrink及transition消费者保护pending状态。绑定目标本身不强制初始化。
- 快速Fill打开pass后，真实draw重新绑定当前alpha-table buffer；table替换不结束pass。保留COW、lease、cache失效、contentVersion及写后异常不重放规则。

逻辑操作/kind像素仍记录Fill一次；实际shader draw、load-action clear、clear bytes及初始化事件单独计数。降低计数本身不构成性能证据。

## 本地验证

- MetalLayer CTest **9/9**：新增独立context测试；真实bitmap COW断言稳定session/texture/creator、父纹理、资产hash和版本。已有像素、lease、alias、transition、消费者及解析回归通过。
- Backend CTest **3/3**：生产初始化判定helper；生产C++ Trace抽取→编译→真实recorder/formatter→parser；分片、overflow、未知身份、累计回归、final、代际、command ID重复隔离及严格输出预算。
- 四个生产TU syntax通过：MetalLayerRenderManager、LayerBitmap、TVPGraphicsLoader、tjsNativeLayer。
- frame-time、point-trace debug/release、session-exit和生产graphics-load/cache检查通过；图像load harness抽取真实资产标记函数，并验证成功、缓存命中和失败不误标。
- 原生D1测试正文在Windows编译为未链接object，仅验证C++接口。新增Apple用例覆盖零/区域/异步读取，RGBA/R8部分写边界，完整上传/复制，alias，满幅Fill的256种alpha精度，连续Fill，Fill→table替换→blend，创建无关资源不破坏pass及诊断开关submit/wait一致性。

首次测试遇到Windows沙箱临时目录权限错误；TEMP/TMP改到workspace的build目录后通过。最终结果以`build/native-metal-p2d-baseline/`为准；此前失败日志不是像素失败。没有在本机运行Apple backend。

## 原生与设备签收

Apple沿用现有CTest的tile/forced-compute与MTL_DEBUG_LAYER=1，以及App device/simulator构建。无Metal设备的skip不视为通过。

设备主回归为天使道路滚动，辅以千恋高Copy/Fill场景。先固定并登记设备/OS、三层HEAD、包哈希、分辨率、存档及输入步骤；每组从相同预热和nominal热状态开始，同一构建用lazy=0/1完成至少三组配对。保存完整计量窗口和沿途thermal，热状态升高的窗口另报。

先从D0建立实际资产→Layer→operation映射和可重放最小序列，再对画面/alpha/clip/顺序/COW/alias逐像素验证。报告逻辑kind像素、实际clear/pass/draw、snapshot、GPU command及有效fragment样本、CPU frame wall、帧时间p50/p95/p99、长帧、nextDrawable与gpuSyncWait。只有同条件重复样本证实改善才签收D1性能；不计入C3/C1/C4收益。

D0设备归因、D1原生正确性及D2性能配对继续待验。D3零帧转场、静止present、跨目标重排、道路专用路径和一般dirty ROI优化均不在本轮实现中。
