# After_P2D_2：路径命中与剩余长帧

2026-10-11，只读日志分析。本轮不修改生产逻辑、不提交、不推送。

三份日志的 `sourceRevision` 均为 `8e3b7448f0ee`，与当前 Root `8e3b744` 对应；当前嵌套指针为 Runtime `f58097d`、Core `e191359`。日志记录 iPhone、OS 27.0、drawable 2532×1170、Metal composition，未记录具体机型。它们可用于本构建的路径命中及瓶颈归因，没有独立开关、固定输入的 nominal 三组 A/B，不能签收改善百分比。

## 输入与完整性

| 文件前缀 | 主游戏 / session | 已输出 runtime 窗口 | 热状态 |
| --- | --- | ---: | --- |
| `5D31DFD9` | 天使 / `40A11815` | 99 | 首窗 nominal；+37.553s 进入1，+57.557s进入2 |
| `C05D5528` | 千恋 / `A2758C90` | 87 | 首个性能窗口已为2 |
| `7D8BA83C` | DRACU / `F166022E` | 57 | 首个性能窗口已为2 |

时间均相对对应 `game.begin`，行号均为原始 JSONL 物理行号。三文件共18,895行，按run检查均无sequence缺口或重复；GPU/CPU parser全部valid，无缺失分片组。valid表示协议与计数关系成立，不表示所有有界样本完整。

天使文件还包含结束后约2.11s的千恋第二会话 `934B2B90`，没有runtime窗口或最终Layer总账，随后后台/terminate并启动另一个无游戏的run。该短会话单列，不混入主游戏统计；文件首尾uptime跨进程，不能直接相减。原始8127–8130行包含stop/terminate，没有足以证明崩溃的异常堆栈。

三个主游戏均有最终 `metal.layerPasses` 总账。其累计droppedReports为8/10/8，中间快照覆盖不完整，但最终总账仍可对账。完整hotspot样本分别73/91、47/74、29/49；其余因operation/resource/output/stage overflow或未知关联明确标记不完整。退出CPU尾窗缺summary/aggregate，尾窗详情不补进完整runtime分母。

分析产物位于 `build/p2d-2-analysis/`：`layer-hotspots.json`、`cpu-calls.json`、`overview.json`、`cpu-review/cpu-review.json`，对应脚本可重算。输入SHA256保存在Layer分析与overview中。

## D0 / D1：道路归因与实际省略成立

道路资源在本次完整样本中明确映射为 `街_通学路a.png`，2120×1280、完整路径hash `15177620565288462977`、generation9372、texture9336、creator Layer6718。`displayShortened=1`、`assetTruncated=0`，正常缩短文件名未使样本不完整。

command1956（天使原始3681行，+43.773s）的完整有序记录显示：

1. target9386、Layer/receiver6713、encoder103502以 `fullFill` load clear开始。
2. 普通Fill logicalID176480被标记deferred。
3. 同一logicalID随后标记omitted。
4. source9372的非alias、flags0、满幅Copy logicalID176481在同一encoder执行，目标1920×1440。

首个clear保留，后续被完整覆盖的Fill shader draw确实省略。另一目标的partial Fill被后续full Fill替换，后者在观察边界物化；本次省略不等于所有Fill都能删除。

| 游戏 | deferred Fill | materialized | omitted | 实际draw | render encoder |
| --- | ---: | ---: | ---: | ---: | ---: |
| 天使 | 28,420 | 25,512 | **2,908** | 410,390 | 213,681 |
| 千恋 | 11,219 | 9,976 | **1,243** | 231,421 | 68,416 |
| DRACU | 10,705 | 9,426 | **1,279** | 204,849 | 79,056 |

来源为最终Layer总账；各行deferred=materialized+omitted，最终无未对账待执行项。draw/encoder各有独立口径，不能用逻辑calls减omitted推算全部实际draw。

三游戏 `layerCreate` 独立clear均为0；fusedInitialization为8273/5050/6168，完整写省略初始化为234/376/121，fullFill load clear为17049/4766/2843。pass结束来源仍主要是target切换：189069/56217/53581；初始化边界仅156/38/185。说明当前仍有大量普通目标切换，并非纹理创建仍逐张独立clear。

## D3：真机命中，完整生命周期仍待验

| 游戏 | 最后可见eligible | 静止present skipped | cache eligible / materialized / cancelled |
| --- | ---: | ---: | ---: |
| 天使 | 5,375 | **1,008** | 8 / 8 / 0 |
| 千恋 | 4,736 | **1,041** | 10 / 10 / 0 |
| DRACU | 2,901 | **174** | 570 / 59 / **511** |

静止present为最后可见累计快照，原始天使7641、千恋5592、DRACU4921行；不是假定覆盖退出尾部的最终总数。生产Policy只有真实displayed且command成功的记录可以确认后跳过，因此证明该设备路径有命中；不代替新ticket、A已显示→B在途→请求A、迟到回调、resize/前后台等全生命周期专项。

主要拒绝原因为新presentation ticket：3909/3468/2708；in-flight为457/214/17。ticket必须正常present是既定语义，不能为扩大skip而去掉保护。每秒最大可见skip增量为天使43–45左右、千恋54，不能把累计skip直接当作相同数量的完整CPU帧被取消。

DRACU的511是未消费缓存租约取消次数，不能当成511次转场；570=59+511对账。其withChildren零帧end有292个。天使/千恋也有111/60个withChildren零帧end，但cacheCancelled为0；仅零帧end不足以证明满足“原无缓存、count0”等延迟准入条件，因此这两游戏的零消费取消收益未获证明。

## 长帧仍存在，分开看CPU与GPU

| 游戏 | 有效runtime间隔 | >33.3ms | >50ms | >100ms | >250ms | 间隔p99 | CPU step wall最大 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 天使 | 5,313 | 297（5.59%） | 68 | 12 | 2 | 54.318ms | **885.163ms** |
| 千恋 | 4,765 | 172（3.61%） | 64 | 20 | 6 | 57.874ms | **969.760ms** |
| DRACU | 2,902 | 189（6.51%） | 78 | 13 | 5 | 70.348ms | **1,089.029ms** |

数据来自全部已输出 `frameSamplesNS`，声明长度一致、`frameSamplesDropped=0`；interval0为历史边界标记而排除，wall保留这些样本。p99用线性插值。runtime完成间隔不是drawable显示帧率；wall包含native调用与等待，不称纯VM CPU。与After_P2D_1的场景、时长、热状态不匹配，不据这些比例作前后性能结论。

CPU方面，新调用证据已把一部分成本落实到场景重建链：

- 天使window85、+84.643s，wall885.163ms、script max881.344ms，decode/storage/GC为0。原始6667–6669行明确出现 `skipToStop/_nextscn_`；6672–6675行同一后续step捕获 `clearStorage`240.836ms、`StorageData`428.681ms、`initStorage`671.200ms、`loadScene`671.870ms。
- DRACU window39、+38.494s，wall1089.029ms、script1086.674ms、decode0；3125–3128行同链最大328.979ms。
- 千恋window69、+68.442s，wall969.760ms、script967.200ms，decode总81.685ms；4430–4433行同链最大191.164ms。另window50中该链达396.991ms，而decode仅1.339ms。

父子inclusive调用不得相加。每step最多4条详情使这些长帧的外层调用被丢弃，不能用已捕获内层链解释剩余全部时间。当前只有天使明确记录skip调用；把DRACU/千恋同类场景重建称为隐藏跳读延续仍属于结合旧基线的推断。CPU长帧保持未签收。

GPU方面，道路+43.773–53.792s的10个有效采样command约11.63–11.76ms，实际16 render encoder、62–63draw，Fill省略每样本1–2次。对应+44.551–53.553s的已输出runtime窗口共591个样本，9个>33.3ms、0个>50ms，wall最大2.464ms，decode0；这一段仍有runtime节奏波动，但没有旧基线道路段那种连续>50ms的表现。它已在thermal1，且没有固定输入A/B，不将该观察归因成正式收益。

道路后段仍有重command：2606为44.035ms，2834/2873/2918为32.628/42.324/41.902ms，2957为60.674ms。最后一项的operation详情因预算省略而不完整，stage timing本身完整，otherFragment39.268ms；不能拿局部operation列表分摊整个GPU时间。后段已进入或接近thermal2，同时绘制/输入内容在变化，热状态与工作量混杂。

可见nextDrawable慢等待最大为天使33.589ms、千恋37.694ms、DRACU49.156ms。这是有抑制计数的慢等待样本，不代表全部等待的分布；它是呈现回压，不能合并成readback或script CPU成本。三个主session有99/84/57个command时间样本，GPU stage有效91/81/57个；无效/partial stage单列，不把分stage重叠时间相加为command时长。

## 缓存、诊断与下一步

图片缓存已输出真实命中：天使344/497次load、千恋944/1320、DRACU386/507；KAG命中14/14/9。尚无预取入队、完成、预算、取消或来源日志，无法从cacheHit证明touch预取运行或贡献收益。缓存命中不能宣称长帧已解决。

CPU详情累计drop为1662/1412/853；stackFailures116/70/48集中在首窗；已flush窗口计数对账，退出尾窗不完整。记录到的诊断构造/输出成本累计为1.182/0.915/0.766s，仍需要诊断开关配对测真实扰动。主session峰值resident约821.08/909.19/727.91MiB，不能单凭峰值判定泄漏或缓存收益。

下一实施优先级应落到场景重建的 `clearStorage/StorageData/initStorage/loadScene` 可控开销及外层慢调用保留：先补足关联，使最长外层不被每帧前四条内层占满，再以生产路径复现定位标签/宏/事件或原生等待；保持表达式、回调及同步语义。GPU侧保留现有保守Fill规则，继续依据完整样本看剩余目标切换、Copy/Alpha与snapshot，随后执行nominal独立开关配对。现有日志足够确定这些方向，不需要立即重复导出同样场景。

本轮签收：D0 v2、D1省略、D3静止present和DRACU零消费缓存取消均有设备命中；全部原生正确性矩阵、预取域、CPU长帧治理及D2性能配对仍待验。
