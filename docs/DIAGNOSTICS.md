# App 诊断日志

设置 → 诊断与日志提供「完整诊断日志」「导出诊断日志」「清除诊断日志」。开关默认关闭，独立持久化，不改变旧的播放器设置。

## 复现黑屏

1. 开启完整诊断日志，清除旧记录。
2. 从游戏库启动 A，退出后启动 B，再退出并启动 A；出现黑屏时等待至少三秒。
3. 回库，在设置导出 JSONL 文件。若发生闪退，重新打开 App 后导出，另附系统 IPS 崩溃报告。
4. 如做软件合成 · Metal 对照，请使用相同顺序。日志记录请求和实际渲染后端。

## 覆盖范围

- App 启动、设置变更、游戏库扫描/导入、启动请求和用户可见错误。
- 每局唯一会话编号、Swift session 状态、桥接启动/结束结果。
- UIKit 横屏/恢复方向请求、系统拒绝和等待超时；scene、窗口 bounds、安全区域、key/hidden/alpha/root controller。
- App/scene 前后台、音频中断/route change、内存告警和 thermal 状态。
- 每秒 DisplayLink tick、step result、runtime FPS、帧间隔、drawable、实际 renderer、resident memory及窗口清单，独立于性能 HUD。
- Metal Layer heartbeat 额外记录 GPU→CPU readback 来源、software fallback 首次回读的 target/source/reference 角色，以及 GPU 路径拒绝原因（如 targetCPUResident、unsupportedMethod、multipleInputs、triangles/perspective 等），用于区分兼容性回退和可优化的数据搬运。
- `heartbeat.profile.commandIntervalProfile` 记录相邻心跳之间的 Metal render/compute/blit encoder、网格绘制、Emote 蒙版重画、`LayerDrawRect` 快照字节、ring 上传、command buffer，以及普通 Emote→Layer 的 GPU 复制或同步 CPU 回读。`ticks` 是同一段内的回调次数；首条心跳只建立基线。蒙版 group 只按源节点身份去重，不代表内容未变化。
- 浮层安装、菜单开关、移除；KRKR 原有日志/异常标题与 SDL 已产生的日志。保留 SDL 原输出回调，不替换 TJS 脚本日志处理器。

## 复现 Emote 卡顿

开启完整诊断日志，进入同一段立绘动画并持续至少五秒，再导出 JSONL。比较相邻 `heartbeat.profile`：`commandIntervalProfile` 的计数是这段时间内的增量，`ticks` 是分母。`emoteLayerCPUReadbackNS` 和 `metalProfile.syncWaitNS` 同时增加时先查同步回读；`renderEncoders` 接近 `drawCalls` 时查网格 pass 切换；`layerRectSnapshotBytes` 高时查 D3D Emote 合成；`maskClears` 明显高于 `uniqueMaskGroups` 时再评估蒙版缓存。不同组的时间字段不能直接相加，GPU 提交时间也未必对应同一心跳的 CPU 峰值。

### 图层传输与主线程阶段诊断

每次诊断心跳另输出 `session.layerWorkProfile`；HUD 和 `GetStats` 不消费计数。
`intervalMS` 是实际采样间隔，首条包含本次 Layer 会话开始至首次采样的工作。

- `stages` 使用 `名称:调用数/墙钟纳秒/最大单次纳秒`，包括 `resourceLoad`、`amvDecode`、`script`、`gc`、`compact`、`software`。阶段只统计开启诊断/绑定 Layer 会话的引擎线程（宿主主线程），避免混入后台加载工作；同类递归只统计最外层调用；不同阶段可以嵌套，且包含同步等待，**不能相加成 CPU 时间**。`software` 覆盖整个软件回退，包含准备 CPU views 和回读。资源加载包括图像、脚本存储和 Emote/AMV 加载；缓存命中也会计时。
- `transfers` 使用 `upload或read:来源@纹理ID(宽x高,lease=0或1)=次数/逻辑字节/墙钟纳秒/GPU等待纳秒`。只统计实际普通 Layer 上传和同步回读，不包含缓存命中、GPU→GPU 复制或异步 alpha tile。来源涵盖具体回退算子、LayerEx 插件、视频/AMV 帧、像素 API 和直接 CPU 位图操作；没有更高层标记时使用 API 类别。
- `lease=1` 表示上传时仍有 CPU 写访问未结束。内置插件完成一次操作后结束作用域访问；脚本原始写指针保留保守同步策略。合并多次 CPU 写入后上传的区域用 `mixed` 标记不同来源；租约上传优先标记原始写指针来源。
- 每段最多记录 64 个纹理/来源/方向/租约组合，输出字节最多的八项，其余按方向合并到 `other`，保留全部次数、字节和时间。纹理 ID 只供当前运行关联；维度记录传输发生时的整张纹理尺寸，字节数可能只是局部更新。
- `amvDecodedFrames`、`amvDecodedBytes` 是区间内实际解码成功的 AMV 帧数及 RGBA payload 总量，不是当前 resident memory。AMV 保持现有全帧解码行为；用这两个字段及 `resourceLoad/amvDecode` 判断打开资源时的卡顿和内存峰值。

关闭诊断会清空这些计数，结束旧录制期间的阶段不带入新录制；新 Layer 会话也从零开始。比较传输率时使用实际 `intervalMS`，并将启动、设置、鉴赏、剧情和退出确认分别对照。

### Layer affine / triangles 诊断

`session.layerTriangleProfile` 随每秒心跳输出一次，包括没有 triangle 调用的零值区间；不在每次 operation 写日志。它分别统计 KRKR Layer `OperateTriangles` 的 GPU 成功路径和软件 fallback，和 Emote `DrawDeformedMesh` 的网格 triangles 分开。

- `calls`、`triangles`：本区间 fallback 调用数和 triangle 总数。`intervalMS` 是实际采样间隔；调用率为 `calls * 1000 / intervalMS`，主线程卡顿时不能假定间隔恰好一秒。首条覆盖 Layer session 建立/诊断开启至首次采样。
- `gpuCalls`、`gpuPixels`：本区间成功的 Copy affine GPU 调用数和 dispatch 像素数（接受的空区域调用为零像素）。旋转/剪切按整个 clip 执行，轴对齐按可见矩形执行；不是 GPU duration 或 triangle 几何面积。与 `calls` 分别计数，避免把 fallback 消失误判为成功。
- `triangleMethods`、`triangleSources`：RenderMethod 分布及 `AffineCopy`、`AffinePile`、`AffineBlend`、`OperateAffine` 来源。共享 method 对象的别名统一使用首次注册名（如 `AlphaBlend_HDA` 归入 `AlphaBlend`，`PerspectiveAlphaBlend_a` 归入 `AlphaBlend_a`）。直接调用 bitmap/render manager 的路径记为 `unknown`，不是脚本堆栈定位。两个 affine 重载都传递来源，不改 RenderManager API。
- `clipPixels`、`maxClipPixels`：clip 与 target 相交后的面积之和/最大值，不是实际 triangle 覆盖像素。`fullSurfaceCalls` 表示 clip 覆盖整个 target，不代表 triangle 画满了 target。`triangleTargetSizes`、`maxTargetPixels`、`target1920x1080Calls` 表示 target 尺寸分布。
- `targetReadbackBytes`、`sourceReadbackBytes`、`referenceReadbackBytes`：该区间 triangle fallback 实际引起的 GPU→CPU 回读。CPU cache 命中为零；同一纹理别名只计首次强制回读的角色（target → reference → sources），不重复计算。后续 CPU→GPU 上传仍看心跳的累计 `layerUploadedBytes` 差值。
- `cpuTimeMS`、`maxCpuTimeMS`：整个 fallback 的主线程墙钟时间，包含回读同步等待、CPU views 和软件绘制。`softwareTimeMS`、`maxSoftwareTimeMS` 仅测软件 `OperateTriangles`，已包含在前者内，不能相加。
- `count2Calls`、`singleInputCalls`、`referenceCalls`、`sourceTargetAliasCalls`、`triangleStretchModes`：后续 Metal 支持范围需要的调用形态。stretch 值沿用 `tTVPBBStretchType`（0 nearest、1 fast linear、2 linear 等）。各字段分别统计，不代表这些条件同时成立。

方法、尺寸、stretch 分布每个区间最多保留 32 个键，输出前八项，其余合并到 `otherCalls`；方法名超过 40 UTF-8 字节时截短并加 `~`。所有计数和最大值在采样后重置，普通 HUD/`GetStats` 读取不消费它们。关闭完整诊断时停止额外计时/采样并清空未输出区间；新 Layer session 也从零开始。

使用同一游戏、角色、场景和 Metal 后端，依次记录约 10 秒静止、10–20 秒小幅头部动作、10–20 秒大幅身体动作，再导出完整日志并说明阶段时间（可同时录屏）。比较 `gpuCalls` 与 fallback `calls`、回读突增、`maxCpuTimeMS` 与同时间的 FPS/帧时间。当前 Copy affine 支持单 RGBA source、两个构成平行四边形的 triangle、整数正向 source rect，以及 nearest / fast linear / linear；reference 不参与 Copy。源目标别名用 GPU snapshot。其他方法、镜像轴对齐、非 affine、非常规 source rect 和 CPU 常驻 target 保留软件路径。真机验证要检查旋转边缘、透明区域、裁剪和 scene 切换，同时看 heartbeat 的 upload/readback、submit/wait 差值。

统一记录 UTC/Unix 毫秒时间、单调运行时长、写入序号、线程、构建提交、进程运行编号和游戏会话编号。它用于定位宿主窗口切换与 runtime 事件的先后关系，不声称检查了每次未返回错误的绘制结果，也不替代系统崩溃采集。不采集屏幕像素、每次触摸、搜索内容或游戏/存档文件。

## 存储与隐私

日志仅保存在 `Application Support/Mikage/Logs`，最多四个约 2 MiB 的 JSONL 分段；轮转删除最旧分段。后台串行写入，入口队列和单条内容均有限制；过载时丢弃日志并在后续记录中标记数量。磁盘失败停止记录并在设置提示。每秒及前后台/退出时刷盘。

关闭开关停止 App 日志采集和额外诊断采样，但保留现有文件。导出为不可变的 JSONL 快照，经系统分享面板交给用户，不自动上传。清除只删除 logger 的固定分段，不删除游戏文件。下一次导出会清理此前的临时导出副本。

不主动读取游戏或存档；统一替换当前沙盒路径，并过滤 KRKR VM 反汇编/寄存器转储。游戏名、资源名和错误文本仍可能属于敏感信息，分享前需检查。KRKR 自身的错误日志行为不受此开关保证完全关闭。

## 验证

`DiagnosticLogTests` 覆盖默认关闭、停用后保留、导出、JSONL 多行转义/顺序、大小轮转、超大记录、清除隔离和不可写路径。完整桥接与 UIKit 编译由 iOS CI 执行；真机验收应验证开启后 A→B→A 和后台/前台时间线完整，关闭后文件不再增长。

## 验证游戏消息框

引擎启动由主 RunLoop block 调用，避免 MainActor 主队列任务同步等待 SDL 消息框时阻塞触摸回调。日志中的 `start.scheduledOnRunLoop`、`start.enteredFromRunLoop` 和 `start.returned` 分别记录调度、实际进入及启动返回；启动脚本有消息框时，最后一项应在点击确认后出现。

UIKit 真机验收步骤：

1. 启动带长文本消息框的游戏，滑动正文并点击 OK，确认能继续启动。
2. 退出后重新启动，确认消息框仍能操作；若有多个按钮，确认各按钮返回正确结果。
3. 在游戏运行中打开消息框，确认正文滚动和按钮正常，关闭后剧情继续。
4. 消息框打开时切换后台再回到前台，确认仍能操作；导出日志检查启动返回及后续 heartbeat。
