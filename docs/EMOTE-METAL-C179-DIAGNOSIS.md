# Emote 持续卡顿：C1798B29 日志复核

日志 `Mikage-diagnostics-C1798B29-ACE9-4311-9E91-D75BBCA52DF6.jsonl`，
构建 `9b0c0a823c3c`，2026-09-28，实际后端 Metal。
用户确认：不点击时，动作也持续不连贯。

播放窗口为 UTC 08:42:00.433–08:44:10.539（北京时间16:42:00–16:44:10）。
累计计数采用最接近的完整心跳 08:42:00.244→08:44:10.252，跨度130.008秒。

## 上一轮修复的结果

| 播放期计数 | 上次 2D11 日志 | 本次 C179 日志 |
|---|---:|---:|
| 单点回读 | 335 | 143 |
| 整图回读 | 100 | 55 |
| GPU 同步等待总计 | 约9.124秒 | 约2.967秒 |

两次场景和输入不是严格一致的实验，不能把这些差值直接换算成优化收益百分比。
但本次所有慢单点样本都由 `pointerDown` 发起，旧版 cursor/clickWaiting 属性栈
已消失；27个 Emote capture 慢读样本的纹理 version 为1，旧版为2，符合省掉
旧图复制后的路径。

全日志52条慢单点读取，其中播放期51条，中位37.838ms、最大72.220ms，
99.65%的等待时间在GPU同步。这些仅为≥8ms、每类别最多每秒一条的限流样本。
实际总查询应使用心跳累计计数，不能用慢日志条数代替。

## 持续卡顿已经与点击回读分开

UTC 08:44:02.264→08:44:07.271 的约5秒内：

- 点缓存 miss 固定218，CPU fallback 固定134，同步等待次数固定340；
- 没有资源加载，仍只有约40.8–41.3 FPS；
- CPU帧耗时约20.7–22.9ms，其中呈现等待约15.7–19.2ms；
- 每秒约82次 capture，约为2个人物各更新41次。

因此持续不连贯不需要点击回读或资源加载就会出现。CPU帧耗时包含等待，
不能全部算作动画计算。播放期116条慢 `nextDrawable` 等待中位16.491ms、
最大46.802ms；它们表示呈现端等待，不能单独证明哪个GPU阶段最慢。

122个 `mixed` GPU命令缓冲样本的中位耗时39.162ms、最大69.285ms。
它们混合了网格、蒙版、图层计算、传输及呈现，当前日志不能按操作次数分摊
阶段时间。单个人物的稳定样本通常有36次变形绘制、84次图层dispatch；
双人物样本通常有68–72次变形绘制及约85次图层dispatch。

## 其他方向的核查

最后心跳累计：prepare 19.295秒/9213次，平均约2.094ms/次绘制；按5637个
动画更新步骤计约3.42ms/步。CPU网格准备1.151秒，子motion创建/复用0.076秒，
CPU变形顶点为0，GPU变形已启用。shape命中区域构建4.933秒，占prepare约25.6%，
仍有优化空间，但这些累计平均不能解释持续几十毫秒的GPU/呈现等待。

重复绘制只有66/9213，约0.72%；未发现明显每隔一帧才更新或大规模重复绘制。
代码使用浮点动画增量和插值，没有发现整数帧量化证据。现有日志没有逐帧姿态
或脚本传入增量，因此这些检查也不能完全排除游戏脚本的动画时序问题。

热状态在08:41:56.016已经进入serious，早于首次Emote加载约4.4秒。
不能把本次升温归因于进入动画后的某个瞬间。场景切换仍有慢加载：共享缓存
命中时59.8–79.7ms（root构建43.1–56.4ms），未命中时86.5–163ms；它们不在
上述稳定5秒区间内。

## 下一步证据

补充原有命令缓冲内的GPU阶段时间戳，把网格顶点/片元、图层compute、传输及
其他render阶段分开。只在既有每秒一次采样中启用；不通过增加提交、等待或
屏障来人为切开渲染。硬件不支持或采样失败要明确标记，不以0ms冒充测量值。

依据Apple的[阶段边界采样接口](https://developer.apple.com/documentation/metal/sampling-gpu-data-into-counter-sample-buffers)
和[GPU时钟校准方法](https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time)，
在GPU完成后解析，并用前后CPU/GPU时钟对换算时间。不同阶段可重叠，阶段区间
之和不能当作命令缓冲耗时，也不能把GPU顶点时间直接当作纯Bezier求值时间。

## 代码与稳定区间交叉核对

原始记录2252→2292（08:44:02.264→08:44:07.271）间隔约5.007秒：

- 动画步骤5236→5442（+206），draw/capture 8562→8974（+412），重复绘制始终66。
  对应每步骤两次人物绘制，并没有大规模重复旧绘制的证据；计数不是姿态变化证明。
- prepare累计增加830.545ms，约4.032ms/步骤；其中shape构建增加227.849ms，
  约1.106ms/步骤。draw CPU编码增加94.404ms，约0.458ms/步骤；capture CPU路径
  增加25.272ms，约0.123ms/步骤。后两项不包含异步GPU实际执行时间。
- capture GPU拷贝增加3,417,292,800字节，每次8,294,400字节，符合1920×1080×4的
  整画布拷贝。此为逻辑拷贝字节，不是实测DRAM带宽，不能仅凭该值断言传输是瓶颈。
- 图层GPU操作增加17,532次，约85.1次/步骤；点查询miss、CPU fallback、同步等待次数
  和同步等待总时间均未增长。ring wrap/stall/fallback为0，不能再归因于环形缓冲耗尽。

动画`progress`使用浮点`mstime / speedRatio`推进，节点关键帧有插值；默认`speedRatio=20`
是时间换算系数，不是强制50 FPS帧率上限。仍缺脚本传入增量和逐帧姿态，不能完全排除
时序问题。宿主CADisplayLink明确请求60 FPS，但请求值不保证实际呈现频率。

计时字段的口径必须区分：宿主`recordFrame`中的CPU时间是约一秒窗口的步骤墙钟平均，
包括等待；`presentationWaitTimeMS`取最近一帧的等待；`gpuSubmissionTimeMS`取最近完成
命令缓冲的GPU区间。因此同一条心跳中的这些值不一定对应同一帧，不能相减作为精确的
动画CPU时间，也不能把GPU时间倒数当作实际FPS。稳定区间CPU细项采用以上累计差值。

## 本轮诊断接线（尚未真机验证）

`MetalStageDiagnostics.h`原来仅有预留槽位与时间换算逻辑，未被运行时引用。
本轮在`MetalRenderBackend.mm`接入既有每秒一次采样：

- 检查OS、stage-boundary能力与timestamp counter set，分配单命令缓冲独占的采样区。
- 在原有render/compute/blit pass descriptor上挂采样点，保留已有mesh/compute encoder复用。
- render区分mesh、window、other的vertex/fragment，compute和blit各记录encoder区间。
- 完成回调保留诊断状态与Metal资源，不捕获后端；完成后采集时钟对并在CPU解析共享采样区。
- 每个命令缓冲最多512个样本，溢出仅跳过后续pass。无额外提交、同步等待或counter barrier。
- 新事件`metal.gpuStages`以`id`关联`metal.gpuCommandBuffer`，记录完整/部分/不可用状态、
  时钟有效性、各阶段有效/无效区间数、丢弃/失败pass数。缺失阶段为-1，真实零耗时保留0。

新增跨平台测试覆盖时钟换算、重叠区间、截断与错误时间戳、缺失阶段、容量上限和失败回滚。
切换手动权限模式后，本地验证已完成：

- `cmake --build build/metal-render-tests --parallel`：编译通过。
- `ctest --test-dir build/metal-render-tests --output-on-failure`：1/1通过，包含新阶段计时测试。
- `python scripts/check-krkr-frame-times.py`：帧率/墙钟时间区分、峰值阶段关联、重置与限流检查通过。
- 主仓库及核心子模块`git diff --check`通过；主仓库仅提示README将按Git配置转换换行符。
- 全量复算2418条原始日志，确认播放期122条mixed GPU样本中位39.162ms、116条慢
  nextDrawable样本中位16.491ms，以及51条慢点读取样本中位37.838ms；稳定区间增量与上文一致。
- 核对Apple官方阶段采样、CPU解析和时间戳换算文档：CPU时钟单位为纳秒，GPU时钟需要
  前后时钟对校准；shared counter buffer可在GPU完成回调中直接CPU resolve，无需另加blit。

当前环境为Windows，原生Metal分支仍需要Apple SDK构建和真机采样验证；跨平台测试通过
不代表原生分支已编译或GPU采样成功。这些改动增加的是定位证据，不代表已经修复动画卡顿。

后续同一场景、静置不点击至少10秒，收集带`metal.gpuStages`的新日志。先确认`status`与
各阶段有效区间覆盖，再比较mesh vertex/fragment、layer compute、blit；不要把各阶段相加
当作帧预算。最好同时记录冷机和serious热状态下的相同场景，以区分热状态影响。
