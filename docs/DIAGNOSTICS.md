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
