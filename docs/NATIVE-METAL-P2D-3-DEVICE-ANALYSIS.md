# After_P2D_3：CPU v2 与长帧来源裁决

2026-10-11。分析 `perf_log/After_P2D_3` 全部五份日志。工作区三仓干净，Root `302c7bc21718c01bfaf57cced7f436edf333cd29`、Runtime `d5b078165b2308ae9d1890d4936676c85b054ea5`、Core `7dc8af2bd17c9508759311c5502407f85d3ead7b`。本轮只新增分析与更新证据记录，不修改生产行为，不提交、不推送。

本次已确认 PSB 对象树构建与同步销毁是场景长帧的重要成本，同时出现三个独立方向：图片资源打开/格式探测、host 事件/pending-input 区域、NEKOPARA 的 Emote 资源加载。大量短 VM 调用的累计成本、诊断扰动和 GPU 呈现继续分别保留，CPU 与性能尚未签收。

## 1. 输入与完整性

| 简称 | 日志文件 | 行数 | session 时长 |
| --- | --- | ---: | ---: |
| 天使 | `Mikage-diagnostics-F0A8EAF1-1BA8-432A-8DB2-B867C6D4C331.jsonl` | 10,049 | 114.405s |
| 千恋 | `Mikage-diagnostics-382C96B3-2AD1-4FC0-87EC-0DCFC9517BCF.jsonl` | 7,599 | 103.914s |
| DRACU | `Mikage-diagnostics-26C4AE5B-A329-4250-BA37-6784449F55FF.jsonl` | 8,499 | 100.515s |
| NEKOPARA | `Mikage-diagnostics-C3DC223B-D90C-4F23-9C20-9FF335ABC21A.jsonl` | 9,784 | 91.824s |
| 9-nine | `Mikage-diagnostics-E509D2E8-DE5D-4E8B-863C-F776DB05E4DD.jsonl` | 5,936 | 86.600s |

各文件只有一个 run 和一个 game session；sequence 从1连续至末行，无重复、倒序或缺号，均观察到 game.end。sourceRevision 均为 `302c7bc21718`，实际 renderer 为 Metal，drawable 为2532×1170，系统27.0；日志未给出精确 iPhone 型号。CPU v2/PSB/vmCalls 已命中。CPU 与 Layer 两个生产解析器分别处理五文件，均 valid，Layer 分片无缺组。

格式有效与详情完整分别判断：CPU 每窗口最多两step/每step四角色，热点仍受操作/资源/输出/stage预算限制。CPU的drop按原因对账：

| 游戏 | eligible / emitted / dropped | frameDropped / windowDropped | stackFailures | 完整CPU窗口 / 总窗口 |
| --- | ---: | ---: | ---: | ---: |
| 天使 | 4254 / 458 / 3796 | 2516 / 1280 | 18 | 15 / 112 |
| 千恋 | 2158 / 309 / 1849 | 1642 / 207 | 19 | 40 / 101 |
| DRACU | 1182 / 258 / 924 | 759 / 165 | 10 | 58 / 99 |
| NEKOPARA | 5258 / 416 / 4842 | 3515 / 1327 | 30 | 6 / 89 |
| 9-nine | 1552 / 240 / 1312 | 1236 / 76 | 15 | 34 / 84 |

byte/format/callback dropped 与 callbackFailures 均0；栈失败集中开局窗口，缺祖先、跨窗scope与bounded drop仍显式标记。Layer样本完整数为天使64/94、千恋51/82、DRACU54/86、NEKOPARA24/73、9-nine51/78。中间累计报告有预算省略，五份均保留final累计总账，不能用省略样本重建所有操作。

## 2. 长帧仍在，按完整 frame 样本计量

| 游戏 | 非零interval样本 | interval >33.3 / 50 / 100 / 250ms | interval p99 | runtime step wall最大 |
| --- | ---: | ---: | ---: | ---: |
| 天使 | 5698 | 590 / 188 / 23 / 3 | 78.022ms | 967.987ms |
| 千恋 | 5554 | 263 / 67 / 14 / 6 | 52.560ms | 1573.960ms |
| DRACU | 5476 | 202 / 76 / 13 / 6 | 53.306ms | 1041.267ms |
| NEKOPARA | 3715 | 357 / 182 / 82 / 9 | 150.696ms | 497.234ms |
| 9-nine | 4609 | 175 / 58 / 10 / 6 | 52.503ms | 1712.879ms |

所有frameSampleCount与行内样本数量一致，frameSamplesDropped均0。interval排除每session的零历史标记；它与step wall分别计量。表中包含已flush的正常游戏及关闭流程窗口，不补算未flush尾部。CPU StepScope边界略宽于runtime frame wall，例如天使同step为968.497ms/967.987ms；不强行匹配小数。

开局stepID=0的script.storage另有天使1060.562ms、千恋781.516ms、DRACU497.932ms、NEKOPARA1614.152ms、9-nine496.301ms，属于host step之外的启动范围，不混入表中游戏step最大值。

热状态按heartbeat观测：天使首窗0，约+22.556s→1、+42.560s→2；千恋首窗0，+41.223s→1、+61.226s→2；DRACU首窗0，+36.875s→1、+51.871s→2。NEKOPARA首窗2，+10.942s观测0，+35.946s→1、+55.077s→2；9-nine首窗起一直2。快照之间不推断每帧精确温度。不同输入/时长/热阶段不能与After_P2D_2比较原始长帧数或宣称回归/改善；仍无nominal三组固定输入A/B。

## 3. 场景PSB：构建和销毁已经有直接证据

| 游戏 / CPU window | 最重step / scope内VM调用数 | 本窗口PSB load数 | materialize总 / 单次max | destroy总 / 单次max |
| --- | ---: | ---: | ---: | ---: |
| 天使 / 94 | 968.497ms / 157,366 | 1 | 443.413 / 443.413ms | 291.569 / 291.569ms |
| 千恋 / 92 | 667.535ms / 95,553 | 1 | 383.994 / 383.994ms | 104.692 / 104.692ms |
| DRACU / 63 | 1041.676ms / 272,378 | 3 | 389.630 / 238.768ms | 214.379 / 132.757ms |
| DRACU / 54 | 857.078ms / 323,587 | 2 | 274.000 / 138.775ms | 149.278 / 89.721ms |
| 9-nine / 35 | 1713.236ms / 595,702 | 12 | 594.022 / 117.270ms | 352.879 / 69.760ms |

step与选中materialize用stepID直接关联；总数/总耗时是整窗口，不把同窗口所有阶段强行归给一个step。PSB load包含read/decompress/tables/materialize，不与它们相加。VM调用数是调用量，不是指令数或CPU时间。

原始证据：天使8325–8329行保留step4731与materialize栈，8330–8366行聚合，8382行runtime窗口；千恋6753–6757行保留step4967与materialize栈；DRACU5551–5555行保留step3350与materialize栈，5568/5574/5576行给出load/materialize/destroy总账；9-nine2614–2618行保留step1748，2631/2637/2639行给出12次load及阶段总账。

materialize栈明确进入当前包的StorageData→initStorage→loadScene→startScene。千恋5010行另直接记录clearStorage的psb.destroy147.261ms；DRACU1893行为72.686ms。关闭游戏的world.finalize也命中销毁：天使164.498ms、千恋219.555ms、DRACU99.381ms、NEKOPARA210.694ms，需与游戏中的场景跳转分别报告。

PSB自身加载中主要成本落在materialize：全已flush窗口materialize/load比例为天使97.98%、DRACU97.88%、NEKOPARA98.21%；这是PSB load的阶段占比，不能当成整帧占比。天使94的read总8.654ms、open0.007ms、tables0.064ms；千恋92的MDF decompress5.701ms、copy0.462ms、read2.509ms。以MDF解压或磁盘读取作为这些场景的优先优化目标，证据不充分。

生产实现 `plugins/psbfile/PSBFile.cpp:501–539` 递归构造真实Array/Dictionary，通过每元素`add`与每成员PropSet填充。当前可进入构建/释放成本优化审查，保留脚本可观察的对象、属性、异常、finalizer与引用顺序。不能用缓存最终tag、延迟执行脚本或跳过树构建掩盖成本。外部root持有导致的稍后释放仍可能落在psb.destroy之外，原生产所有权回归边界继续适用。

上表几个重窗口syncWaitNS增量均0，queueWait仅亚微秒至约1.3微秒；天使/千恋/9-nine对应Emote增量0，DRACU相关Emote阶段为毫秒级，无法解释整秒wall。nextDrawable及未测native等待另列。

## 4. 图片资源打开：千恋冷UI加载的主要已测阶段

千恋CPU window8 / step278：runtime wall1573.960ms，CPU scope1574.226ms，step内VM25,839次。所在窗口imageDecode67次，总1471.229ms，其中imageOpen1418.549ms、imageCodec41.107ms、imageResolve11.176ms。单次imageOpen最大22.402ms，说明许多较小的打开成本可以在一次同步加载链里累计为秒级卡顿；不能只按≥16ms的最慢单次codec选择优化方向。

原始证据：782–786行step/psb.read详情，822行完整LayerWork；766–767行Auto Path重建16ms；相邻GPU command143的资源映射出现`psb://flowchart.pimg/15262.tlg`、`15259.tlg`和`13733.tlg`。PSB phase栈为psdlayer.loadPIMG→loadImages→UILoader._psdStart/doLine。这建立了本次PIMG域的存在，未证明67次都属于同一父文件。

9-nine也有相同累计模式：window9的32次imageDecode总387.711ms，其中imageOpen321.527ms；window12的25次总374.799ms，其中imageOpen319.605ms。它们与window35的PSB场景批量重建分别治理。

源码边界已核对：`TVPGraphicsLoader.cpp:1129–1132`的imageOpen同时包含holder.Open和QuickTest，不是纯文件系统open；主图和mask均有这个scope。`PSBMedia.cpp:45–66`对子资源每次重新打开父PSB文件、seek并复制chunk；`XP3Archive.cpp:728–785`首次Read可以触发整压缩segment加载/解压，CreateStreamByIndex还可执行content filter及全量读取。它们是下一次拆分的候选机制，当前日志没有archive/filter/PSBMedia的逐次耗时，不能宣布已证明反复整包解压或据此直接扩大缓存。

下一包应在既有imageOpen内区分stream创建、PIMG父文件/segment读取与format probe，记录有界资源/父关系和实际字节，保持filter、副作用、session取消及内存预算。不把文件后缀当真实codec：本次部分`.png/.tlg`资产的image.codecSlow实际报告`.webp`。

## 5. 其它已观察到的成本，分别保留

**host事件/pending-input。** 天使9740行runtime.slowFrames为wall112.289ms、event111.496ms、iterate0.792ms、events0；9746行同一step5502的CPU wall112.290ms、VM22次，residual112.136ms。host `recordFrame`在更新最大wall时同时保存event/iterate/count，保证四个peak字段描述同一step。`MikageKRKRRuntime.mm:599–643`的event区域包含pollKRKREvent/SDL事件泵与pending pointer处理，events0不代表事件泵和pending处理没有工作。当前无法再拆分这111.496ms，不能称为纯VM或PSB成本。下一次补host poll/pending-input的低频阶段证据，保持输入FIFO与alpha ticket语义。

**Auto Path重建。** 天使9171行记录58,792个文件扫描、57,891个激活，52ms；其它后段多次44ms/37ms。源码`TVPStorage.cpp:842–945`在storage critical section内枚举/重建路径表；当前只证明重建慢，未证明失效多余。需结合触发者和路径代际排查，不移除必要失效。

**NEKOPARA Emote。** window52最大runtime wall497.234ms，decode/PSB聚合均0；5745行明确记录resourceLoad267.529ms，含fileLoad164.369ms和root103.151ms、sharedCacheHit0。该窗口Emote prepare累计353.743ms，相关父子阶段不可相加。后续还有162.400/156.556/151.823ms资源加载样本；共享缓存存在命中与eviction。它说明NEKOPARA的Emote加载值得独立跟进；尚无证据说明加大缓存就能解决，峰值resident已达1069.09MiB。本次只更新证据，保留既定“NEKOPARA修复后排”的实施优先级。

**诊断扰动与详情盲区。** 重窗口已记录overhead天使94为33.999ms、DRACU63为43.970ms、9-nine35为69.601ms；这些是整个窗口的已测成本，不能从最大单帧减掉，也没有覆盖tracer全部成本。v2成功保住step、PSB及VM调用量，但role3经常选择紧随onFire的timerCallback，与外层几乎同长，真正的loadScene/skip循环等深层VM详情仍可能被淘汰。9-nine的595,702次VM、DRACU的272,378/323,587次VM仍需有意义的内层调用或低扰动采样来解释；parent/residual不可视为纯VM CPU。沿用新session诊断A/B，不扩大无界逐调用输出。

**失败与内存。** PSB阶段failures均0，五session正常结束；DRACU记录1次image/image.decode失败，三份日志存在少量VM异常聚合，均无对应≥16ms失败详情，不能签为全路径无异常，也不能据此判崩溃。峰值resident/GPU Layer资源：天使849.19/612.04MiB、千恋663.50/593.43MiB、DRACU678.56/405.21MiB、NEKOPARA1069.09/188.83MiB、9-nine558.27/450.37MiB；无内存压力事件/泄漏因果证明。

## 6. GPU与已有优化继续独立对账

| 游戏 | deferred = materialized + omitted | layerCreate clear | 静止present最后可见skip | 缓存eligible / materialized / cancelled |
| --- | ---: | ---: | ---: | ---: |
| 天使 | 36213 = 34212 + 2001 | 0 | 1096 | 8 / 8 / 0 |
| 千恋 | 18141 = 16485 + 1656 | 0 | 1060 | 10 / 10 / 0 |
| DRACU | 17786 = 16192 + 1594 | 0 | 372 | 759 / 122 / 637 |
| NEKOPARA | 10099 = 9536 + 563 | 0 | 292 | 14 / 14 / 0 |
| 9-nine | 6503 = 4630 + 1873 | 0 | 843 | 8 / 8 / 0 |

Fill与clear取final完整累计；静止present为最后可见快照，不能当包含退出尾部的final。缓存计数单位是租约，不把637次取消写成637个转场。优化设备路径继续命中，不据此签像素矩阵或性能收益。

道路精确映射保持：`街_通学路a.png`、2120×1280、完整路径hash15177620565288462977，新session generation6529/texture6499/creatorLayer5396。共有5个相关样本，其中4个完整。command1761 / 原始3676–3678行：同目标encoder79072保留fullFill load clear，Fill logical155997 deferred→omitted，随后满幅Copy logical155998读取道路源；其后还有另一Layer读取同一路源。这证明合并实际命中，同时保留混合command归因边界。

该command有60.821M逻辑rectPixels、16 render encoder、57 actualDraws、4 omitted Fill，GPU43.809ms，otherFragment23.879ms，compute0.845ms；GPU stage完整。它包含多Layer/转场工作，不能将全部GPU时间归给道路或把overlap stage相加。其它完整道路样本GPU为11.756/11.661/13.428ms，不能只挑这些较快样本代表全段。

| 游戏 | 有效GPU stage / 已采样stage | 已采样command最大 | nextDrawable慢样本最大 |
| --- | ---: | ---: | ---: |
| 天使 | 94 / 109 | 100.923ms | 64.344ms |
| 千恋 | 96 / 100 | 79.126ms | 36.511ms |
| DRACU | 98 / 98 | 30.442ms | 33.361ms |
| NEKOPARA | 78 / 87 | 50.445ms | 47.724ms |
| 9-nine | 80 / 84 | 24.352ms | 30.356ms |

command与慢等待是有界采样，不能当完整分布；天使100.923ms和千恋79.126ms最大command在开局，stage分别有溢出，不拿部分fragment当完整值。nextDrawable是呈现backpressure，与readback和同步脚本wall分别解释。此次也记录到少量真实readback等待，DRACU约9.885ms、千恋11.818ms、NEKOPARA9.914ms；它们不符合上述约1秒场景窗口的主要成本。

## 7. CPU卡顿后续优先级与签收

以下优先级针对CPU长帧。用户随后指出持续发热仍未解决，已按完整累计总账重算pass/draw/像素率，发热方向优先调查持续Layer合成、target切换与Copy/snapshot/COW；详见[持续发热复核](NATIVE-METAL-P2D-3-THERMAL-FOLLOWUP.md)。PSB改善不能替代降温验收。

1. **PSB构建/同步释放**：已确认生产热点，按真实Array/Dictionary构建、成员填充与释放机制做小范围优化及生产语义回归；保留批量多文件计数，逐step范围继续补齐。
2. **图片imageOpen/PIMG/XP3路径**：千恋1.57s冷UI窗口证据最强，先拆stream/probe/父资源读取，再决定缓存或流复用，不能仅优化codec或无界增加内存。
3. **剩余VM调用与host event**：改善有界内层候选、区分poll/pending-input；同构建新session诊断A/B。保留脚本同步、回调/输入/存档语义。
4. **GPU道路配对**保持独立；NEKOPARA Emote专项保留后排，字体/音频/影片等旧证据不因本轮PSB命中而被排除。

实现完成、设备字段/路径命中、原生正确与性能签收分别登记：CPU v2/PSB设备归因已命中；Apple完整原生/App矩阵没有在本轮重新执行；没有固定输入nominal三组配对；CPU长帧仍未签收。

可复算产物位于 `build/p2d-3-analysis/`：`cpu-calls.json`、`layer-hotspots.json`、`analyze.py`、`analysis.json`、`analysis.log`。analysis.json保存输入SHA256、run/session、原始行号、CPU详情/总账、重窗口及相邻heartbeat累计差值；只读脚本未修改原始日志，也未增加任何运行时readback/submit/wait。
