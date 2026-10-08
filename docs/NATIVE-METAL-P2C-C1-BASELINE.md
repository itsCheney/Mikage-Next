# P2C C1：精确 shrinkCopy 与安全自别名

实施始于 2026-10-07；本地签收日期 2026-10-08。本地代码、审计和 portable 验证完成，未提交或推送。Apple Objective-C++/MSL、Swift/App 测试和新真机配对仍待验；没有性能改善结论。不实施 C2/C3/P2D。

## 基线与来源

| 仓库 | 实施前及签收 HEAD |
| --- | --- |
| 主仓库 | `e17c15526feaab202032f3850492f3166e591187` |
| Runtime | `172154210fb26611977c807ff2438f5ead8b0f35` |
| Core | `f77b2c9338e05379a46666e2a6a24b7182954887` |

实施前的三个 HEAD 与用户确认的干净基线一致；原测试输出、空 tracked diff、计划原文和输入日志哈希已保存。原 `before.status` 的实际采集时点在两个 C1 定义文件首次写入之后，包含 `LayerShrink.h`、`LayerShrinkDefinitions.def` 及父层的未跟踪标记，不能视作修改前的全空状态快照；本次保留这些原始记录，并在 manifest 中注明差异。完整元数据见 [before.json](native-metal-p2c-c1-baseline/before.json)。历史 C4 记录不覆盖。

输入为 `perf_log/After_C4` 三份 JSONL，均标记 `sourceRevision=56b2799f8068`。DRACU `07CBA6E4…` 的 `shrinkCopy.read` 为 82 次 / 5,158,656 bytes / 159,629,500 ns wait，另有一次目标读 / 608,667 ns；天使 `31B23C61…` 为 112 次 / 15,516,800 bytes / 591,393,915 ns，另有两次目标读 / 1,663,125 ns；千恋 `8E2A0EEA…` 有两次源读 / 16,588,800 bytes / 11,845,000 ns，另有两次目标读。

这些日志证明路径触发和等待，不能区分两个入口，也不能由同 texture-ID 的 read/upload 判定全部调用的依赖。DRACU/天使本批 thermal=2，千恋跨 0/1/2；正式配对必须重新采集。旧 logger 还可能在 1024-byte 字段边界截断结构化数据，历史逐帧完整性不能补造。

## 运算契约与安全域

独立 `.def` 保持 Unsupported=0、Area=1、Fast=2、Count=3；普通 Layer 注册名称、编号和描述符不变。backend 的能力/执行/最后结果接口默认不支持，没有新增用户开关。

Area 复用原 double 裁剪、向零截断 RtoL 和权重生成；裁的是 image bounds，不是 Layer ClipRect。横 pass 保存 raw unsigned RGBA sums，纵 pass 再加权、除原 total 乘积并转低八位。RGB 使用 tc/bc，alpha 使用 ta/ba，每项仍由 alpha 权重非零决定读取；step=-1 的重复端点保留。

Fast 按实际余块宽/高先水平整数 floor，再垂直 floor，输出 alpha=255。它不等价于一次二维平均。源在 resize 前保留，原 setImageSize 只调用一次，保留尺寸、位置、Clip/cache 副作用。

Area 保留 host unsigned long 的 32/64 位模算术。优先采用经过完整权重/分母/累加上界证明的 32 位管线；真正需要 64 位时独立使用 ulong 管线。按 [Apple capability table](https://developer.apple.com/metal/capabilities/) 检查 64 位能力，缺少能力且不能证明窄算术等价时 CPU 回退。两 pass 采用 ceil dispatchThreadgroups 与 tid 边界保护，不要求 Apple4 的非整组 dispatch。

GPU 仅接受 canonical native Layer metadata/resize 绑定、正确 ObjThis、同会话 RGBA8 和无 CPU 租约的资源。非虚只读 callback 查询不调用用户 getter/setter；自定义绑定保留原 CPU 路径。源用 AddCPUAccessRef 保活，不制造额外 COW；目标 COW/resize 后不重新取源。

真实同纹理输入的原 CPU 顺序是 y 外层、x 内层读后写。按非零 alpha-gated 实际读取构造 X/Y span，O(W+H) 检查它们与先前完整目标行、当前行左侧的交集。只有不依赖前面写入的情况走 GPU；否则 aliasDependency 回退。不同 bitmap 共享触发真实 COW 时保留旧源，Fast 尺寸变化同样保留旧源。

参数、横向 sums 和独立 RGBA 输出 scratch 合计限制 64 MiB；Fast 中间数据是 16-byte uint RGBA sums，Area 是 16/32-byte sums。参数使用既有 staging 生命周期；GPU scratch 完成后才 blit 实际 ROI，部分覆盖保留外围像素。失败已编码的临时工作仍计入既有提交预算，未绑定 staging 从 pending 移除；不增加专用 submit/wait。

precommit false 可 CPU 续跑，Fast 不再 resize。postcommit 异常先依据 Applied 状态失效目标 CPU/point/alpha cache，再向上传播，禁止 CPU 重跑已写结果。

## CPU 安全修复

- 两轴表原分配 `w*h`、使用 `w+h`，改为 checked `w+h`，消除单行、单列输出越界。
- 负 step 左移改 checked 乘法；仅在实际 alpha gate/循环需要时形成采样指针，避免无效端点和最后一次递增越界。
- 非有限参数、long/偏移/分配溢出、非 RGBA、采样越界和模分母零在像素写入前失败。
- Fast 临时行容量取实际最大消费行数，避免巨大 step 的无用申请；原像素算术、余块和返回成功条件保持，失败不报告成功。

## 诊断及日志闭环

C0 workProfileVersion=2、原 origins/top-N/frame samples 和 C4 version=1 保持。新增 shrinkProfileVersion=1：32 条按入口/原因/别名聚合的记录及明确 overflow；包括 GPU/CPU/noop、像素、CPU inclusive wall/prep、传输四指标、参数上传和临时字节。不是 GPU 时间。

每窗口最多记录 2048 个有效 ShrinkScope 内真实成功 read 的 waitNS；缓存命中、失败、旧 generation 不造样本，真实成功零 wait 可记录。分析器用原始单次样本计算 nearest-rank p50/p95，继续合并原始 frame samples 计算 p50/p95/p99。

新 C 尾部经只读 accessor 向 Swift 复制，保持 NUL/version/count 边界，host/App 必须一起重建。当前 bridge sizeof=267864 bytes，边界及最大数值测试通过。

末端审计发现 DiagnosticLog 原来把所有字段剪至 1024 UTF-8 bytes。现仅对 layerWorkProfile 的十个已知字段使用生产契约上限完整保留，超限整值省略并记录 fieldsValueOmitted/Keys；普通字段、queue、排序、fieldLimit 和 maxBytes 整记录防护不变。新日志带 structuredFieldLimitsVersion=1，App 同时输出 frameSampleCount。分析器校验数量，并报告省略、整记录截断、记录丢失和旧字段恰好 1024 bytes 的可能截断风险；不把历史数据恢复成完整样本。

## 本地验收与待验

Windows 11 / MinGW GCC 13.2 / portable device-double：

- MetalLayer 4/4、MetalRenderBackend 1/1、TJS shutdown 1/1；无 skip。
- 60 个真实插件精确对照，含 source/output 单轴为1、分数/负坐标、四边裁剪、step=-1、RGB/alpha不同权重、32位wrapping、真正64位累加、1280×720→128×72、1920×1080→496×279。
- 2048 组轴别名证明与逐像素读写依赖对照；安全/不安全自别名、真实 bitmap COW peer、Fast同层resize旧源、CPU lease和无效参数回归。
- 12 个 precommit CPU续跑、postcommit异常、64位不可用/可降32事务测试；Fast callback仅一次，结果精确，提交后无 CPU replay。
- 原 C4 1962 案例、旧 78628 surface comparisons/三 sessions 和各阶段回归保留，容差未放宽。
- 25 项解析 fixtures、诊断/真实 C bridge严格警告、frame-timing检查通过；完整生产 shrinkCopy.cpp 和 tjsNativeLayer.cpp syntax编译通过。

原始命令、日志、哈希和新增源文件快照见 [manifest.json](native-metal-p2c-c1-baseline/manifest.json)；三层完整本地 patch 和源快照保存于 build/native-metal-p2c-c1-baseline，不提交或推送。

仍须 Apple 执行：Objective-C++/MSL两种整数管线、默认/强制compute、真实GPU失败及staging生命周期、Swift/C importer、App XCTest和新增四个真实 DiagnosticLog export测试。本机没有 Swift/Xcode，不宣称已执行。

真机按 DRACU、天使、9-nine 固定存档/步骤、设备/OS、三层完整版本、包哈希、分辨率、设置、预热、热状态及时长，每组三次。输入驻留后，支持域要求 shrink readback/wait=0、无CPU输出整图upload，必要输入/参数另计；所有保留CPU域须具名。记录 per-read wait、frame分位数、超过50ms次数和覆盖/丢失。没有同条件新样本，不推断FPS、温度或功耗收益。
