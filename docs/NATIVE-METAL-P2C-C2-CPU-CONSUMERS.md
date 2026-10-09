# P2C C2 后续：CPU 消费链与像素往返收敛

2026-10-09。归属兼容性计划的 **P2C C2 / G2**，承接已实现的 C2A 归因和 C2B span 合成；本文件安排后续范围与验收，尚未实施这些优化。普通 Layer overdraw 仍归 P2D，tiny update batching 仍归 C3，Nekopara 继续延后。

## 证据与优先级依据

来源是 `sourceRevision=e3b0a5e04488` 的 After_P2C_C2B-3 三份 iPhone 日志。精确计数、单位、输入SHA256及旧窗口来源见 [observations.json](native-metal-c2-cpu-consumers/observations.json)，原始日志保留本地不改写。

天使补抓 [BD7898C1](../perf_log/After_P2C_C2B-3/Mikage-diagnostics-BD7898C1-A99B-4E2F-94C1-81A739117769.jsonl) 的82个完整窗口共320次路由：GPU132、CPU188；CPU为record126和vectorSource62。首个71次窗口由旧GPU4/CPU67变为GPU37/CPU34，原arguments33已成为GPU drawPath，路由参数补修得到样例验证。可是同构窗口新旧都读回3次、8,029,788bytes，首次消费者由drawPath移到drawRectangle，后续CPU消费仍使目标取回像素。

本次继续滚动与再次绘制后的LayerEx完整C0总量为 **66 read / 243,585,196 bytes / 486.163046 ms wait**，以及66 upload / 243,585,196 bytes。以下逐方法详情是已采下界：

| CPU 消费者 | 已采 read | bytes | wait ms | 解释 |
| --- | ---: | ---: | ---: | --- |
| drawImageStretch 1052×900 | 56 | 212,083,200 | 178.598253 | vectorSource重放/合成是主要传输候选 |
| record域drawPath 1052×900 | 2 | 7,574,400 | 89.286875 | 125次CPU路由不等于125次实际回读 |
| drawRectangle 277×411、277×676 | 2 | 1,204,396 | 171.085168 | 字节较少，等待仍显著；是GPU绘制后的下一CPU消费者 |

独立C2A预算省略6条read、31条caller、43条producer。6条read的C0余量为22,723,200bytes / 47.192750ms，逐方法归属未知，不能补给drawImageStretch。C2B v2路由聚合完整：12代表＋308重复省略，容量省略为0；C0总量完整。两个预算必须分开解释。

设置场景的clear也要纳入C2消费链，而非用三个绘制入口的GPU比例判断：

| 日志 | clear消费链 | bytes | wait ms |
| --- | --- | ---: | ---: |
| [千恋0D60ECF6](../perf_log/After_P2C_C2B-3/Mikage-diagnostics-0D60ECF6-DEF2-49EE-B38E-2A87299DB634.jsonl) | HSVColorPicker构造5次read（3×166×166、2×16×16） | 332,720 | 63.780792 |
| [天使23C12AEE](../perf_log/After_P2C_C2B-3/Mikage-diagnostics-23C12AEE-57C2-4836-94B8-98645DEA54C4.jsonl) | 两组HSVColorPicker构造，共10次read（每组3×306×306、2×24×24） | 2,256,480 | 233.886794 |

本批场景/时长不同，BD7898C1热状态经历0/1/2，另外两份为2；这些是路径与成本证据，不是受控性能基线。已有GPU命中不代表整条插件链零回读，等待集中也不直接证明是哪项GPU任务耗时。

## 后续范围与依赖

默认先设计vectorSource的安全重放与GPU合成；录制元数据生成可以继续CPU，不要求先把record全域GPU化。record与drawRectangle按真实调用依赖拆分；clear按设置场景单独验证。每块只扩大可证明的支持域，分批交付，保留普通Layer名称/编号与现有安全拒绝。

| 子域 | 拟实施工作与前置证明 | 专项完成标准 |
| --- | --- | --- |
| vectorSource / drawImageStretch | 审计已有录制命令、原plutovg重放与采样；评估在不可变录制快照上生成有序span并GPU合成，保持源版本/寿命、变换、Clip、alpha及paint顺序。借用/逃逸资源或无法固定的输入保留具名CPU路径。 | 已驻留支持域不因重放获取目标CPU像素、不生成整图CPU output upload；输入/参数上传另计，原重放结果、区域外像素和返回值精确一致。 |
| record模式 | 拆分纯命令构建、元数据查询与实际目标像素写入；仅记录而不消费像素的步骤不acquire目标。需要同时录制与绘制时，明确record状态的事务副本及提交/恢复点，保持getRecordImage、重绘、保存和后续复用的命令/顺序/生命周期。 | 录制内容、导出/重放和屏幕结果一致；失败不多记或漏记命令，提交前CPU仅续跑一次，提交后异常不重放；按实际read计量，不用record调用数推算总线流量。 |
| drawRectangle | 在真实注册与最小参数契约下评估复用原plutovg栅格化＋span合成，保留画笔宽度/端点/连接/覆盖率、坐标、历史Clip、record与租约规则。沿drawLine/drawPath→drawRectangle链核对同内容版本的消费。 | 支持域该消费者不再令目标回CPU；精确像素、typed返回、更新/装箱次数及ROI外内容一致；链路总体read/upload/wait减少，不能只把归因从前一方法移到后一方法。 |
| clear与紧随的绘制 | 先证明原调用是否完整覆盖目标、是否依赖旧像素，以及其后是否立即有CPU绘制。完整覆盖且后续仍CPU时评估安全overwrite租约；连续可GPU化的段评估GPU clear＋后续绘制。部分覆盖/Clip/record/COW未证明时保留旧值和原回退。 | clear后所有消费者的像素与可见时序一致；不为完整覆盖写读取无用旧像素，也不因单独GPU clear在紧随CPU阶段产生新的往返；保留Layer/Clip/cache/alpha副作用及同步CPU指针契约。 |

上述方案不以删除record/vectorSource拒绝条件代替证明；CPU原算法、目标读依赖、alias、租约、参数资源与预算的边界仍有效。GPU路径继续采用独立scratch/事务输出与既有提交预算，不能用snapshot改写有顺序依赖的结果。

## 诊断与必要CPU边界

- 更细定位时补齐C2A的有界消费者聚合/代表覆盖：完整累计实际成功read的calls/bytes/wallNS/waitNS，代表保留纹理/会话/版本及可用调用链；重复与容量省略显式区分。复用现有测量，不增加GPU查询、readback、submit或wait，不保存额外资源强引用；保持generation/windowID及现有诊断开关约束。无需将日志改为无界逐调用dump，缺失调用者保持未知。
- 已完整的C2B路由与C0 origin总量继续对账，CPU消费者详情仅定位，参数与scratch不重复计入像素传输。诊断缺口不妨碍当前优先级判断，但有缺口时不得签收“逐调用已完整归属”。
- `alphaToProvince` 在BD7898C1有一次1721×284、1,955,056bytes / 20.440875ms wait，先确认province/命中测试要求的通道、ROI与同步可见性，再评估按有效版本复用、GPU通道提取或区域化必要读回；不承诺必须返回CPU的结果零回读，不改省图值或调用返回语义。
- 保存缩略图仍是明确CPU编码/保存边界：千恋两次496×279合计1,107,072bytes / 18.518959ms wait，BD7898C1天使一次553,536bytes / 9.127500ms。沿用C1/C2的shrink→scanline→保存验收，保持同步完成/异常、有效版本缓存及跨会话释放，不能用跳过保存降低统计。
- `saveDataPack`缺失导致系统变量保存失败，属于独立功能兼容任务；与消费端优化分别验收。先确认接收对象/预期序列化接口，再修复并验证数据落盘与重启恢复，不添加空方法、提前成功或吞异常来伪装优化。

## 整链路验收

1. 用原plutovg/真实生产绑定作oracle，覆盖变换/缩放、透明/覆盖率、Clip/ROI、record导出与重放、COW/共享源/错位alias、租约/逃逸指针、会话切换及各阶段失败。原精确容差不放宽；软件返回值、异常顺序、装箱/更新次数和中间可见结果保持。
2. 同一可重放负载同时报告GPU/CPU/noop路由、各CPU消费者、C0总read/upload calls/bytes/wait、参数上传与GPU临时需求。按generation/session/texture/contentVersion连接实际调用序列，不能只凭尺寸或总量相等合并资源，也不能把缺采样补零。
3. 已驻留GPU支持域要求整个选定绘制链无目标readback/整图CPU output upload；保留必要CPU算法的链只发生经证明需要的区域/版本传输，并明确不可消除的原因。对比CPU overwrite与连续GPU方案时纳入其后的消费者，禁止仅把等待或上传转移到未计量路径。
4. 诊断开关不改变像素、路由、backend调用或同步；优化不新增强制submit/wait来换取表面命中。保持GPU前失败的唯一CPU续跑、GPU后异常传播、资源预算与后台/退出寿命。
5. 运行所涉portable回归与原生Apple kernel/默认及强制compute/device及simulator/App检查；真机按相同设备/OS、场景、输入步骤、分辨率、设置、热状态与时长每组三次。合并原始帧样本计算runtime interval与CPU wall的p50/p95/p99和长帧，另报GPU时间；无配对不宣称性能改善。

当前只签收参数误拒绝在样例中的修复与路由统计完整性，以上CPU消费链优化及完整C2/性能签收均为待办。
