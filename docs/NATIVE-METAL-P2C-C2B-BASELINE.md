# P2C C2B：LayerExDraw 原栅格化与精确 GPU 合成

2026-10-08 启动，2026-10-09 本地签收。本批选择流程图的 `drawLine`、`drawPath`、`drawImageStretch`；保留原 plutovg CPU 栅格化和 paint 采样，新增独立 GPU span 合成。未提交或推送；Apple、原生 kernel 和匹配条件真机验证待验，不宣称性能改善或全部 C2 完成。

## 基线与选域证据

| 仓库 | 实施前 HEAD |
| --- | --- |
| 主仓库 | `3cc3971f518c12dea81acdced04d8bda5f3826b8` |
| Runtime | `023673063436e72d352920002f88412ceac0533e` |
| Core | `cb387a6524f9423e9f563428ba69d0c440deb133` |

三仓开始时干净。三层状态/diff、原计划、历史 LastTest 输出、平台及 After_P2C_C2A 三份日志哈希先于本批修改保存到 `build/native-metal-p2c-c2b-baseline`；历史输出不是本次重新构建结果。

选域来自 `sourceRevision=3cc3971f518c` 的 After_P2C_C2A 日志：千恋 drawLine 186×936、drawPath 960×863 的两次读回合计 4,010,304 bytes / 26.083958 ms wait；天使 drawLine 277×411、drawPath 与 drawImageStretch 1052×900 的三次读回合计 8,029,788 bytes / 118.050959 ms wait。调用链在 `scnchart_ui.drawLayer/drawLines/drawMiniMap/updateScrollView`。天使同 texture867 的两个绘制读回分别为 v3 与 v39，中间有 `layer.gpuRect` 写入，不合并为同版本缓存命中。

千恋两次 496×279 保存图与先前 shrink 的 generation/session/texture/version 精确匹配，消费者为 `Layer.saveLayerImage` / `MainWindow.saveBookMarkToFile`，合计 20.529250 ms wait。同步 CPU 编码/保存保留必要读回与完成/错误语义。9-nine 的 Copy 几何和大图 BoxBlurAlpha 拒绝单独留待参数/ROI 证据，不纳入本批优化或收益。三份 iPhone 日志热状态均为 2，每游戏仅一份、场景未配对，不能作性能前后基线。

## 实现与兼容边界

Runtime overlay 固定 [plutovg v1.3.3](https://github.com/sammycage/plutovg/tree/v1.3.3)，复用原 vcpkg baseline 的 SHA512；可选 span callback 默认关闭。原 blend loop 仍生成固色、渐变、纹理采样和覆盖率，capture 在任何目标指针运算/读写前分流，并立即复制借用的 source chunk。patch 增加 metadata-only canvas、逻辑状态/Clip 深拷贝及同尺寸 surface 重绑，保留上游许可。

新 span kind 与普通 Layer operation 分离：Unsupported=0、SolidSource=1、SolidSourceOver=2、ArraySourceOver=3、Count=4，C++/MSL 同源 `.def`。按原顺序形成 row CSR，一个 GPU 线程在 ROI 内逐像素执行该行有序 span；保留原 packed uint 运算、BYTE_MUL 取整、carry/wrap、alpha 快速分支及原始通道字节。不会把多次 paint 合成一个透明 overlay，也不采用 Metal fixed-function blend。

Metal 从实际 ROI 复制原始字节到私有缓冲，在独立输出 scratch 合成，最后仅提交 ROI。packet、row CSR、uniform 和两份 256-byte row-aligned scratch 受 64 MiB 预算约束；最多 1,048,576 spans、每行最多 512 references。Unsupported/Count/越界、CSR/采样越界和资源超限在目标写入前拒绝。复用既有 staging/command-buffer 生命周期与 submission budget，不增加强制 submit/wait。

NCBind 只有 Draw opt-in；安全原始参数先检查，再建立 targetless 事务。record、非原生参数转换、借用源、外部租约、逃逸目标 surface 别名、非有限或不安全采样域、超预算及 backend 不可用保留具名 CPU 回退。原参数数量、错误/对象查找顺序和 eager CPU 租约仍用于 legacy 路径。COW 不提前标记 ImageModified；GPU 成功后才保留新 canvas 状态并更新一次。捕获原 typed RectF 返回值，提交后才进行可能调用脚本的装箱，拒绝时不会重复构造返回对象。提交前拒绝恢复状态后 CPU 执行一次；真实目标开始写入后的异常失效缓存并向上传播，禁止重放。

`GdipImage(surface)` 的逃逸别名保持原引用语义；已被外部持有的目标 surface 在 COW/capture 前以 targetAlias 拒绝，借用 surface 源同样留在 CPU。GPU/CPU 来回切换保留同一图像的历史 Clip 交集，COW/resize/纹理身份变化仍按原规则重新绑定。

## 诊断与本地验收

新增现有 native 通道事件 `metal.layerSpan`：version=1、generation/trace、method、gpu/cpu/noop、reason、可用时的 session/texture/contentVersion、spanCount、sourceBytes、parameterBytes、scratchBytes。每个现有 profile 窗口最多 32 条，`metal.cpuConsumer` budget 尾部报告 spanRouteRecords/spanRouteExceeded；未知目标 identity 为 null，不填零。noop 不报告未发生的上传。

sourceBytes 属于 parameterBytes，scratchBytes 为每次调用所需的 GPU 工作区（可复用分配，不是总显存峰值或传输量）；均不重复累加到 C0 像素 read/upload。分析器按 method/route/reason 聚合，保留预算丢失、详情缺失和旧日志 unknown。诊断关闭无字符串生成或资源身份读取，不新增 GPU 查询/同步；C/Swift ABI、原 profile version 与 C0/C1/C4 字段保持。

平台 Windows 11、MinGW GCC 13.2；MetalLayer 为 Release/Ninja，Backend/TJS 沿用原配置，GPU 为 device-double，无 skip。最终结果与实际命令、日志、哈希见 [manifest](native-metal-p2c-c2b-baseline/manifest.json)。

- MetalLayer 5/5、MetalRenderBackend 1/1、TJS shutdown 1/1；原容差不变。
- 真实上游原版与 production patch 分别编译，六种固色/重叠/虚线端点及连接/渐变/纹理场景的 730,434 pixels 完整原始字节精确一致，captured spans 经生产 MSL 数学展开后逐像素与 CPU 相同。另验证 sticky abort、clone/rebind 以及极端采样拒绝且目标不变。
- 原版 C 公式约 236 万次精确对照，覆盖所有 coverage/source alpha 与任意 packed uint；CSR 顺序、非法 kind/范围/资源/预算、超大 pooled staging 实际分配均检查。
- 编译生产 Draw/NCBind/Gdip/Appearance/Path 代码与真实 plutovg；三个入口精确像素及 typed 返回值一致、零 target CPU acquire；拒绝 CPU 仅执行一次、noop、Clip 延续、COW/租约、record、借用源/逃逸目标、参数错误与返回值构造/异常都通过。Layer/texture 依赖为 double，原生 GPU 另验。
- 真实 facade 与生产 MSL device-double 检查诊断开关像素/路由一致，已驻留支持域无 target readback/上传、dirty 输入仅实际 ROI 上传、非法 kind/租约/格式拒绝、提交前不改目标及提交后缓存失效。Apple 分支 wait 断言未在本机执行。
- 34 项新旧解析 fixture、strict C++17 `-Wall -Wextra -Werror` 诊断 helper、frame-timing、point trace debug/release 通过；完整 native Layer、LayerExBase、LayerExDraw（已启用 overlay）TU syntax 通过。
- 更新后的生产分析器读取三份未修改 After_P2C_C2A：177 个窗口、零 issues；新路由字段在旧日志中为 unknown。旧记录不作 C2B 性能基线。

原版源码缓存逐文件 SHA256 验证，下载包 SHA512 验证，patch 不改变未涉及源码/文档。测试中发现的超密虚线超过 512 references、测试 TU 符号冲突及标量展开链接重复已分别通过拆分独立调用、隔离测试依赖类型和 inline 展开修正；没有放宽预算/像素容差或修改生产像素公式。原失败日志保留。

## Apple 与真机待验

待验证完整 App/插件、Objective-C++/MSL、默认及强制 compute、device/simulator、GPU ordering、诊断开关 submit/wait、保存同步返回和错误。重放千恋与天使流程图的上述尺寸和具体入口，确认支持域 `metal.layerSpan route=gpu` 且对应 LayerEx target 读回/整图 output upload 消失；CPU 路由逐个报告原因。记录其他必要 CPU consumer，不能因 origin 改名签收。本轮 HEAD 未改变，工作树构建须同时保存本轮 diff/包哈希，不能只凭相同 sourceRevision 将 C2A 与 C2B 包视为同一代码。

DRACU、天使、9-nine 的新配对采样继续匹配设备/OS、场景、输入步骤、分辨率、设置、热状态、时长，每组三次，报告整条调用链的传输、等待和原始逐帧 p50/p95/p99/长帧。没有配对数据不推断改善。本批不实施 C3/P2D、9-nine blur/geometry 或异步保存。
