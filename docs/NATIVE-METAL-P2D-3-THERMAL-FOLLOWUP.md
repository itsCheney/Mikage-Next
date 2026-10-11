# After_P2D_3：持续发热方向复核

2026-10-11。用户反馈仍然发热。本轮重算同一批五份日志的持续工作量，接续[CPU长帧分析](NATIVE-METAL-P2D-3-DEVICE-ANALYSIS.md)。版本、输入完整性和热状态沿用该报告。只做只读分析与文档更新，不修改生产逻辑，不提交、不推送。

发热验收仍未通过。上一份报告的PSB优先级针对卡顿；持续发热的下一包应优先调查普通Layer合成的pass切换、Copy/snapshot/COW及缓存失效。当前数据确认剩余工作量很大，尚不能量化各路径的瓦数贡献。

## 1. 持续工作量

| 游戏 | render pass / tick | blit encoder / tick | 实际draw / s | 逻辑覆盖 Mpx / s | host step wall / interval |
| --- | ---: | ---: | ---: | ---: | ---: |
| 天使 | 47.96 | 4.20 | 4,599 | 560.5 | 38.9% |
| 千恋 | 19.96 | 2.32 | 3,377 | 371.5 | 19.0% |
| DRACU | 28.18 | 2.55 | 3,631 | 302.0 | 17.6% |
| 9-nine | 17.30 | 1.51 | 1,982 | 213.9 | 15.5% |
| NEKOPARA | 49.65 | 3.95 | 6,021 | 540.2 | 62.6% |

pass/tick取完整commandIntervalProfile增量，跳过首个counter baseline；实际draw与逻辑像素率取第一份到final的metal.layerPasses累计差，除以其报告时间跨度。两组计量跨度略有不同，保留在thermal.json中。`drawCalls`字段在Swift指向meshDrawCalls，不能用它当ordinary Layer总draw；这里使用actualDraws。

逻辑覆盖保留已省略Fill的原操作口径，不是实际fragment数量；snapshotBytes/clearBytes也只表示记录的工作量，不是实测DRAM流量。wall占比包含native工作、GPU/呈现等待与诊断，不能称纯CPU利用率。采样GPU command不能乘command数推算完整GPU占用率或功耗。

这些负载持续多个窗口：天使约30–50s的累计差为每秒2,505个pass、981.1M逻辑像素；约50–70s为每秒3,949个pass、7,250次实际draw。window59（原始5298行heartbeat.profile）没有imageDecode，仍有131.70个pass/tick、平均step wall20.14ms。DRACU window18（2070行）没有imageDecode，仍有53.07个pass/tick。它们说明持续绘制不能由少数冷加载尖峰解释；零decode窗口不自动等于静止或没有其它资源工作。

天使thermal0→1→2观测于约22.6s/42.6s，千恋约41.2s/61.2s，DRACU约36.9s/51.9s，符合用户仍然升温的反馈。没有受控能耗对照，不能从热状态时间推导优化前后百分比。

## 2. 已有优化命中，剩余总量仍大

天使final累计省略2,001个Fill，仍编码520,175次actualDraws、272,382个render pass。初始化layerCreate独立clear已经为0，静止present最后可见累计skip1,096/eligible5,700。这些数据确认路径生效，不能从调用个数比例推导像素、GPU时间或降温幅度。

静止present只省drawable/窗口绘制/present，按既定语义仍提交离屏工作。天使约71.3%的eligible请求因新presentation ticket正常呈现，DRACU约92.3%。不能通过忽略新ticket提高skip率；应先核对上游实际更新与离屏缓存重建的必要性。D3转场缓存也只消除符合条件且未消费的租约，本次DRACU取消637个租约，已消费路径仍正常执行。

按第一份到final累计差计算，天使pass结束原因约79.9%是target切换、15.0%是blit边界；千恋约74.9%/19.4%，DRACU约61.8%/15.2%。这是结束次数分布，不是GPU耗时或功率分摊。

天使逻辑snapshot工作量约0.176GiB/s、clear工作量约0.509GiB/s；DRACU clear约1.274GiB/s，explicit clear final为33,541次。必须分清正确性必需的Copy/clear与可消除覆盖，不把计数高直接判为冗余。

## 3. C3不能沿用旧基线的归因

| 游戏 | glyphApplied / glyphCalls | tiny-upload批次（各epoch final合计） |
| --- | ---: | ---: |
| 天使 | 29751 / 29751 | 10 |
| 千恋 | 51354 / 51354 | 155 |
| DRACU | 44283 / 44283 | 6 |
| 9-nine | 35951 / 35951 | 152 |
| NEKOPARA | 10487 / 10487 | 531 |

每文件有正常工作epoch和退出reset后的空epoch，两者final都保留。不能只看最后空epoch而把glyph记成0，也不能累加同epoch每秒的累计快照。

天使完整profile区间仍有23,693个blit encoder，但tiny-upload完整总账只有10批。旧报告“多数blit由逐字形上传造成”的归因不适用于这一批。剩余blit应沿GPU Copy、snapshot、COW和其它资源上传重新关联，不把它们全部归给某一种来源。

天使高pass样本command2476有522个render encoder、833次actualDraws，却只有2.812M逻辑rectPixels；已保留资源中有48个49×68纹理，操作role出现draw/composite/transition.output。这支持小区域/小Layer与目标切换并存的机制线索。该样本operation/resource/stage/output均溢出，不能据此认定所有522个pass都来自文本，也不能给出完整fragment比例。另一些道路样本则有31–61M逻辑像素及约11.7–43.8ms GPU command，两个工作形态需要分别治理。

## 4. CPU与诊断保留独立方向

按已flush窗口，天使script wall约205.3ms/s，imageDecode约13.4ms/s；DRACU分别134.0/13.0ms/s，千恋126.6/29.6ms/s。script包含嵌套native与等待，不与imageDecode相加。PSB构建/释放可改善秒级停顿，但这些峰值证据不足以解释所有持续发热。

CPUFrameDiagnostics已测overhead约天使21.1ms/s、千恋13.1ms/s、DRACU12.3ms/s、9-nine13.0ms/s、NEKOPARA18.2ms/s。该字段仍不包含tracer和其它日志的全部成本。应以同构建新session诊断开关A/B隔离扰动；关闭诊断的收益单列，不冒充渲染优化。

NEKOPARA还有持续Emote prepare/draw/capture与约35.9GB的窗口区间captureGPU字节累计，另有资源加载尖峰；保持独立且按用户既定次序后排，不能拿一个PSB优化承诺解决它。

## 5. 对实施优先级的修正

发热方向先追：

1. 普通Layer/cache/transition输出的持续target切换，确认dirty触发者、消费范围及无变化时是否重建；只删除有完整覆盖或不变性证明的工作。
2. 剩余GPU Copy/snapshot/COW与blit来源，关联资源版本/Layer/实际ROI，分别观察道路大面覆盖和小Layer高pass。保持alias、CPU lease及观察边界，不做跨目标重排。
3. 已有静止present与转场缓存的上游失效原因，保留新alpha ticket和真实显示确认语义；不靠降低画质、限制正常绘制或降低脚本执行频率掩盖热负载。
4. 新session诊断开关配对，继续保留CPU PSB、图片打开和host事件专项，但不以长帧改善签收降温。

2026-10-11 后续：代码与保留样本进一步定位到两处可直接消除的窗口合成浪费——不透明 Primary 的 DrawBuffer 同坐标自拷贝（每个 update rectangle 一次 alias snapshot + pass 断开），以及 update region 被分带成细条后每条各走一整条 pass 链。两项已本地实现并通过 portable 回归，设计与待验范围见 [兼容计划 P2D/D1](NATIVE-METAL-COMPATIBILITY-PLAN.md)。保留 command 拟合约 51µs/pass、0.57ms/逻辑Mpx：GPU 侧像素量仍是主项，pass 数主要压在 CPU 编码与固定 pass 开销上，因此这两项不代替上面 1–3 的 overdraw 排查。

本批日志已足以把持续工作量排查列为发热主线，无需原样再抓一批相同诊断日志。具体生产优化仍须针对已证实依赖给出像素/顺序回归，再用固定输入、同设备nominal配对验收。Energy/真实功耗、完整GPU busy时间及同条件温升改善当前仍无测量。

可复算脚本和数据：`build/p2d-3-analysis/thermal.py`、`thermal.json`、`thermal.log`。与原始JSONL核对累计快照单调、epoch final和时间跨度；不改原始日志，不新增运行时submit/wait/readback。
