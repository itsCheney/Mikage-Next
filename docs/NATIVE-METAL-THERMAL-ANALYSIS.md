# 原生 Metal 发热来源分析：pass 碎片化、字形上传与 overdraw

2026-10-09。归属兼容性计划 **P2C C3 / P2D**。本文回答"C0–C2 之后真机游玩仍然发热"的来源问题：按 After_P2C_C2_1 五份 iPhone 日志（`sourceRevision=f20d0e10c275`）量化 GPU render pass、blit encoder、像素量、主线程占用与热状态，并把主要开销连回具体调用链。精确计数、分位数、拟合系数及输入 SHA256 见 [observations.json](native-metal-thermal-analysis/observations.json)；跨基线对照见 [observations-all-baselines.json](native-metal-thermal-analysis/observations-all-baselines.json)。两者由 `scripts/analyze-thermal-passes.py` 只读生成，原始日志不改写。

## 1. 结论

1. **发热不来自 GPU↔CPU 回读或同步等待。** 五份日志整会话 `syncWaits` 仅 1–3 次、同步等待合计 3–126 ms、readback 0.04–6.0 MiB、in-flight queue 等待合计 ≤ 0.4 s；C0–C2 针对的传输边界已经收敛，不再是主要开销。
2. **发热来自每帧持续的 render pass 碎片化与全屏 overdraw。** 整个会话（含静止段）平均每个 display tick 25–43 个 render encoder、8–19 个 blit encoder；抽样 command buffer 的 GPU 时间 p50 0.7–8 ms、p90 24–47 ms、p99 35–149 ms，其中 80–90% 落在普通 Layer 矩形合成的 fragment stage。活动段 `nextDrawable` 等待每秒最高 123 ms，即 GPU-bound 呈现背压。
3. **blit encoder 的 83–89% 是 `bitmap.update`，其主体是逐字形上传。** `InternalBlendText` 每个字形都经单张共享 `_CharacterTexture` → `UpdateLayerTexture` → 新 blit encoder，并强制关闭当前 render encoder；下一个矩形操作再开新 pass。每个字形 = 1 次 blit + 1 次目标纹理整张 tile load/store。这是计划 C3 要求的调用链证据，本文已补齐。
4. **剩余 pass 来自 Layer 树目标切换与纹理创建时的 clear pass；像素量 38% 是 Fill。** 这属于 P2D D0/D1。转场 begin/stop 抖动（每秒 2–9 次、27–90% 以 0 帧结束）每次都分配整幅 cache 并整树重合成，是可证明的冗余候选。
5. **主线程 30–36% 忙**，`script` 阶段 187–257 ms/s；每帧 CPU wall p50 仅 0.3–1.2 ms，但 p95 26–37 ms、p99 51–80 ms，9–14% 的帧超过 16.7 ms。资源加载全部在主线程，是卡顿主因而非发热主因。
6. 以上指标在 Before_C0 → After_P2C_C2_1 的 9 批、49 个会话中没有系统性变化；C0–C2 的收益在回读/同步，不在 pass 与像素量。

## 2. 证据与边界

| 日志 | 游戏 | 时长 | thermal（游戏开始后秒 → 状态） | fps 中位/均值 | 活动秒占比 |
| --- | --- | ---: | --- | ---: | ---: |
| 28FEB878 | 千恋万花 | 64.3 s | 3.1→2 | 55.7 / 50.1 | 94% |
| 411C44A7 | DRACU-RIOT! | 52.3 s | 2.8→2 | 56.5 / 53.0 | 100% |
| 4E019989 | 天使纷扰 | 112.4 s | 3.6→1，35.6→2 | 51.2 / 48.1 | 95% |
| E44F7D75 | 9-nine | 51.0 s | 2.9→2 | 56.0 / 49.0 | 96% |
| EB1D1620 | 天使纷扰 | 99.9 s | 3.6→0，8.6→1，48.6→2 | 55.0 / 49.5 | 82% |

设备为 iPhone 390×844 @3x、drawable 2532×1170、系统 27.0、显示链路固定 60 fps。"活动秒"指该秒 render encoder 数超过 tick 数 1.1 倍或存在 blit；引擎几乎没有真正空闲的秒。四份开局已是 thermal 2，只有 EB1D1620 从 nominal 起步，在游戏开始后约 8.6 s 到 1、48.6 s 到 2。

本批是路径与成本证据，不是受控基线：热状态 2 下的 DVFS 会放大 GPU 时间波动，场景/时长未配对，不能用来比较游戏之间或优化前后的百分比。抽样 command buffer 每秒一个，不能累加成 GPU 预算；各 stage 时间可重叠，份额只作相对比较；`stages` 阶段含同步等待，不能相加成 CPU 时间。

## 3. GPU：pass 碎片化与 overdraw

| 日志 | render enc/帧 | blit/帧 | compute/帧 | gpuMS p50 / p90 / p99 | otherFragment 份额 | rectPixels p50 / p90 / p99（Mpx） | clears p99 |
| --- | ---: | ---: | ---: | --- | ---: | --- | ---: |
| 千恋 | 32.3 | 16.9 | 0.12 | 7.9 / 47.4 / 95.9 | 89.5% | 8.2 / 33.7 / 160.6 | 391 |
| DRACU | 43.2 | 18.8 | 0.21 | 8.2 / 24.7 / 35.3 | 80.7% | 5.5 / 29.6 / 44.6 | 78 |
| 天使 4E01 | 40.9 | 13.7 | 0.35 | 6.5 / 42.1 / 149.0 | 86.3% | 1.0 / 25.2 / 88.2 | 127 |
| 9-nine | 29.6 | 13.4 | 0.18 | 5.0 / 23.6 / 35.1 | 85.9% | 2.8 / 14.5 / 27.6 | 204 |
| 天使 EB1D | 25.4 | 7.9 | 0.24 | 0.7 / 35.9 / 111.5 | 85.6% | 0.0 / 24.9 / 90.5 | 320 |

"每帧"按整会话 encoder 总数除以 display tick 总数计算。command buffer 每帧恰好一个（1.00/帧），所以 pass 数不是多 command buffer 造成的。最重的抽样帧：1874 render encoder / 2037 dispatch / 76.9 Mpx / 125–149 ms；235 encoder / 185.4 Mpx / 118 ms（相当于 89 个 1920×1080 全屏）；另有一帧 195.7 ms 只有 5.3 Mpx，但它紧随 114.8 ms 的 `nextDrawable` 等待之后，包含呈现背压，不作为拟合依据。

**像素构成**（五份抽样合计 4.06 Gpx）：Fill 38.1%、Alpha 26.2%、Copy 19.6%、CopyColor 12.4%，其余各 ≤1.2%。Fill 是最大的单项，与"清屏再绘"模式一致；ColorMap（字形绘制本身）只占 0.2%，字形的代价在 pass 切换而不在像素。

**GPU 时间拟合**（描述性，不是硬件常数）：对 391 个抽样 `gpuMS ≈ 0.057 × renderEncoders + 0.64 × Mpx + 2.8`，R² = 0.73；按会话分别拟合，每 pass 0.049–0.125 ms、每 Mpx 0.33–0.71 ms，R² 0.60–0.92。全部 9 批 3394 个抽样给出 0.054 / 0.62 / 2.9，R² = 0.74。两项同量级：1874 pass 的帧约 70% 成本在 pass，185 Mpx 的帧约 90% 在像素。

**pass 来源拟合**：按秒区间 `passes/帧 ≈ a × blits/帧 + b × computes/帧 + c × meshDraws/帧 + d`。千恋 a = 1.39、d = 7.7（R² 0.96）；DRACU a = 1.26、d = 8.1（R² 0.97）；天使两份 a = 1.08–1.12、d = 9.9–18.6（R² 0.67–0.68）；9-nine 区间内 blit 与 pass 不共变，拟合无效。9 批 3302 个区间合并得到 a = 1.33、b = 23.1、d = 13.75（R² 0.48）。解释：每个 blit 平均额外打断约 1.1–1.4 个 pass；去掉 blit 后仍有约 8–19 个 pass/帧来自 Layer 树目标切换与 clear；每个 compute（blur/shrink）平均伴随约 23 个 pass。

## 4. `bitmap.update`：逐字形上传的调用链

| 日志 | bitmap.update/s | 平均字节 | 占 blit encoder | 累计 wall |
| --- | ---: | ---: | ---: | ---: |
| 千恋 | 735.9 | 1,087 | 88.9% | 843.7 ms |
| DRACU | 819.6 | 520 | 85.7% | 948.3 ms |
| 天使 4E01 | 541.9 | 1,187 | 83.3% | 1,801.2 ms |
| 9-nine | 560.7 | 558 | 88.6% | 502.8 ms |
| 天使 EB1D | 325.1 | 1,125 | 84.3% | 1,107.7 ms |

调用链（已核对源码，不是区间相关性推断）：

1. `Engine/KRKRRuntime/Source/cpp/core/render/LayerBitmap.cpp:2504` `tTVPNativeBaseBitmap::InternalBlendText`：每个字形先在 CPU 栅格化，再 `_CharacterTexture->Update(bp, Gray, pitch, {0,0,w,h})`（`:2568`）。`_CharacterTexture` 是单张静态共享 R8 纹理（`:2502`），逐字形复用。
2. `core/render/MetalLayerRenderManager.cpp:499` `Update()` → `session->backend->UpdateLayerTexture(...)`，来源未标注时记为 `bitmap.update`（`:529`）。
3. `core/render/backend/MetalRenderBackend.mm:1723` `UpdateLayerTexture`：申请 staging、memcpy、`p.Blit()`、`copyFromBuffer`、`endEncoding`；每次调用一个 blit encoder。
4. `:811` `Blit()` 先 `EndMesh(); EndOrdinary();`，无条件结束正在进行的 ordinary render encoder。
5. 字形随后的 `OperateRect` 经 `:857` `OrdinaryRender(target)`：`ordinaryRenderEncoder` 已空，于是 `:955` `Pass(target,false)` 以 Load/Store 新建 render pass。

因此一个字形 = 1 个 blit encoder + 1 次消息层纹理整张 tile load + store；一行 50 个字在即时显示或 skip 模式下就是 50 个 pass。`bitmap.update` 的 gpuWaitNS 全为 0，说明这不是同步问题，而是 encoder 生命周期问题；累计 wall 0.5–1.8 s 为 staging/memcpy/encoder 创建的 CPU 侧开销，不含 GPU 内 tile 流量。其他 `Update()` 调用者只有 `LayerBitmap.cpp:1695` `tTVPBaseTexture::Update`（视频/AMV 等整帧），已由 `AlphaMovie.frame`、`video.frame` 单独标注，不计入此项。

## 5. 转场 begin/stop 抖动

| 日志 | begin/end 对 | 每秒 | 以 frame=0 结束 | canvas |
| --- | ---: | ---: | ---: | --- |
| 千恋 | 226 | 3.5 | 177（78%） | 1920×1080 |
| DRACU | 476 | 9.1 | 428（90%） | 1280×720 |
| 天使 4E01 | 518 | 4.6 | 138（27%） | 1920×1440 |
| 9-nine | 88 | 1.7 | 8（9%） | 1920×1080 |
| 天使 EB1D | 362 | 3.6 | 120（33%） | 1920×1440 |

`core/script/tjsNativeLayer.cpp:7602` `StartTransition` 在 `withchildren` 时调用 `IncCacheEnabledCount()`（`:4207`）→ `AllocateCache()`（`:4162`）：无 cache 时 `new tTVPBaseTexture(Rect)` 分配整幅纹理（后端 `Create()` 附带一次 clear pass，`MetalRenderBackend.mm:987`），并把 `CacheRecalcRegion` 置为整幅；随后 `Update(true)` 触发整树重合成。`InternalStopTransition`（`:7799`）`DecCacheEnabledCount()`（`:4219`）后视 `TVPFreeUnusedLayerCache` 释放或留待 compact。即使转场在同一帧内 stop、一帧都不绘制，上述分配、clear 与整树 Copy/Alpha 仍然发生；抽样里反复出现的"4 个 rect、8.3 Mpx、alias 2.07 Mpx"帧与此一致。该模式来自游戏脚本（KAG 消息层/立绘更新习惯），引擎侧只能降低每次的固定代价，不能改变调用次数。精确到每次调用的像素归属仍需 P2D D0 的采样关联。

## 6. 主线程

| 日志 | 主线程占用 | cpuWall p50 / p95 / p99（ms） | >16.7 ms 帧 | script ms/s | resourceLoad ms/s | 单次最大 |
| --- | ---: | --- | ---: | ---: | ---: | --- |
| 千恋 | 31.5% | 0.72 / 29.2 / 59.1 | 9.9% | 205.1 | 58.1 | script 1,673 ms（scriptStorage 766.6 ms） |
| DRACU | 31.8% | 1.16 / 26.1 / 51.3 | 10.1% | 248.5 | 26.4 | 513.8 ms |
| 天使 4E01 | 35.9% | 0.46 / 37.3 / 79.6 | 13.8% | 186.7 | 26.2 | 1,044.9 ms |
| 9-nine | 31.2% | 0.60 / 25.8 / 62.1 | 8.8% | 257.0 | 109.7（imageOpen 75.2） | 715.9 ms |
| 天使 EB1D | 29.8% | 0.30 / 32.8 / 76.7 | 9.7% | 189.4 | 34.9 | 1,051.2 ms |

主线程占用 = 合并原始帧样本的 CPU wall 之和 / runtime interval 之和。`script` 是 `tjsInterCodeExec.cpp:958` 最外层 TJS 执行作用域，嵌套包含资源加载与同步等待；它持续占到每秒 190–257 ms，但分布是双峰的：多数帧 ≤1 ms，少数帧因脚本存储加载、PNG 解码（`image.codecSlow` 单次 35–66 ms）或整树重合成而超过 50 ms。资源加载全在引擎主线程（宿主主线程），是 p99 与 1–1.7 s 停顿的来源；对发热的贡献约为一个核心的 3–11%，次于 GPU。

## 7. 无条件 60 fps 呈现

`App/Engine/KRKRSession.swift:512` 每个 display tick 调 `MikageKRKRStep()`；`host/MikageKRKRRuntime.mm:590` → `SDL_AppIterate`（`environ/sdl3/sdl3_app.cpp:618`）→ `TVPRenderOnce`（`core/render/TVPCompositor.cpp:635`）→ `MetalRenderBackend::EndFrame`（`MetalRenderBackend.mm:1358`）总是 `nextDrawable` + 窗口绘制 + `presentDrawable` + `Submit()`，与窗口纹理是否变化无关。静止时抽样 GPU 0.3–0.7 ms、CPU 约 1 ms/帧，单独看不大；但它让 GPU/显示管线永不进入空闲，并把上述每一次脚本触发的重绘都立即变成一次 60 Hz 呈现。它是常驻的底噪，不是峰值来源。

## 8. 跨基线对照

[observations-all-baselines.json](native-metal-thermal-analysis/observations-all-baselines.json) 覆盖 Before_C0 到 After_P2C_C2_1 共 9 批 49 个会话。按游戏看整会话 render encoder/帧（批次顺序 Before_C0 → C0 → C1 → C4 → C2A → C2B → C2B-2 → C2B-3 → C2_1）：天使纷扰 38 → 50 → 59 → 41 → 50 → 42 → 52/22 → 49/35 → 41/25；千恋万花 8.5（近乎空闲的会话）→ 30 → 31 → 32 → 41 → 31 → 26 → 25 → 32；DRACU-RIOT! 56 → 32 → 34 → 26 → 36 → 36 → 43；NEKOPARA 52 → 64 → 49 → 38。`bitmap.update` 每秒 71–1,481 次、占 blit 47–96%，在有该 origin 的全部 42 个会话中成立（Before_C0 尚无 origin 统计，但其 blit/帧 2–33 同量级）。49 个会话中 38 个开局即 thermal 2；从 nominal 起步的 8 个里 7 个在游戏开始后 44.6–71.8 s 到 2，另一个（9-nine）到 1。没有任何一批在 pass、blit 或像素量上出现台阶式下降；各批差异与场景/时长差异同量级。

## 9. 与计划的对应

| 开销 | 证据 | 计划归属 | 建议顺序 |
| --- | --- | --- | --- |
| 逐字形 blit + pass 切换 | 第 3–4 节；每 blit 约 1.1–1.4 pass，占 blit 83–89% | **C3**（G3） | 1 |
| Layer 树目标切换、Create() clear pass、Fill 38% | 第 3 节拟合常数项 8–19 pass/帧，Fill 1.55 Gpx | **P2D D0/D1**（H） | 2 |
| 0 帧转场整幅 cache 分配与重合成 | 第 5 节 | **P2D D3**（新增） | 2 |
| 静止帧重复 present | 第 7 节 | **P2D D3**（新增） | 3 |
| 主线程资源加载 | 第 6 节 | P5 低频边界 / 独立任务 | 4（卡顿，非发热） |

C3 的方向：字形纹理改为 shared-storage 的字形 atlas 或环形槽位，iOS 统一内存下 `replaceRegion:` 不需要 blit encoder；同帧字形先写入 atlas，再在一个 ordinary render pass 内连续绘制；跨目标、跨依赖或中间读取仍按原顺序保留。验收用"encoder 数不再≈update 次数"，并报告每秒 update、encoder、pass、submit、CPU encoding 与帧时间，不只报告 MiB。

## 10. 不确定性

- pass/像素的成本分摊来自回归，受热状态与场景混杂影响；不能把 0.057 ms/pass 当成硬件常数。
- 具体哪些 Fill 冗余、哪些目标切换可合并，需要 D0 的 operation→resource→Layer 采样关联；本文只证明总量与来源类别。
- 转场抖动的每次像素代价由代码路径与抽样帧形态推断，未做逐调用归属。
- 性能改善必须在 thermal 0 下同游戏、同存档、同输入三次配对（P2D D2）；本文数字只用于定位与优先级。
