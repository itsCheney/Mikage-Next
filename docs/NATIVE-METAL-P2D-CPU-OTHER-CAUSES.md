# P2D CPU 第二轮排除：PSB 之外的长帧来源

2026-10-11。基于同一份 After_P2D_2 / Root `8e3b7448f0ee`，并检查当前未提交的CPU v2/PSB归因包。本轮保留已有改动，补scope内VM调用数、一项生产所有权边界回归及分析文档，不扩张生产优化范围，不提交、不推送。

结论：PSB不是唯一候选。现有日志还确认字体、系统语音、影片打开、图片解码和脚本事件链存在慢调用；最重场景窗口同时有大量VM调用。对象树的最后释放也可能发生在PSB实例删除之外，单看psb.destroy不足以排除释放成本。

## 重场景窗口的交叉核对

| 游戏 / window | runtime step wall最大 | VM调用数（整个窗口） | 已记录CPU诊断overhead（整个窗口） | syncWait增量 | GPU queueWait增量 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 天使 / 85 | 885.163ms | 177,784 | 30.363ms | 0 | 0.000583ms |
| DRACU / 39 | 1089.029ms | 274,753 | 40.374ms | 0 | 0.001252ms |
| DRACU / 25 | 982.917ms | 344,869 | 45.869ms | 0 | 0.002002ms |
| 千恋 / 69 | 969.760ms | 251,303 | 43.127ms | 0 | 0.001375ms |

VM数和overhead来自对应cpu.aggregate/summary；wall来自完整frameSamplesNS。VM调用数不是单个step的调用数，也不是字节码指令数。诊断overhead不能直接从最大frame wall里扣除。CPU aggregate step的计时边界稍宽于runtime frame wall，两者不强行对齐小数。

metalProfile和emoteProfile是累计快照；表中等待使用相邻同run/session heartbeat.profile之差，没有把累计值归给单个窗口。窗口内transferOrigins没有read，天使/DRACU最重窗口decode为0，千恋69的decode总81.685ms、单次max48.041ms。对应Emote增量天使/千恋为0；DRACU39的progress2.082ms、prepare1.259ms、draw0.302ms，各自独立，不能相加成CPU阶段总耗时。

这将同步GPU读回、已记录in-flight queue等待及Emote计算从这几个约1秒窗口的主因候选中降级。nextDrawable是另一种等待，不能用syncWait=0排除它；已有道路GPU/present瓶颈仍独立保留。未记录的native等待也不能被这些字段排除。

可复算证据在 `build/p2d-other-causes/correlate.py`、`correlations.json` 和 `correlations.log`。

## 1. 大量脚本调用与场景扫描，优先级高

当前最重窗口有17万到34万次VM调用，而旧详情只保留最先返回的4个内层，无法说明这些调用分布在哪个step或函数。PSB的native解析可以很重，但StorageData的脚本循环、场景索引建立、状态恢复和跳读的其它副作用也可能很重。

旧本地DRACU `KAGEnvPlayer.tjs` 的328–344行在拿到PSB root以后仍遍历scenes建立sceneMap；988–1007行startScene在loadScene以后继续checkSkipToLabel和goToPoint；1080起的goToPoint、1153起的goToLine用getLine循环并执行restore/setSFlag等副作用。这证明存在PSB读取之外的同步工作机制，不证明本次三个游戏使用相同版本或其实际占比。

本轮给尚未发布的v2详情补`vmCalls`：复用已有owner-thread调用序号，记录各保留scope内开始的VM调用数量，VM scope包含自身，step/PSB phase只计其范围内调用；没有额外时钟、栈查询或对象引用。它能把调用量落实到最慢step，不能把调用数换算成CPU耗时。v1明确`vmCallCountAvailable=false`。

需要按最慢step保留完整外层与调用总数/直接子调用类别，区分“少数重native”和“大量短调用累计”。当前v2改善详情保留，但其残余wall仍含未测native、属性分派、临时对象清理和等待。不能把它直接叫纯VM执行，更不能靠强制yield或缓存最终tag/表达式结果治理。

## 2. PSB实例之外的对象释放，已复现覆盖边界

游戏可以把 `data.root`、scenes、sceneMap、scenario或其它节点留在自己的变量中。PSB native实例删除时只是释放它自己的_root引用；若外部引用仍在，树会继续存活，最后一个外部引用释放时才真正销毁。

新增 `Tests/PSBFile/PSBFileTests.cpp` 的retainedRootBoundary使用真实PSBFile和TJS容器：

1. 外部variant保留root，再invalidate并删除PSB owner。
2. 此时psb.destroy已发生，树的析构marker没有执行。
3. 清掉外部root后marker才执行，测试时钟增加20ms；psb.destroy计时保持不变。
4. 诊断开关前后的内容/IO/所有权序列一致。

生产PSB测试 **1/1通过**。这个20ms是用于证明计时边界的假时钟，不是实际游戏或设备性能数字。因此“psb.destroy很小”也不能排除一般字典/数组/脚本finalizer的同步释放。

另一个真实机制是VM寄存器临时variant保留：`tjsInterCodeExec.cpp:612–725`在普通Deallocate时减少使用数，InternalCompact或后续赋值才清理保留值；`tjsObject.cpp:918–1115`和`tjsArray.cpp:1454–1505`会同步清成员/子对象。这可能把释放成本推到下一次调用或compact边界。当前未证明它占多少，不能直接改成立即清空，因为finalizer与重入顺序可能改变。

## 3. 字体、系统语音、影片初始化，已有独立慢调用

| 路径 | 原始日志证据 | 源码候选机制 | 结论边界 |
| --- | --- | --- | --- |
| 字体映射 | 天使991–994行，__mapFont76.107ms、getTextWidth77.101ms | PrerenderedFont.cpp构造执行完整字体文件读取；LayerBitmap.cpp:1785已有字体缓存 | 该次调用慢；尚未证明重复加载/缓存故障 |
| 系统语音 | 天使510–513行play87.943ms、PlaySystemVoice104.613ms；千恋385–388行play85.061ms、外层131.563ms | WaveSoundBuffer Open同步创建decoder；StartPlay在BufferCS/L2BufferCS下同步预填L2及多个L1 buffer | 音频open、初始解码或锁等待均是候选；日志尚不能分摊原生占比 |
| 影片打开 | DRACU1597行Movie.open128.387ms；千恋3774行144.990ms | VideoOverlay::Open先Close，创建stream/player；BasePlayer::OpenFromStream同步OpenDemuxStream/OpenDefaultStreams，旧流关闭可StopThread | 打开慢已确认；文件/解码器/播放器等待需进一步区分，不能从脚本名直接判断格式 |
| 图片冷加载 | 天使window22单次imageDecode76.397ms，DRACU32为65.758ms；千恋4为50.811ms | 已有image.resolve/open/codec阶段及真实CPU缓存路径 | 是部分>33/50ms长帧来源；不能解释decode0的约1秒窗口 |
| 其它脚本事件 | 天使3132–3133行extractDelay/doExtractDelay约59ms；5751行saveSystemVariables42.203ms | 原生存储/序列化可能同步；这些脚本正文未核验 | 保留候选，不凭函数名把59ms判成archive解压或把42ms全部判成磁盘写 |

同一链的内外层时间不可相加。表中的源码机制支持排查方向，尚未由本次日志逐调用确认进入该native函数。音频/字体/影片路径尚未有相应原生CPU phase，现有PSB日志不能覆盖它们。它们足以说明长帧需要按场景分类治理，但不能被拿来解释所有0.9–1.1s跳转。

## 4. 诊断本身有可测扰动，必须隔离

上述重窗口已记录30–46ms的CPU诊断构造/记录/输出成本。每个VM调用还会走tracer的Push/Pop/上下文AddRef/Release，以及相关scope/时钟路径；这些操作没有全部进入CPUFrameDiagnostics的overheadNS。该字段不是“诊断全部成本”的完整证明。

`LayerWorkDiagnostics::StageScope`用嵌套depth只为最外同类stage计时并加锁，源码不支持“每个VM调用都持全局stage mutex”的判断。本轮没有发现可据此确认的全局锁热点。

应以同构建、同存档输入的新session做诊断开关A/B，独立报告日志开关和Fill/D3优化开关。host tracer为保持entry/exit平衡会在当前session保留已取得的引用，因此只在游戏中途关录制不能证明完整去掉tracer成本。现有窗口数据不能证明诊断解释了整秒长帧，也不能宣布它开销为零。

## 5. 内存与GPU呈现，保留独立候选

三个主session峰值resident约821/909/728MiB，Layer GPU资源峰值约520/609/309MiB。PSB/MDF临时缓冲、TJS树、寄存器保留、图像与GPU资源同时存在，分配/释放或OS内存压力可能放大冷路径。仅凭峰值没有泄漏、内存压力事件或耗时占比证明。

道路后段GPU command32–61ms、nextDrawable慢等待最多约33.6ms（天使）、37.7ms（千恋）、49.2ms（DRACU），是另一个已观察到的长帧域；它和场景重建的同步脚本/native wall分别验收。thermal2可能放大多种成本，目前没有nominal固定输入三组配对，不能分离热降频因素。

## 后续裁决顺序

先用当前CPU v2/PSB新包保住重step证据，配同构建新session诊断A/B；再按实际结果选择场景扫描/一般对象释放或PSB构建/释放。若PSB阶段很小，优先补游戏函数边界内的原生类别与每step调用数量，不能据psb.destroy很小排除外部引用释放。

字体、音频、影片初始化作为独立短长帧包，在相应场景复现后再做阶段计时与修复。图片冷加载沿已有codec证据推进；GPU道路单独做热状态配对。保持同步执行、输入、存档、表达式和回调语义。

本轮没有发现足以直接宣布某个新生产优化必然解决长帧的设备证据；发现的是多个有日志支持的慢路径，以及一项已由生产回归证实的归因覆盖边界。

本轮检查：Backend **4/4**（含8个CPU parser用例）、生产PSB **1/1**、真实Release VM栈 **1/1**通过；v1旧日志回归仍通过。Apple/新设备v2字段待验。
