# P2C C2A：消费端归因与安全像素访问边界

2026-10-08 本地实施与 portable 验证完成，未提交或推送。本轮完成消费端证据接口和 LayerExDraw 多余像素访问边界修复；具体算法 GPU 化、异步保存、C3/P2D 不在范围内。真实 plutovg、Apple 构建和新真机数据仍待验，没有性能改善结论。

## 基线与证据

| 仓库 | 实施前与签收 HEAD |
| --- | --- |
| 主仓库 | `349f8945d6052f30e259fca0741cb8efad22ca6f` |
| Runtime | `ddf2b44fb926b4d10c1d1b5b348bad7c2cdce50c` |
| Core | `5de79e271ba73eee3fec884d634b53cc23fa269b` |

两个子仓库开始时干净，主仓库只有上一轮已接受的八行 C2 文档补充，完整保留。原计划、三层 diff/status/HEAD、历史测试输出及四份 `perf_log/After_C1` 哈希先于源码修改保存；见 [manifest](native-metal-p2c-c2a-baseline/manifest.json)。

修改前使用已有二进制重跑：MetalLayer 像素/审计及版本检查、Backend、TJS 通过。解析测试首次因沙箱系统临时目录权限失败，改用 `TEMP/TMP=D:/vn-sim/build/tmp` 后通过；保留失败和重试原文，不将环境失败记为代码回归。

## 实际修改与兼容边界

LayerExDraw 原构造调用可写 reset，实例获取钩子又 reset；纯属性/状态操作也会取得像素并标记整图。现仅 Draw 选择元数据构造，其他 LayerEx 派生类的原 eager 行为保持。NCBind 调用策略默认关闭，只有 Draw 启用；固定 UTF-8 注册名在注册时保存，调用时借用，租约帧使用栈内 optional，不为关诊断的归因复制字符串或分配帧。

`updateWhenDraw`、`smoothingMode`、`textRenderingHint`、record 属性、文本测量和无 record 的 transform 仅刷新元数据。record 尺寸来自 native image metadata。普通 transform 历史上只更新矩阵，view transform 才可能 record 重绘，保留该区别；带 record 的相关入口保守取得可写租约。

绘制、clear、图片/文字、record 重绘及保存保持原 CPU plutovg 算法和保守写域。saveRecord 会继续调用 record 重绘，不能按纯只读导出处理。正常、参数转换异常和嵌套 record 操作均释放正确纹理；内部重绘共享一次租约。外部参数 getter 重入后若更换/resize 目标，内层取得当前纹理，外层 canvas/元数据/租约挂起并在返回或异常时恢复。

保留缓存 canvas 的历史 Clip 状态；每次实际使用前验证内容身份、buffer、pitch 和尺寸。Metal 使用只读稳定 content key，无额外 GPU 资源引用；不支持稳定 key 的软件纹理用不制造 COW 的 CPUAccessRef 保证缓存包装器寿命，并在重绑/销毁时释放。新增 scoped pixels 的借用 Texture() accessor，不改变其布局或 iTVPTexture2D 虚接口。

兼容审计额外覆盖：原参数数量拒绝顺序、拒绝属性/空 objthis、membername 转发、错误 dispatch API，以及 raw/bridge 首次访问的缺实例错误。raw/真正 bridge 只查已有实例，不能由新策略自动创建实例而改变原错误。已有 `GdipImage(surface)` 逃逸别名语义保持，未改为 snapshot，也不宣称修复其全部生命周期风险。

## 有界诊断与分析

复用现有 native console 通道：`metal.cpuConsumer` 分 read/caller/budget，`metal.cpuProducer` 记录成功 shrink 输出。包含 generation、会话 ID、texture/contentVersion、方法/入口/访问类别、源归因、尺寸、最后写入者/矩形及已有 bytes/wallNS/waitNS。提交与帧 ID 无现成数据时为 null；栈位置保持 unverified/unavailable。

每个现有 profile 窗口最多 32 条读回详情、32 条 producer 和 8 条调用栈。调用栈原始及转义长度均不超过 512 bytes，每条 native 消息不超过 900 UTF-8 bytes；超限和采样超过预算显式报告。方法/来源等标签超长或非法 UTF-8 拒绝记录，不截成可能误匹配的名称。预算中的 readRecords/producerRecords 为获准槽位，含拒绝的大记录；分析器保留 oversize 与缺失详情的区别。

只有真实成功 whole-texture Read 记录传输详情，缓存命中/失败不造样本。epoch 在传输开始捕获，切换诊断/会话后旧操作不进入新窗口。CPU shrink 必须显式标记计算成功，producer 在实际 dirty commit 后且纹理仍存活时记录；失败/空操作不伪造输出。GPU producer 在成功 ROI 提交后记录。

原 C0 名称、四指标、C1/C4 profile 和 C/Swift ABI 保持。诊断关闭不抓栈/分配日志字符串，不增加 GPU 查询、readback、submit 或 wait；读取的 timing 复用原测量。原生同步/真实执行证明继续待验。

分析器修复 `(WxH,lease=N)` 的逗号分隔，解析 sampled 消费端并按 generation/session/texture/contentVersion 与先前 shrink producer 精确关联；不按同尺寸或同窗口猜消费者。不把详情/producer 再累加到 C0。无 C2 事件的旧日志标为消费者 unknown，不补零；超额、标签拒绝、重复/孤立 caller、缺失预算或覆盖不一致明确呈现。

## 本地验证

平台 Windows 11、MinGW GCC 13.2；MetalLayer 为 Release / Ninja，Backend/TJS 沿用现有构建配置。portable device-double，无 skip。

- MetalLayer 4/4，MetalRenderBackend 1/1，TJS shutdown 1/1。
- 32 项解析 fixtures，包括真实 lease/other 格式、版本/会话/epoch 错配、同尺寸非关联、采样溢出、重复/畸形/孤立记录、bounds 与旧日志 unknown。
- C2 消费者 helper 严格 C++17 `-Wall -Wextra -Werror`：嵌套、异常、开关/epoch、32/8/32 预算、UTF-8/转义与 900-byte 边界、标签拒绝及抛异常 callback。
- 编译真实 NCBind/TJS、注册包装器、生产 base/Draw 边界与 MatrixConvertor。canvas double 验证构造/纯方法零 acquire、一次绘制/内部重绘租约、Clip 历史、COW/替换/resize、参数转换及外部重入/异常、raw-first 与原错误顺序；不证明真实 plutovg 光栅化像素。
- 真实 Metal Layer facade 的诊断开关事务比较：精确输出、fallback/upload/readback/backend read 计数一致；一次 producer/成功 read，第二次缓存读取无详情，C0 不重复计数。Apple 分支同时断言 submit/wait 计数相等，尚未在本机执行。
- 原 C4 1962 案例、C1 60 精确对照及 12 故障事务、旧 78628 surface comparisons/三 sessions 均保留，容差未放宽。
- frame-timing、point-read trace debug/release 通过；完整生产 shrinkCopy、tjsNativeLayer、LayerExBase、BMP、TLG5/TLG6 六个 TU syntax 编译通过。
- 修正后的生产分析器重新读取四份未修改 After_C1 日志，303 窗口精确对账、无 issues；旧日志的消费者身份仍为 unknown。

日志、实际命令及哈希见 manifest；完整三层本地 patch 和所有新/修改源快照在被忽略的 `build/native-metal-p2c-c2a-baseline`。只读归因实现不构成同条件性能配对。

## 待验与下一批

本机无真实 plutovg/vcpkg Apple 依赖和 Swift/Xcode：完整 LayerExDraw 原生编译、真实 plutovg 前后像素 oracle、默认/强制 compute、device/simulator、App/保存返回语义和诊断开关 GPU 同步验证待 Apple 执行。

重采天使 1052×900、千恋 960×863/186×936 LayerEx，以及千恋两次 496×279 scanline。必须获取 producer→消费端的真实 identity/version 链路，区分保存小图、插件和后续绘制；CPU 保存允许具名的一次必要读回。报告整条 shrink→scanline→消费者的传输/等待、编码 CPU wall 和逐帧长帧，设备/OS/场景/输入/设置/分辨率/热状态/时长一致，每组三次。新的 GPU 算子和异步方案由这些证据选定。
