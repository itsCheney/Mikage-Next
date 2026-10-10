# P2D 收口与 CPU 长帧治理：实现与待验基线

2026-10-10—11。起点为干净的 Root `4fc5f226c9505a3b64c04973ffc61727b7a0c18e`、Runtime `a0fe418d112b2d87982c0df118b1913859d7c20d`、Core `f29b3e9455e9855a470a123ee54b0182158c050f`。本轮不提交、不推送；变更保留在三仓工作区。

实施顺序为 D0 → D1 → D3 → CPU → D2。本记录区分实现、portable 验证、原生正确、设备路径命中与性能签收。Windows 结果不代表 Apple Objective-C++/MSL 编译或 iPhone 性能。

| 改动包 | 实现与 portable 验证 | Apple 原生正确 | 本次设备命中 | 性能签收 |
| --- | --- | --- | --- | --- |
| D0 v2 归因 | 已接入；v1/v2 emitter→parser 与旧日志回归 | 待验 | v2 待新日志 | 不适用；还须测诊断开销 |
| D1 单槽 Fill 合并 | 已接入；覆盖政策、原生测试正文编译 | tile/compute 像素与 Metal validation 待验 | 新合并路径待验 | 待 nominal 配对 |
| D3 转场缓存 | 已接入；抽取真实 lease/materialize/stop 生命周期测试 | 完整 begin/Draw/Complete/重入/OOM 路径待验 | 待验 | 待配对 |
| D3 静止 present | 已接入；真实确认策略、16 项上限与迟到回调测试 | 真实 drawable acknowledgement 待验 | 待验 | 待配对 |
| CPU 缓存、预取、调用归因 | 已接入；真实 holder、异步完成分支、缓存与 KAG 生命周期测试 | host 编译及真实 codec 对照待验 | 待新调用日志 | **CPU 长帧仍未签收** |
| D2 | 分离指标、开关和配对要求已登记 | — | 无本轮配对数据 | 待验 |

## 输入证据与长帧边界

`perf_log/After_P2D_1/` 的三份日志均为 Root `4fc5f226c950`，只能证明首轮 lazy-init/clear 路径，不能证明本次 Fill 合并、D3 或 CPU 改动已经运行。完整分析保存在 `build/p2d-1-analysis/`；本次 v1 解析回归为 `build/native-metal-p2d-closeout-baseline/v1-regression.json`。

| 游戏 | 有效 runtime step 间隔 | >33.3ms | >50ms | >100ms | 间隔 p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 千恋 | 2,790 | 159（5.70%） | 62（2.22%） | 14 | 65.93ms |
| DRACU | 2,181 | 109（5.00%） | 48（2.20%） | 15 | 72.89ms |
| 天使 | 5,289 | 673（12.72%） | 205（3.88%） | 17 | 80.16ms |

这些是完整 `frameSamplesNS` 的 step 完成间隔，不是每秒 GPU 样本推算，也不是真实 drawable 显示帧率。道路约 +48–51s 的 44 个间隔中有 36 个 >50ms，而脚本单次 max <1.2ms、resourceLoad/imageDecode/GC 为零；结合 GPU/fragment/drawable 证据，该段主要指向 GPU/呈现回压。

道路资产完整路径 FNV1a hash 已与源纹理 generation 12533、texture 12503、2120×1280 精确对应，hash 为 `15177620565288462977`。command 2270/2283 分别为 GPU 174.516/127.750ms、有效 fragment 93.974/69.939ms，drawable wait 29.495/65.324ms；20 个逻辑 rect、约31.36M像素、9 render/1 blit。目标 generation 12529 同一 encoder 的首个满幅 load clear 保留，其后的 Fill shader draw 被随后的非 alias 满幅 RGBA Copy 完整覆盖，构成本轮 D1 的具体依据。

另一类长帧是隐藏跳读：DRACU 的 CPU wall 396.242/943.430ms 与 script max 395.913/941.488ms，千恋为630.017/627.383ms。对应脚本日志明确为“到下一选项的隐藏跳读”。图片解码分别约0、45.5、2.4ms，不能解释全部长调用；旧 `scriptStorage=0` 也不能排除 KAG 场景读取。这些包含原生调用、嵌套执行和等待的墙钟时间不能称作纯 VM CPU。

三份日志热状态与窗口不匹配，尚无 nominal 三组同构建 A/B，因此不报告本次性能改善百分比。

## 01：D0 v2

核心文件为 `LayerHotspotContext.h`、`LayerHotspotDiagnostics.h`、Layer/Bitmap/LayerEx 原生入口及 `scripts/analyze-layer-hotspots.py`。

- 展示归档内路径；过长路径优先展示文件名。完整规范化路径 hash 不变。`displayShortened` 与真正的 `assetTruncated` 分开，正常缩短不再使样本不完整。
- 真正的 native Layer、Bitmap、LayerEx receiver 使用稳定 ID、类型及可用 Layer ID；无关联明确保留零/unknown，不以分派对象地址猜测 Layer。作用域恢复支持嵌套与异常，不持有对象。
- v2 保留 v1 行前缀，追加 receiver、flags/color/opacity/sampling、logicalID 与 immediate/deferred/materialized/omitted。分析器兼容 v1，并明确 `actualDrawsAvailable=false`，不把旧日志缺少的实际 draw 当作零。
- `calls/kindPixels` 保留逻辑工作；`actualDraws` 只在真实 render draw 编码处递增，clear/pass、deferred/materialized/omitted 分开累计。旧 `draws` 是历史编码事件口径，包含 compute；不能与 `actualDraws` 相加或跨版本混比。
- 沿用256操作、64资源、960-byte单行和32KiB滚动一秒预算；未采样工作仍累计。缺分片、超限、缺字段、未知映射与不完整 stage 继续显式标记。不增加 query/readback/submit/wait 或 GPU 资源强引用。

## 02：D1 单槽 Fill

核心文件为 `PendingLayerFill.h` 与 `MetalRenderBackend.mm`。`MIKAGE_METAL_LAYER_COALESCE_FILL` 在 backend 创建时读取，默认1，独立于诊断与 lazy-init。

ordinary tile、flags=0 的 Fill 完成原校验、资源准备及 encoder/binding 设置后，仅延迟最后一次 draw。每 backend 最多一项，保存参数副本和目标句柄；不延迟 encoder 创建。compute、特殊 flags、glyph 特殊提交域维持原编码。

同目标后续 flags=0 Fill 的 clip 完全包含待执行区域时可替换；非 alias、验证通过的满幅 RGBA Copy 可以省略前项。替代绑定以及 Copy draw 成功前保留旧项，异常/拒绝不会提前撤销旧写入。其它操作先物化，再改绑定。

read/async read、source/mask、snapshot/alias、COW/lease、upload、目标切换、encoder end、submit、capture 与 destroy 通过初始化/encoder边界物化。未使用新纹理仍无需清零。逻辑 contentVersion、dirty、缓存失效与返回值在原调用处更新；首个 load clear 保留，不引入 DontCare，不改 framebuffer-fetch 像素公式，不重放已编码后的异常操作。

测试正文 `NativePendingFillTests.inc` 覆盖 Fill→Fill→Copy、partial Fill→full Fill→partial Copy、读边界、参数拒绝、alias、提交、销毁重建以及开关0/1。它在 Windows 只编译，实际 Metal 像素/机制须在 Apple 执行。已有真实 facade COW/lease/异常回归保留。

## 03：D3 固定开销

转场缓存只在 Native Metal composition、withchildren、原无缓存且count=0时进入 pending。begin 立即注册 compact hook、建立逻辑 lease 和 dirty；首次 Draw/Complete 或普通缓存 lease 物化。stop到0且未消费则取消。已有缓存、多租约和其它 backend 保持原路。新 cache 在分配与 dirty 建立成功后发布；首次消费失败原样抛出，保留 pending/lease 以便清理或重试。分配失败从 begin 移到消费是本轮已批准的行为边界。

`StartTransition` 除上述两个 lease 调用外保留原 provider、TransSrc/TransDest、tick、continuous hook、Update(true)、exchange/swap、同步事件及回调顺序。`TransitionCacheTests` 抽取真实缓存方法和 InternalStopTransition，覆盖 OOM、重试、resize、取消、普通第二 lease、交换、事件重入和非Metal/禁用门控；完整 provider/start 与原生绘制仍待验。

静止 present 为所有 backend Resource 增加独立 lifetime ID/mutation version，保守递增所有写入口。签名比较完整有序窗口列表、源资源及版本、几何、输出尺寸和 epoch。仅最新真实显示且 command 成功的签名可确认；有更新在途帧、新 alpha ticket、强制刷新、未知资源或超限均正常 present。关闭优化时不分配签名/确认记录；确认元数据 OOM 时失效并退化为正常呈现。私有确认记录最多16项，只保留元数据，迟到回调不持有 Impl/纹理。

skip 仍调用离屏 Submit，runtime tick/输入/脚本继续执行。只省 drawable 获取、窗口 draw 与 present。resize/expose、前后台/隐藏、session/backend切换、capture和失败失效确认。simulator 缺少真实 presented acknowledgement 时保守关闭，不用 command completion 冒充显示证明。`NativeStaticPresentationTests.inc` 需要可见窗口和真实确认；unsupported/skip 不签为原生通过。

独立开关为 `MIKAGE_METAL_DEFER_TRANSITION_CACHE` 与 `MIKAGE_METAL_STATIC_PRESENT`，默认1，backend/session初始化读取。`metal.staticPresentation` 输出跳过/拒绝及转场缓存 eligible/materialized/cancelled 累计。

## 04：CPU 缓存、touch 与调用归因

`CPUFrameDiagnostics.h` 在 host 事件处理前建立 epoch+stepID，覆盖event、pending input及iterate。VM调用保存callID/parentCallID；在返回或异常展开、tracer pop前完成捕获，并在 ExecuteCode 的局部 code pointer 失效前冻结位置。新增栈API写入调用方固定缓冲，最多4层/512 bytes，不构造完整长路径字符串。

图片 load/cache/decode、script storage，以及三套 KAG 的load/read/cache hit/miss、实际label build和GetNextTag分别聚合；不保存最终tag或表达式结果。≥16ms详情最多8条/窗口、4条/step，独立8KiB滚动一秒预算，单行≤960 bytes。eligible/drop、栈/日志失败和测量到的诊断构造、记录及输出成本累计；summary输出成本进入下一窗口。微小计时本身仍须由诊断开关配对测开销，不能从计数推断零成本。

图像缓存三个入口共用成功提交和插入后裁剪，replacement先扣旧字节，大小账使用64位。单图超限照常返回但不入缓存；Bitmap查询只接收真实CPU表示，texture-only明确miss，不读回。CPU缓存转纹理成功后释放CPU副本，失败保留可重试表示；heap cache行序恢复为逻辑顺序，不长期保存双副本。

touch预取复用原加载线程，仅无owner请求去重；queued/running/loaded一共最多32项，取消项在实际排空前仍占槽，避免跨session堆积。落实调用方字节预算、deadline和session generation；尺寸分配前、逐行及发布前检查有效性。容量或预算耗尽后停止继续排队。允许显式 `.png/.jpg/.jpeg/.bmp/.tlg`、normal、原尺寸、无colorkey/mask/province的域；其它域继续需求加载。同步miss不等待；不预测下一资产、不新增线程、不改变普通async请求的每次回调与顺序。

真实 holder/缓存/异步 admission-worker-completion 源码抽取回归已通过，但其边界替身不等于实际PNG/JPEG/BMP/TLG codec逐像素证明。真实codec域对照、host编译与设备隐藏跳读仍待验。

只读分析：

```powershell
python scripts/analyze-layer-hotspots.py <device.jsonl> --output build/p2d-hotspots.json
python scripts/analyze-cpu-calls.py <device.jsonl> --output build/p2d-cpu-calls.json
```

CPU输出按run/epoch/window/step/call关联，检查详情与总账、缺行/丢弃/未知栈。inclusive时间不可相加，不叫纯VM CPU。剩余隐藏跳读成本必须由本构建新调用日志说明；本轮没有强制yield、分帧、降低画质或减少正常绘制。

## 05：验证与 D2 配对验收

Windows验证产物集中在 `build/native-metal-p2d-closeout-baseline/`：MetalLayer 9/9、Backend 4/4（含GPU与CPU emitter→parser）、LayerInput 1/1；13个生产C++ TU syntax、graphics-load、frame-time、三套KAG release/debug与session-exit检查。原生测试正文编译为未链接object；没有在Windows执行Metal backend。

待验清单：

- Apple tile/forced-compute与Metal validation；本次Objective-C++/MSL和host编译；App device/simulator构建。无设备或unsupported的skip保持待验。
- 真实codec PNG/JPEG/BMP/TLG 的同步与touch结果逐像素/metadata对照，尤其透明边界、取消、失败、冷/热加载。
- 固定设备/OS、三仓revision和dirty源码/包哈希、分辨率、存档、窗口、预热及输入，nominal至少三组配对。道路滚动、静止菜单、零帧转场、隐藏跳读及冷/热加载分别登记。
- 隔离开关：Fill合并A/B固定lazy=1、cache=0、staticPresent=0；转场缓存A/B固定其它项；静止present同样单独A/B。诊断开关另测开销，不能把关闭诊断当作缓存收益。
- 独立报告完整GPU command、有效fragment样本及stage完整性、逻辑pixels/calls、实际draw/pass/clear、snapshot、drawable/in-flight wait、CPU调用与frame wall、内存，以及>33.3/50/100/250ms长帧。热状态升高或缺stage单列；不把嵌套stage相加，不用样本推算完整总量。
- 只有匹配新日志证明路径命中和配对改善后才签收性能。即使缓存与GPU机制通过，隐藏同步循环仍占长帧时保留CPU未签收项。
