# 原生 Metal 兼容性补齐计划：对照 Kirikiroid2 OpenGL 的源码审计

日期：2026-10-05；更新：2026-10-10。状态：**P0 已验收；P1A/P1B/P2A/P2B 已提交；P2C C0 真机 origin 对账通过，C4 有实际 GPU/零 CPU 样例，C1 本地实现与 portable 通过。C2A/C2B 及参数/HSV 补修已发布，After_P2C_C2B-3 确认参数支持域 GPU 命中与完整路由对账；C2 剩余 vectorSource/record/drawRectangle/clear 和成功 read 聚合本地实现与验证完成。C3 字形 atlas/tiny update 合批本地实现及 portable 验证完成，未提交。完整 Apple/App/原生矩阵及匹配真机重采/性能配对仍待验，C2 更广消费域、C3 原生/真机、P2D 设备归因/原生与配对、P3–P5 未完成；P2D D0/D1 首轮本地实现及 portable 已完成，未提交。**

2026-10-09 发布补记：用户授权本轮提交推送，Core `5e54d4678119c79848f6aab9d759550c8ff0a663`、Runtime `b6f31a2f892a0f4e20641471a6c361d2b899ac37` 已顺序发布并核对远端；主仓库测试/证据与指针由包含本记录的提交发布。下文“本轮未提交”的本地验收历史以本发布补记更新；不跟踪CI，等待用户反馈构建或设备异常。

## 1. 决策与完成目标

继续以原生 Metal 为正式后端。Kirikiroid2 的 OpenGL RenderManager 用来发现遗漏的能力、理解成熟的调用语义和构造兼容性样例；当前软件实现提供可执行的像素参照。遇到两者不一致时，先裁决行为，再实现 GPU 路径。

本计划的完成目标分为三个层次，不能以一个“覆盖率”代替：

1. **当前引擎的普通 Layer 能力**：补齐当前已注册软件方法的 Metal 路由及有效参数域，扩大 affine/perspective 的 GPU 支持，减少可避免的软件回退和像素传输。
2. **旧 GL 的额外兼容能力**：对当前未注册的染色、AlphaTest 等方法明确支持契约；只在确认调用方需求后接入。动态 GLSL 不纳入本轮默认承诺。
3. **真机性能与稳定性**：在同一设备、游戏、场景和设置下验证画面、输入、帧耗时、GPU 阶段、内存和长时间运行。GPU 算子覆盖增加本身不能证明持续卡顿或发热消失。

特别地，`layerCPUFallbacks=0`、GPU 算子 reject 为零、以及像素正确，都不能推出没有 CPU↔GPU 传输：上层仍可主动取得 scanline、lock 或 raw pointer。本计划将“扩大 GPU 算子支持域”与“消除已证实的 CPU 像素边界”分别验收，不能用其中一项替代另一项。


## 2. 审计基线与证据边界

### 2.1 固定版本

| 对象 | 本轮核实的版本 | 用途 |
| --- | --- | --- |
| 主仓库 `vn-sim` | `1d0a4358fda835926beda6247ecd46054bd91dc8` | 测试、诊断文档、iOS CI |
| Runtime/host 子仓库 | `e0bb78a91fa0549a1588d751334ef7b01e68b595` | 构建与宿主边界 |
| Core 子仓库 | `49bf9f70fb4866515a26e6dd9a7aa89fa2447083` | 本地 Metal、软件、GL 接口 |
| Kirikiroid2 | `d1c2b1259423542c893e0b65eaeb46c848848f2b` | 旧 GL 实现、接口和测试代码 |

审计开始时，主仓库及两个嵌套仓库的工作区检查均无改动。本轮只新增本计划；参考源文件下载到被忽略的 `build/kirikiroid-reference`，没有导入运行时代码。

旧 `RenderManager_ogl.cpp` 原始文件 SHA-256：`9d9a674b88b8136ca796524878dd548714f2c37d0afd7ca375b0f588606ebeff`。按文本行分割为 4,032 行；行数差异不作为能力判断依据。

### 2.2 核对方式

- 读取当前生产源码，而非仅依据文档或上一段对话。
- 下载固定提交的旧 GL `.cpp`、旧 `RenderManager.h`、`RenderManager_ogl_test.hpp`，核对注册、别名、几何、回读和测试容差。
- 静态提取旧文件中 `CompileRenderMethod`、`CompileAndRegScript`、`CompileAndRegRegularBlendMethod`、`RegisterRenderMethod` 的字面名称；去除注释、去重并合并条件分支。
- 展开当前软件注册的 `REGISER_BLEND_*` 宏，核对共享方法对象及 `ConfigureGpuOperation()` 的首次配置规则。
- 阅读现有测试实现和 CI 配置；**本轮没有重新运行 CTest、Apple 构建、旧 GL 程序或 iPhone 场景**。源码中存在测试，不等于当前提交在真实设备上已通过。
- 注册名称统计只代表名称与语义描述符的静态关系；不能代表所有输入、几何、格式和设备都能 GPU 执行。

### 2.3 主要源码索引

下文证据编号均指向固定提交；本地相同文件可用于后续修改。

| 编号 | 源码与范围 |
| --- | --- |
| K1 | [旧 GL 方法注册、混合、转换和 blur][k-register] |
| K2 | [旧 GL scanline 回读][k-readback] |
| K3 | [旧 GL target-as-source/framebuffer fetch][k-fetch] |
| K4 | [旧 GL 三角形、透视、stencil][k-geometry] |
| K5 | [旧 RenderManager 接口][k-interface]、[旧 shader 测试][k-tests] |
| C1 | [当前方法注册与 GPU mapping][c-mapping] |
| C2 | [当前 Layer manager 路由及回退][c-manager] |
| C3 | [当前 Metal 资源与算子实现][c-metal] |
| C4 | [当前 MSL 像素、tile、blur、transition、affine 实现][c-shaders] |
| C5 | [当前软件几何实现][c-software-geometry]、[图像数学与 blur 工具][c-imageutils] |
| C6 | [当前操作结构和统计枚举][c-operation]、[后端接口][c-interface] |
| C7 | [当前 RenderManager 接口和参数机制][c-render-interface] |
| C8 | [软件函数绑定][c-blend-bindings]、[tvpgl 实现][c-tvpgl] |
| T1 | [普通 Layer 测试][t-layer]、[测试构建配置][t-layer-cmake] |
| T2 | [Metal backend 测试][t-backend]、[iOS CI][t-ci] |
| D1 | [第三批性能核对与已实施优化][d-third]、[此前 Layer 性能跟进][d-followup] |

### 2.4 2026-10-06 诊断样本：P2C 的证据与边界

以下是上传诊断日志的**现象基线**，不是对当前工作区或所有游戏的性能承诺。六个横向样本均标记为 `sourceRevision=6e5cbf10e7dd`；实施前必须在当时的三层 HEAD、同设备、同场景重新采样。它们的共同结果是 `layerCPUFallbacks=0`、GPU reject=0，因此这里的传输不是普通 Layer Metal 算子被拒绝后的 software fallback。

- 一个 transition 场景累计 upload 1,176.45 MiB、readback 171.45 MiB；其中 `transition.outputOverwrite` 为 133 次/1,052.05 MiB（89.43% upload），`transition.source` 为 16 次/158.20 MiB（92.28% readback）。该采样还记录 26 次同步等待、430.63 ms。它对应传统 `DivisibleTransHandler` 经 scanline 在 CPU 处理后整图写回的路径；现有 overwrite 优化避免了旧目标 readback，但不能避免每帧整图 upload。
- 六个不同游戏样本没有 `transition.source` / `transition.outputOverwrite`，故 transition 是严重但触发相关的路径，不能被用来解释所有游戏的卡顿。`shrinkCopy`/同类 lock readback 则更普遍：DRACU-RIOT! 为 155 次、5.74 MiB、271.4 ms wait；天使纷扰为 78 次、21.95 MiB、486.5 ms wait；9-nine 为 12 次、3.94 MiB、40.2 ms wait。小至约 13 KiB 的读取也观察到 10–40 ms 等待，优化目标不能只按 MiB 排序。
- 天使纷扰记录了同一张 1052×900 texture 的 `layerExDraw.write` 与 `layerExBase` 两次完整 GPU→CPU→GPU 往返，单轮约 14.45 MiB；其中 `layerExBase` readback 的 GPU wait 约 40.92 ms。这是 raw-pixel API 与 GPU-authoritative texture 交替使用的可复现样例，不应笼统归为“插件少量读回”。
- 六个样本还出现 7,304–32,131 次 `bitmap.update`。常见单次约 500 bytes，但部分样本达到约 4,650 次/s，且与 `blitEncoders` 高度相关。这是 encoder/命令编码 churn 的强信号，不足以单凭相关性断言调用链；P2C 必须先补齐 origin 与调用点证据，再做批处理。

该版本诊断的 texture-ID top-N 输出会把多个来源折叠成 `read:other`/`upload:other`。例如天使纷扰中 `read:other` 为 67 次/4.58 MiB/289.2 ms wait；在按 `layerReadbackBySource` 的统计中，同一采样却显示 71 次 lock readback。这是 C0 增加有界 origin 聚合的历史依据；C0 后的对账与最新优先级见第 2.5 节，不再用旧 `other` 推断当前来源。

### 2.5 2026-10-07 C0 后真机日志：归因签收与优先级调整

来源：[查看来源对话的最新一轮](chatgpt-conversation://6ac4d8a3-e2a4-83e8-ac29-040033fed205)所附 8 份原始 JSONL。本轮实际读取附件，按 `layerWorkProfile.transferOrigins` 汇总并核对最终 heartbeat；8 份均为 iPhone、`sourceRevision=4943c0e47665`，与本轮主仓库 HEAD 的短标识一致。该标识不替代 runtime/core 完整 commit、构建配置和包哈希。

| 游戏 | Upload（MiB） | Readback（MiB） | 同步等待（ms / 次） | 主要 readback 来源 | `bitmap.update` 次数 |
| --- | ---: | ---: | ---: | --- | ---: |
| 天使纷扰 | 786.1 | 20.88 | 432.3 / 75 | shrinkCopy 12.79 MiB / 368.2 ms；LayerEx 8.09 MiB / 64.1 ms | 18,308 |
| DRACU-RIOT! | 524.8 | 84.60 | 310.1 / 97 | transition 79.69 MiB / 132.9 ms；shrinkCopy 4.92 MiB / 177.2 ms | 27,020 |
| 9-nine | 386.7 | 3.94 | 47.6 / 12 | 全部 shrinkCopy | 30,635 |
| 少女领域 | 160.8 | 3.59 | 2.48 / 2 | 全部 shrinkCopy | 26,580 |
| NEKOPARA Vol.4 | 188.7 | 0 | 0 / 0 | 无 | 7,931 |
| 冥契的牧神节 | 777.1 | 0.078 | 50.6 / 1 | `bitmap.scanline` | 4,506 |
| 青空下的加缪 | 282.8 | 0.024 | 1.43 / 1 | `bitmap.scanline` | 32,364 |
| 千恋万花 | 2,398.3 | 426.6 | 859.7 / 50 | transition 400.78 MiB / 763.4 ms；LayerEx 8.98 MiB / 80.1 ms；shrinkCopy 16.88 MiB / 16.1 ms | 53,983 |

表中 shrinkCopy 合并 `read:shrinkCopy.read` 与 `read:shrinkCopy.write`，LayerEx 合并 `read:layerExBase` 与 `read:layerExDraw.write`；upload/read 分方向计量，不重复加入 texture top-N。数值四舍五入，精确对账使用原始 bytes/ns。

**C0 的真机归因门槛已通过：**8/8 日志的 origin upload/read bytes 与最终 `layerUploadedBytes` / `layerReadbackBytes` 逐字节相等；read calls / waitNS 与 `metalProfile.syncWaits` / `syncWaitNS` 精确相等。8 份所有窗口的 read/upload overflow、capacityRecords、oversizeRecords 均为零。千恋为 50 次 / 859,671,544 ns，天使为 75 次 / 432,325,836 ns。旧 top-N 中仍可存在 `other`，但完整 origin 视图已能承担总体归因。对账证明统计覆盖与一致性，不证明诊断开关零开销、无额外提交，也不替代 Apple 专项测试。

最新证据将默认实施顺序改为 **C4 CPU transition → C1 shrinkCopy → C2 LayerEx → C3 bitmap.update batching**，保留原编号以对应已有基线和提交包：

- **C4 首先实施。**千恋的 `transition.outputOverwrite` 为 263 次 / 2,080.37 MiB，即 263 × 8,294,400 bytes，全部为 1920×1080 输出；`transition.source` 为 40 次 / 400.78 MiB / 763.42 ms wait。分别占该样本 upload 约 86.7%、readback 93.9%、read wait 88.8%。DRACU 也有 17 次 source read / 79.69 MiB / 132.93 ms wait，以及 14 次 output upload / 49.22 MiB。因此 transition 在多个游戏中已真实触发，虽非所有场景持续存在，仍应先以千恋为主回归、DRACU 为交叉回归。
- **C1 其次实施。**天使的 `shrinkCopy.read` 为 69 次 / 12.260 MiB / 365.516 ms wait，`shrinkCopy.write` 的 read 为 2 次 / 0.530 MiB / 2.701 ms。DRACU shrinkCopy 仅约 4.92 MiB，却比 79.69 MiB 的 transition 等待更久；9-nine 的 12 次 read / 47.612 ms 全来自 shrinkCopy。收益应按同步延迟与长帧评价，不能按 bytes 排序。
- **C2 保留同纹理往返回归，并扩展到千恋。**天使的 `layerExBase`、`layerExDraw.write` 各为 2 次 / 4.046 MiB read，对应 `upload:mixed`、`upload:layerExDraw.write` 各 4.046 MiB；1052×900 样例仍存在。千恋也有 8.98 MiB / 80.1 ms LayerEx read。origin 归因与 texture 时序应一起使用，不能仅由聚合量相等推断每个调用的依赖。
- **C3 有充分立项依据，调用链仍需审计。**8 个游戏全部出现 tiny update；千恋 53,983 次 / 53.32 MiB，累计 measured wall 1.709 s。最新对话按同 heartbeat interval 对齐得到 update 与 blit encoder 相关系数约 0.927–1.000；这是运行时相关性，尚不证明字形调用方或每次 update 必建 encoder。沿 `UpdateLayerTexture()`、staging、`blitCommandEncoder`、`endEncoding()` 核对后再批处理，累计 wall 不直接视为独占 CPU encoding 时间。

**独立性能问题与测试限制：**NEKOPARA 本次 readback/wait 均为零，但最新对话仍报告约 38 FPS、26.35 ms frame time、44.36 ms GPU submission，以及大量 Emote mesh/capture 工作；应单独调查 Emote GPU 负载，不能承诺 P2C 会解决这个场景。冥契仅一次 81,748-byte scanline read 就等待 50.564 ms；另有约 450.85 ms `queueWaitNS`，应分别审计 scanline 调用与 command/in-flight queue，不能将 queue wait 算进 read-origin wait。

原始日志中青空的 `thermalState` 从 0 经 1 到 2；其余七份从首个 heartbeat 起均为 2。本批可作为归因与路径触发基线，**不能作为同热状态的最终 benchmark**，不得据此比较游戏间 FPS 或计算优化前后性能百分比。C4/C1 实施前后须重采相同场景与热状态，优先回到 `0/nominal`，保留沿途 thermal 变化及完整逐帧样本。

### 2.6 2026-10-07 C4 后 iPhone 日志：transition 证据与 P2D 触发条件

来源为[查看来源对话](chatgpt-conversation://6ac4d8a3-e2a4-83e8-ac29-040033fed205)随后上传的三份 JSONL；均标记 `sourceRevision=56b2799f8068`。该短 revision 是日志数据，当前计划未据此推断主仓库、runtime/core 的完整 commit 对应关系；后续复现仍须保存三层 HEAD、包哈希、设备、OS、渲染开关、存档和操作步骤。

| 游戏 | transition 实际执行 | 总 readback / sync wait | 本轮可确认的结论 |
| --- | --- | ---: | --- |
| 千恋万花 | 545 frame / 545 GPU / 0 CPU（crossfade 411、wave 110、universal 17、mosaic 7） | 21.36 MiB / 36.75 ms | transition 无像素 readback/整图 output upload；剩余为 shrinkCopy 和少量 LayerEx |
| DRACU-RIOT! | crossfade 449 / 449 GPU / 0 CPU | 4.95 MiB / 160.24 ms | 82 次 `shrinkCopy.read` 为 4.920 MiB / 159.63 ms，已是 C1 的干净回归样例 |
| 天使纷扰 | 363 frame / 474 GPU call / 0 CPU（crossfade 137、universal 226） | 23.42 MiB / 656.72 ms | transition 无像素往返；仍有 shrinkCopy、LayerEx，且发现独立的 P2D GPU fragment 瓶颈 |

**C4 的本轮证据范围：**上述三条路径均没有 `transition.source` readback 或 `transition.outputOverwrite` full-frame upload；`wave` 约 485 KB 的参数上传属于允许的参数数据，不是 framebuffer 往返。这证明实际触发的 handler 已走 GPU，不能外推为所有七种 handler、所有规则/尺寸、或所有设备已经最终验收；这些仍按 C4 的原生、像素、生命周期和同热状态门槛继续验证。

**P2D 的触发证据：**天使纷扰“走路/道路滚动”期间，`街_通学路a.png`（2120×1280）在约 71.816 s 加载，约 74 s 后进入低帧区；76.85 s 左右记录到 20 Layer dispatch、9 个 render encoder、约 31.34 M `rectPixels`（约 15 个 1920×1080 全屏等价）、2.765 M scaled pixels、2.074 M alias pixels、`otherFragmentMS=73.138`、GPU command 131.662 ms。此时 `gpuSyncWaitMS=0`，但 `nextDrawable` 等待约 20–65 ms，最低瞬时帧率约 14.4；动画结束后 GPU command 约 1.364 ms、`rectPixels=0`，帧率恢复约 55–60。

这确认的是**普通 Layer fragment/overdraw 导致的 GPU-bound presentation backpressure**，不是 C4 CPU transition、transition transfer、shrinkCopy、image decode 或脚本 CPU 时间。`街_通学路a.png` 与 hotspot 的时间/语义关联很强，但现有诊断尚未输出 texture/resource ID → asset → Layer object 映射，不能仅凭该样本断言具体哪个 Copy/Fill/Alpha 操作使用了该资产。thermal state=2 会放大同等工作量的波动（约 65–132 ms GPU time），但不是道路动画开始时陡降的充分根因。

### 2.7 2026-10-09 C2_1 后 iPhone 日志：发热来源与 pass 碎片化证据

来源为 After_P2C_C2_1 的五份 JSONL，均标记 `sourceRevision=f20d0e10c275`，设备 iPhone 390×844 @3x、显示链路固定 60 fps。完整分析、调用链核对、分位数与拟合见[发热来源分析](NATIVE-METAL-THERMAL-ANALYSIS.md)，精确数据与输入 SHA256 见其 [observations.json](native-metal-thermal-analysis/observations.json)，由 `scripts/analyze-thermal-passes.py` 只读生成；跨 9 批 49 个会话的对照见同目录 observations-all-baselines.json。

| 游戏 | thermal（游戏开始后 s） | render enc/帧 | blit/帧 | `bitmap.update`/s（占 blit） | 抽样 gpuMS p50/p90/p99 | 主线程占用 | 整会话 sync / readback |
| --- | --- | ---: | ---: | ---: | --- | ---: | --- |
| 千恋万花 | 3.1→2 | 32.3 | 16.9 | 736（89%） | 7.9 / 47.4 / 95.9 | 31.5% | 2 次 19.7 ms / 1.06 MiB |
| DRACU-RIOT! | 2.8→2 | 43.2 | 18.8 | 820（86%） | 8.2 / 24.7 / 35.3 | 31.8% | 1 次 8.0 ms / 0.04 MiB |
| 天使纷扰 4E01 | 3.6→1，35.6→2 | 40.9 | 13.7 | 542（83%） | 6.5 / 42.1 / 149.0 | 35.9% | 3 次 125.6 ms / 6.00 MiB |
| 9-nine | 2.9→2 | 29.6 | 13.4 | 561（89%） | 5.0 / 23.6 / 35.1 | 31.2% | 1 次 2.9 ms / 0.05 MiB |
| 天使纷扰 EB1D | 3.6→0，8.6→1，48.6→2 | 25.4 | 7.9 | 325（84%） | 0.7 / 35.9 / 111.5 | 29.8% | 2 次 31.2 ms / 2.39 MiB |

"每帧"按整会话 encoder 总数除以 display tick 总数；command buffer 每帧恰好一个，pass 数不是多 command buffer 造成。抽样 command buffer 每秒一个，不能累加成 GPU 预算。

**本轮可确认的结论：**

- **C0–C2 针对的回读/同步已收敛，不是发热来源。**整会话同步等待合计 3–126 ms、readback ≤ 6 MiB、in-flight queue 等待 ≤ 0.4 s。
- **发热来自每帧持续的 render pass 碎片化与普通 Layer overdraw。**render encoder 25–43/帧；抽样 GPU 时间 80–90% 落在普通 Layer fragment stage；活动段 `nextDrawable` 等待每秒最高 123 ms，即 GPU-bound 呈现背压。对 391 个抽样做描述性拟合 `gpuMS ≈ 0.057 × renderEncoders + 0.64 × Mpx + 2.8`（R² 0.73），pass 固定开销与像素量同量级：1874 pass 的帧约 70% 成本在 pass，185 Mpx 的帧约 90% 在像素。
- **blit encoder 的 83–89% 是 `bitmap.update`，调用链已核对为逐字形上传。**`LayerBitmap.cpp:2504` `InternalBlendText` 逐字形经单张共享 `_CharacterTexture` 调用 `Update()`；`MetalRenderBackend.mm:1723` `UpdateLayerTexture` 每次新建 blit encoder，`:811` `Blit()` 无条件 `EndOrdinary()`，下一个 `OperateRect` 经 `:857` 重新以 Load/Store 开 pass。每个 blit 平均额外打断约 1.1–1.4 个 pass（千恋/DRACU 区间拟合 R² 0.96–0.97）。这补齐了第 2.5 节对 C3 要求的调用链证据；`gpuWaitNS` 全为 0，说明是 encoder 生命周期问题而非同步问题。
- **去掉 blit 后仍有约 8–19 pass/帧来自 Layer 树目标切换与 `Create()` 的 clear pass（抽样 clears p99 78–391/帧）；像素量 38% 为 Fill**（五份合计 1.55 Gpx），Alpha 26%、Copy 20%、CopyColor 12%。
- **转场 begin/stop 抖动每秒 2–9 对，27–90% 以 `frame=0` 结束，每次仍分配整幅 cache 并整树重合成**（`tjsNativeLayer.cpp:7602` → `IncCacheEnabledCount` → `AllocateCache`）。这是脚本侧习惯，引擎只能降低每次固定代价。
- **主线程占用 30–36%**，`script` 阶段 187–257 ms/s，帧 CPU wall p50 0.3–1.2 ms 但 p99 51–80 ms，9–14% 的帧超过 16.7 ms；资源加载全部在主线程，是卡顿而非发热主因。
- 跨基线对照：Before_C0 → C2_1 的 pass/帧、blit/帧与像素量没有台阶式下降，差异与场景/时长差异同量级。

**优先级调整：**C4/C1/C2 的本地实现已完成且本轮证据显示其目标传输已收敛，后续实施顺序改为 **C3（字形/tiny update 聚合）→ P2D D0/D1（目标切换、clear pass、Fill 冗余）→ P2D D3（0 帧转场固定开销与静止帧重复 present）**。本批同样不是受控基线：四份开局已是 thermal 2，不能用于前后百分比比较；配对按 D2 口径在 nominal 下重做。

### 2.8 2026-10-10 P2D 首轮本地实施

接续 Root `17d0773`、Runtime `3a8179c`、Core `484e927`，实施 D0 元数据/有界热点/pass 来源诊断与保守 D1 延迟初始化/完整 Fill；未提交、未推送。范围、独立改动包、验证及设备重采方法见 [P2D 基线](NATIVE-METAL-P2D-BASELINE.md)。

Windows MetalLayer 9/9、Backend 3/3、四个生产 TU syntax 和相关生命周期检查通过。D0 输出使用 backend epoch 与稳定 texture/Layer 身份，保留 overflow、完整总账及分片完整性；D1 只作用于普通 Layer 创建纹理，不引入 DontCare，也未改变转场或 present 行为。Apple 原生、App device/simulator、实际资产映射及三组 nominal A/B 仍待验，不能由 portable 结果签收 P2D 性能。

## 3. 对原对话结论的逐项核实

下表保留初始固定版本审计结论；P1B 实施后的最新数量见第 4.1 节，实际输出见 [P1B 记录](NATIVE-METAL-P1B-BASELINE.md)，Gamma 兼容修复见 [P1A 记录](NATIVE-METAL-P1A-BASELINE.md)。

| 原结论 | 核实结果 | 对实施的影响 |
| --- | --- | --- |
| 当前 GL 未接普通 Layer GPU 管线 | **成立**。基类 `SupportsLayerOperations()` 为 false，GL 未覆盖这一组接口，Metal 已覆盖。C2/C6 | 不把“先接 ANGLE”作为现有算子缺口的解决办法 |
| 当前有 41 条 GPU 映射 | **成立**，是 C1 的显式注册映射条目数 | 41 不等于独立 shader 数或完整支持的方法数 |
| 旧 GL 约 74 个名称、当前直接覆盖约 34 个、缺约 40 个 | **口径不完整**。本轮所有四类注册入口合计 77 个去重名称，与显式 mapping 的交集为 37；再考虑当前共享对象别名，为 38 | 使用第 4 节和附录的逐项清单，不按旧数字排工期 |
| Perspective 当前总是软件回退 | **成立**。C2 的 `OperatePerspective()` 无 GPU 分支 | 必须新增几何执行能力；不能只补方法名 |
| Triangles 仅有限 Copy 快路 | **成立，但条件更多**：count=2、单输入、Copy、flags=0、stretch 0..2，并受 Prepare、格式、裁剪等限制 | 优先扩大现有 affine 语义域，再考虑通用三角形 |
| 旧 GL 有通用三角形和透视 | **实现存在**。K4 提交三角形，透视计算 homography | 不代表所有组合正确；旧透视多 quad 调用还有静态风险，见第 5 节 |
| 旧 GL 可整份直接移植 | **不宜如此**。有旧缓存、纹理缩放、Cocos/OpenCV、GL 扩展和资源管理依赖 | 复用语义与样例，保留当前资源架构 |
| 旧 GL 一个 scanline 可能触发整纹理回读 | **成立**。缓存缺失时按 internalW×internalH 回读，`PixelDataCounter=5`，另有过期路径。K2 | 不恢复旧回读策略 |
| 旧 GL blur 是 9 个采样点 | **成立**。中心点加 8 个偏移，除以 9；偏移可较大，并非任意大核的逐点平均。K1 | 不用它替换当前 sliding-sum blur |
| 当前 BoxBlurAlpha 已是普遍意义上“更正确”的 alpha-aware blur | **表述过强**。当前两个软件方法实际都进入逐 RGBA 字节平均，Metal 跟随这一行为。C4/C5 | 区分“匹配当前软件”与“符合原始 Kirikiri 历史语义”，另立行为裁决 |
| target-as-source 都可以 snapshot/ping-pong | **只对明确定义为旧图快照的操作成立**。C2 对若干错位自混合保留顺序相关软件语义 | 不能删掉 alias 拒绝条件来制造零回退 |
| Metal 需要以后再做 framebuffer fetch/tile 优化 | **已过时**。C3/C4 已有 programmable blending、ROG、同目标 render pass 复用和 compute 回退 | 扩展现有像素函数；验收两条实际路径 |
| GPU stage timing 仍未实现 | **对当前源码不成立**。已有 stage-boundary 采样与 `metal.gpuStages`，是否设备可用需运行验证 | 使用现有诊断，不重复实现，不把 mixed command 时间按次数分摊 |
| 旧新 RenderManager 核心接口基本一致 | **成立于接口形状**，不是实现能力等价。C7 有动态脚本编译返回空、stencil/manual target 默认空实现 | 额外能力需单独审计调用方和错误处理 |

## 4. 能力账本：哪些真的缺失

### 4.1 数量关系

| 范围 | 数量 | 解释 |
| --- | ---: | --- |
| 当前软件层注册名称 | 70 | 展开注册宏后去重 |
| 当前显式 GPU mapping 条目 | 65 | P1A/P1B 新增 24 条；包含部分共享对象的重复名称 |
| 当前有 GPU 描述符的名称 | 70 | 65 条显式映射，加 5 个共享对象别名；均仍有执行限制 |
| 当前软件存在、但无 GPU 描述符 | **0** | P1B 11 项矩形描述符已补齐；不代表全几何 GPU 支持 |
| 旧 GL 注册名称 | 77 | 四类注册入口合并、去重；包括条件分支的名称集合 |
| 旧 GL 名称在当前有 GPU 描述符 | 62 | P1A/P1B 新增 24 项；P2B 已接入 `PerspectiveAlphaBlend_a` 的受限透视执行 |
| 旧 GL 名称在当前只有软件实现 | 0 | 15 个未注册历史扩展仍未恢复 |
| 旧 GL 名称当前软件层也未注册 | **15** | 染色/AlphaTest 兼容能力；不能算普通 shader mapping 缺失 |

核对恒等式：当前 `70 + 0 = 70`；旧 GL `62 + 0 + 15 = 77`。两边名称并集 85，见附录 A；P0 `46/24` 与 P1A `59/11` 记录保存在各自独立基线中。

额外 5 个共享对象别名是 `AdditiveAlphaBlend_HDA`、`PsMulBlend_HDA`、`PsOverlayBlend_HDA`、`PsHardLightBlend_HDA`、`PerspectiveAlphaBlend_a`。`AlphaBlend_HDA` 已在显式 mapping 中，不再重复加。

**特别注意 `ConstAlphaBlend_SD_a`：**旧 GL 把它注册成 `ConstAlphaBlend_SD` 的同一个对象；当前软件却是两个独立对象、分别调用不同函数。初始审计确实缺 mapping；P1A 已添加独立 flags 与双源公式，不能按旧 GL 的对象别名关系实现。见 K1 2788–2793 与 C1 2965–2976。

### 4.2 初始 24 个算子缺口与实施状态

| 组 | 方法 | 数量 | 工作性质 |
| --- | --- | ---: | --- |
| 普通混合 | `SubBlend`、`MulBlend`、`MulBlend_HDA`、`ColorDodgeBlend`、`DarkenBlend`、`LightenBlend`、`ScreenBlend` | 7 | P1A 本地完成；矩形、参数与 alias 域已接入 |
| Photoshop 混合 | `PsAlphaBlend`、`PsAddBlend`、`PsSubBlend`、`PsSoftLightBlend`、`PsColorDodgeBlend`、`PsColorBurnBlend`、`PsLightenBlend`、`PsDarkenBlend`、`PsDiffBlend`、`PsDiff5Blend`、`PsExclusionBlend` | 11 | P1B 本地完成；真实软件三表、整数公式与独立变体 |
| 图像与透明度 | `RemoveOpacity`、`AdditiveAlphaToAlpha`、`AdjustGamma`、`AdjustGamma_a` | 4 | P1A 本地完成；R8 限同尺寸正向合法 ROI；Gamma owned LUT 与软件索引修复 |
| SD 语义 | `AlphaBlend_SD`、`ConstAlphaBlend_SD_a` | 2 | P1A 本地完成；分别为单源 RGB/alpha=0、双源完整 ARGB 插值 |

上述 24 项是初始审计时的软件方法缺口，P1A/P1B 均已完成其矩形描述符与本地实现。许多方法出现在 `tRenderMethodCache` 的 bm* 对应关系中；实际 GPU 路由仍有格式、几何、alias、lease、参数资源和 backend 限制。

### 4.3 已有能力仍受哪些条件限制

| 能力 | 已有实现 | 仍需处理/验证 |
| --- | --- | --- |
| 普通矩形 | copy/fill、alpha、普通混合、部分 Ps、灰度/反预乘、Gamma、alpha/channel 等 | P1A 新源 wrapper 限正向矩形；格式、缩放、裁剪、错位别名、CPU pin/lease 域与原生结果待验 |
| 双源 SD | `ConstAlphaBlend_SD`、`_d`、`_a`，同尺寸矩形 | 错位源目标别名保留回退；单输入 `AlphaBlend_SD` 的 alpha=0 契约另行处理 |
| Universal transition | 普通、`_d`、`_a` 的三源 GPU 路径 | 两个 RGBA 源加 R8 rule、同尺寸与合法源范围；不是所有 CPU transition 都由此覆盖 |
| affine | 两三角形、Copy 及 Alpha/ConstAlpha/AdditiveAlpha/Ps 单源兼容路径 | P2A 本地完成；镜像扫描线、非整数源、其他过滤器保留回退；原生顺序/像素及 tile affine 优化待验 |
| perspective | P2B：Copy 与 Alpha/ConstAlpha/AdditiveAlpha/Ps 单 RGBA 源逆映射 compute | 本地完成；其它方法、过滤器、超限批次、矩形错位自别名等仍软件；新 Apple/真机待验 |
| blur | 两 pass sliding sums；两个名称共用当前软件公式 | 64 MiB sums 限额、无缩放、完整合法 ROI；大图/裁剪等仍可能回退 |
| R8 | mask/rule 资源及 ColorMap/RemoveOpacity 输入 | RemoveOpacity 限同尺寸正向合法 ROI；不等于任意 Gray/province 目标均支持 GPU 运算 |
| 显式 CPU 像素边界 | `DivisibleTransHandler` scanline、`shrinkCopy` lock、LayerEx raw pointer、bitmap dirty update 均可请求 CPU 可见像素 | 不是普通 Layer operation descriptor 的缺口；P2C 必须逐一裁决 GPU 路径、完整覆盖写、必要 CPU 边界与批处理，不能声称任意插件/截图/命中测试零回读 |
| CPU/GPU 一致性 | COW、dirty region、point cache、CPU lease、会话隔离 | 新算子必须正确声明 alpha 变化、源依赖及写 ROI，不能绕过这些机制 |
| tile/compute | 支持设备上 tile；in-place compute 或 snapshot compute | 新算子在所有路径的像素和顺序一致性，不能只测默认 tile |

### 4.4 15 个历史扩展方法

- `AlphaBlend_color`、`AlphaBlend_color_a`、`AlphaBlend_color_d`。
- 上述三个对应的 `_AlphaTest` 变体，以及独立 `AlphaTest`。
- `PsAddBlend_color`、`PsSubBlend_color`、`PsMulBlend_color`、`PsScreenBlend_color`，及各自 `_AlphaTest` 变体。

本轮对当前 core/plugins 的 C++/头文件检索未找到这些方法的直接调用，也未找到旧 stencil/manual-target/动态编译接口的外部调用；这不排除运行时字符串或第三方插件需求。先记录能力缺口，后续结合插件清单、脚本入口及失败样例确认。

当前 Emote 的 mesh/mask/color modulation 使用另一套 backend API。旧 `AlphaBlend_color*` 或 stencil 的缺失，不能直接推导为当前 Emote 不支持染色或蒙版。

## 5. 必须先固定的语义规则

### 5.1 判定正确性的顺序

1. 对已有行为，使用实际初始化后的软件 RenderMethod/函数指针作基线，覆盖当前的参数与资源契约。
2. 对混合公式，同时检查 `gl/tvpgl.cpp` 与 `gl/blend_function.cpp` 的绑定。不能仅拷贝一个名字相近的 `_c` 实现，就假设运行时调用的是它。
3. 使用 Kirikiroid GL 查漏、识别调用习惯、构造边界样例；如有 GL 运行环境，再作为第三方图像对照。
4. 软件、旧 GL、已有游戏表现冲突时，保留最小输入、三个结果和参数，单独记录兼容决策。不要同时“修软件定义”和“新增 Metal 加速”却不标明行为变化。

旧 GL 的测试默认通过注释禁用了 `TEST_SHADER_ENABLED`；检查函数允许 alpha 误差和按 alpha 加权的 RGB 误差，还存在忽略 alpha 的测试分支。因此不能把“旧代码里有 TEST_SHADER”当作所有像素逐字节正确的证据。K5。

### 5.2 混合、颜色与取整

- 普通 Layer 的颜色语义与 Emote mesh 混合分开；不复用一个相似编号的 mesh blend mode 代替。
- 明确 ARGB 数值与内存字节、RGBA8Unorm 纹理、R8 mask、straight/premultiplied alpha，以及 HDA、`_d`、`_a` 的不同公式。
- 逐个保留 `>>8` 与 `/255`、opacity=255 特支、查表、饱和和舍入顺序。
- 新 ordinary Layer 方法优先扩展同一份 `layerPixel()` 整数语义。只有证明固定功能混合在规定输入域精确等价，才允许用它替换。
- 不以全局“允许 ±1”掩盖不同算子错误；现有 compositor 的 ±1 测试标准，不自动应用于 ordinary Layer 的精确像素测试。

### 5.3 几何与回退

- 现有软件 `OperateTriangles()` 断言单输入、两个三角形及特定 quad 结构。**它不是任意三角形的通用参考/回退实现。**C5。
- 已支持 affine 域继续对软件做逐像素比较；新增任意三角形域需要独立的 rasterization 契约和参照。
- 透视不得用两个 affine 三角形近似。必须保留 homography、正确插值、像素中心、源端 `+1` 边界与 clip 相对坐标规则。
- K4 的旧透视代码在每个 quad 循环中构造 6 个顶点，却以 `nQuads * 2` 调 `OperateTriangles()`；当 nQuads>1 时有静态越界风险。这里只确认源码形状，未运行复现；移植时必须使用多 quad 测试，不能复制这一控制流。
- 无效、退化、NaN/Inf、越界和未知参数必须在写目标前拒绝；只向已知支持该输入域的软件路径回退。

### 5.4 别名、COW、参数寿命

- 区分 source==target、reference==target、共享底层资源、完全重合和偏移重叠。
- GPU snapshot 表示读取操作开始时的旧图；软件逐扫描线写入可能表示依赖前面已经写过的像素。两者不能默认互换。
- COW 后 `reference` 可能是原图，target 是新图；转换、Gamma 和混合需要逐方法确定其读源，不能统一当“原地处理 target”。
- 不缓存用户传入的短命 `SetParameterPtr()` 指针到 GPU。Gamma/LUT、矩阵等参数在编码时固定版本，并保证提交完成前资源有效。
- 保持 CPU 原始指针、读写 lease、异常退出和 session detach 的既有行为。同步脚本像素 API 不能擅自返回上一帧。

## 6. 实施阶段与验收门槛

建议顺序：**P0 → P1A → P2A → P1B → P2B → P2C（C0 已签收、C4 已获得实际场景证据 → C1 → C2 → C3）→ P2D → P3 → P5**。P4 是按调用需求启用的兼容分支。第 2.6 节已确认千恋和 DRACU 的实际 transition 走 GPU，C4 从“优先实施”进入“补齐全域原生/配对验收”；C1 本地实现与 portable 验证已完成，Apple/真机配对待验，后续实现项为 C2。P2D 与 P2C 的 C1–C3 没有语义依赖：D0 诊断可在 C0 之后立即开展，D1/D2 的优化和专项签收在 P3 前独立完成。Emote GPU 工作量和 command queue 的独立调查仍归 P5，不以 P2C/P2D 完成为解决承诺。

### P0：把能力契约和测试基线固定下来

目标：后续每新增一个算子，都能知道它支持哪些输入以及怎样证明正确。

2026-10-05 签收：按本会话确认的范围，本地代码契约、审计与测试完成即可签收 P0；Apple/真机新结果单独列为待验项。执行平台、三层 HEAD、实际输出和历史证据边界见 [P0 基线记录](NATIVE-METAL-P0-BASELINE.md)。本轮不增加 24 个待补算子的 GPU 描述符，不扩大合法调用的支持域。

工作：

- [x] 将附录清单转成可维护的能力描述/审计输出，记录名称、canonical 对象、operation、输入数、格式、参数、几何、alias 和 alpha 写入规则。
- [x] 用运行时注册检查验证 70 个名称及 46 个已映射名称；为 `_HDA` 和 `PerspectiveAlphaBlend_a` 验证对象共享，为 `ConstAlphaBlend_SD_a` 验证对象独立。
- [x] 固定 `TVPLayerOperationKind` 编号 0–26；共享 `.def` 驱动 Count/traits/MSL 定义及诊断存储，消除数字 switch、输入区间和 `[27]` 联动风险。
- [x] traits 覆盖输入数量/格式、读取目标、写 alpha、alias、参数资源及几何；保留 reference/COW、HDA、blur、SD 与 affine 的现有例外。
- [x] 复用确定性像素、参数边界、ROI/别名/租约与 GPU 计数测试，增加非法 kind、安全失败及测试用新 kind 的 Count 扩展验证。
- [x] 保存当前本地测试与历史真机证据，记录平台、HEAD、skip 与 device double；新原生 Metal/真机基线列为待验项。
- [x] 动态编译空结果返回明确失败且不注册，注册入口拒绝空对象；验证重复失败及 Release 路径，不提供动态 GLSL 功能。
- [x] 更新 backend 与 Layer README，明确已加速矩形及 affine Copy 子域和软件回退限制。

修改位置：C1/C2/C6/C7、`MetalRenderBackend.h/.mm`、C4、T1/T2、相关 README。

验收：已有渲染结果和路由不变；能力清单能区分“未注册 / 无描述符 / 参数不支持 / backend 不可用”；添加一个测试用新 kind 时不出现统计越界或 shader 编号漂移。

### P1A：补高确定性的矩形语义

2026-10-05 本地签收：13 项已实现，MetalLayer 2/2 与 MetalRenderBackend 1/1 通过；新增 21,757,036 个精确 scalar 像素。按用户选择同步修复 Gamma_a 的 LUT[256] 越界，除此之外跟随实际软件绑定。原生 MSL、iOS 构建与本轮真机回归仍待验，详见 [P1A 基线](NATIVE-METAL-P1A-BASELINE.md)。

#### A. 普通混合 7 项

- [x] 实现第 4.2 节普通混合组，分别核对 HDA 和 opacity=255 的具体分支。
- [x] 保持 `MulBlend` 与 `_HDA` 对象和 flags 独立，各自与真实软件绑定比较；当前两个绑定均保留目标 alpha，不人为制造差异。普通 Screen/ColorDodge 不复用 Ps 公式。
- [x] 将格式、alias、alpha-cache invalidation 与像素实现一起接入 C2/C3。

#### B. 透明度、转换与 Gamma 4 项

- [x] `RemoveOpacity`：按当前软件读取 R8 mask，只改变 alpha；穷举 mask/opacity/alpha 组合。同尺寸正向合法 ROI 支持；缩放/镜像在写入前明确拒绝，不进入 RGBA 寻址的软件 resize。
- [x] `AdditiveAlphaToAlpha`：精确整数实现与 TVPDivTable 等价，覆盖 alpha=0、1、254、255 和 RGB>alpha；沿用 ApplySelf/reference/COW 契约。
- [x] `AdjustGamma` / `_a`：复用软件生成的 owned LUT，以不可变版本快照编码，不在 MSL 重算 pow。
- [x] Gamma `_a` 独立核对 alpha/超 alpha RGB/上下限/COW/reference/连续改参数；软件与 GPU 同步钳制 LUT 索引至 255，并记录兼容性修复。
- [x] Gamma 不新增纹理、每算子提交或同步；两个最近版本缓存和命令资源持有保证寿命，参数上传单独计数。

#### C. SD 2 项

- [x] `ConstAlphaBlend_SD_a`：扩展现有双源公式，按 input0/input1 顺序完整 ARGB 插值，保持独立方法对象。
- [x] `AlphaBlend_SD`：单显式 source 与目标旧值执行 RGB 插值、输出 alpha=0，不要求两个显式输入。
- [x] 双源及新单源混合的错位目标别名继续按顺序相关软件语义回退，保留反例测试。

修改位置：C1 的参数类与映射、C2 的输入/格式路由、C6 参数结构、C4 像素函数与双源函数、C3 LUT 资源绑定及执行。

验收：13 个名称在声明支持域内 GPU 执行；预先驻留的 RGBA/R8 输入不产生 fallback/readback/整图上传；Gamma 允许必要的小型参数/LUT 上传，必须与图像上传分开统计。离开支持域仍有正确结果及准确拒绝原因。

### P1B：补 Photoshop 混合 11 项

2026-10-06 本地签收：fetch 后三层 metal_dev 与 origin/metal_dev 均 0/0、文件 diff 为空、子模块指针一致；随后实施 P1B。MetalLayer 2/2 与 MetalRenderBackend 1/1 通过，新增 46,137,346 个精确 scalar 像素；原生 MSL/Apple/真机结果待验，见 [P1B 基线](NATIVE-METAL-P1B-BASELINE.md)。

- [x] 实现第 4.2 节 Ps 组全部 11 项，共用 layerPsP1BPixel 整数 helper。
- [x] SoftLight/Dodge/Burn 导出实际软件初始化表，196608-byte 三表包只读上传/缓存，命令保留旧资源，不重算 pow。
- [x] 验证 ColorDodge/ColorDodge5、Diff/Diff5 与普通/Ps 的公式/alpha/opacity 次序，增加独立反例及端点断言。
- [x] tile、in-place 与 snapshot compute 均绑定 buffer(3)；RGBA 正向单源矩形、same-pixel alias、错位软件回退与 pin/lease 域有测试。

验收：与 P1A 合计 24 个缺口均完成矩形支持与反例回退；当前 70 个注册名称都有明确的 GPU 能力描述或经记录的限制。这里不宣布其所有几何和所有输入均 GPU 化。

### P2A：把现有 affine Copy 扩展到 Layer 混合

目标：优先解决现有游戏可到达的 affine quad 回退，不把通用网格设计作为阻塞项。

2026-10-06 本地签收：19 个混合 kind、30 个方法/别名接入准备后的 affine 域，MetalLayer 2/2 和 MetalRenderBackend 1/1 通过。Copy 快路、矩形与 affine 各自采样域保持；原生验证和设备上的 tile 性能评估仍待验，详见 [P2A 基线](NATIVE-METAL-P2A-BASELINE.md)。

- [x] 梳理既有约束，将 manager 入口扩展为 `GPUAffine()`，保留 `TVPLayerAffineCopy`、Prepare 与原 Copy kernel。
- [x] 分离采样与混合，新 compute kernel 复用 `layerPixel()`；接入注册的 Alpha/ConstAlpha/AdditiveAlpha 变体与全部 Ps 家族。
- [x] 软件 warp 读取 target 旧值并忽略 reference；完整 clip 的透明 warp 边界参与混合，复用 alpha/PS tables 与源/目标 GPU 快照。
- [x] 精确 portable 回归覆盖旋转、错切、非整数目标位置、非矩形反射、源 ROI、目标 clip、单像素/单行/列、alias/COW/cache/lease；特殊扫描线和其他几何仍回退。
- [x] affine 限 StretchType 0..2；矩形既有过滤器域不变。新增 `AffineAlias` 诊断区分矩形重叠。
- [x] 明确首版非矩形 affine 使用 compute 与快照，会关闭 tile pass；矩形特判保留 pass 复用。未实现 affine tile fragment，设备成本评估留作待验。

验收：声明支持的 affine quad 与软件逐像素一致；仍不支持的几何有最小反例；无额外 CPU 中转；triangle diagnostics 区分 GPU 成功、方法缺失、采样/几何/alias 拒绝。

### P2B：新增真正的 Perspective GPU 路径

2026-10-06 本地签收：Copy 与 P2A 的 19 个混合 kind 接入单 RGBA 源 perspective，32 个名称/别名参与测试；MetalLayer 2/2、MetalRenderBackend 1/1 通过，共 78,622 次精确表面比较，见 [P2B 基线](NATIVE-METAL-P2B-BASELINE.md)。支持域外的有效输入仍软件；新原生/真机验证待验。

- [x] 新增 quad 描述与默认 false 的通用 batch 入口；最多 256 quad 的 Metal 事务，失败在真实目标提交前返回。
- [x] 共用原 Gaussian 求解与 3×3 inverse，固定 LT/RT/LB/RB、右/下 +1、whole-source、clip-relative centers 与透明边界；不引入 OpenCV。
- [x] 使用真正逆 homography compute 与共用像素函数；按 quad 次序更新 scratch，最后提交真实目标，不用 affine 两三角形替代透视。
- [x] 沿用 `PerspectiveAlphaBlend_a` 共享对象与描述符，不新增注册或 kind 编号。
- [x] 精确回归覆盖 identity/变换/强透视/反射、源目标裁剪、分母、混合多 quad/alias、失败原子性、COW/cache/lease 与无中途读回的排队批次。
- [x] 修复软件矩形 point2/point3 错误、不相交误 warp、晚 quad 非法输入与未初始化矩阵、负过滤器和非有限采样；独立参照确认新行为。

验收：当前合法单输入透视场景留在 GPU；多 quad 不越界、不相互污染参数；覆盖边缘和采样的差异有逐项结论；异常输入不会在部分修改目标后再次软件渲染。

### P2C：消除已证实的 CPU/GPU 边界与 tiny-upload churn

目标：把先前 P5 中泛化的“检查读源”变成可交付的运行时路径。这个阶段不以“所有 CPU pixel API 必须消失”为目标；CPU 算法、截图或插件 ABI 仍可能合理地需要边界。目标是让声明支持的生产路径保持 GPU resident，或将不可避免的 CPU 边界收敛为一次、可归因、语义正确的操作，而不是默许整图往返、同步 lock 或每个小 dirty rect 一个 encoder。

#### C0. 先补齐传输归因与可比较基线

2026-10-07 本地签收：64 槽 origin（40 个保护槽/24 动态槽）与显式 overflow、2048 对真实 runtime 帧间隔/CPU wall 样本、v2 C/Swift/分析接口和设置页构建 HEAD 已接入。Layer CTest 4/4、backend 1/1、frame 检查通过；78,628 精确表面比较、3 session，见 [C0 基线](NATIVE-METAL-P2C-C0-BASELINE.md)的历史实现记录。随后第 2.5 节 8 份真机日志通过完整 origin 对账，**C0 归因签收，可进入 C4 实施**。同热状态 before/after、原生诊断开关开销、App XCTest/Apple 专项检查仍待独立证据，不宣称性能改善。

- [x] 在 `metal.layerWork` 中增加与 texture-ID top-N 并列、但不按 texture ID 拆分的**有界 origin 聚合**：每个 origin 的 calls、bytes、wallNS、sync waitNS；保留 top-N texture 明细用于定位，不能再由 `other` 隐藏 stall 来源。
- [x] 为 `transition.source`、`transition.outputOverwrite`、`shrinkCopy.read/write`、`layerExBase`、`layerExDraw.write`、`bitmap.update` 和未分类 lock/pixels 保持稳定且可测试的命名；新来源超长/超容量显式进入 overflow，原字符串不改写。
- [x] 计数器只观察既有工作；生产 texture/cache/lease 路径与 device double 验证实际次数、bytes、wait、generation、开关工作量一致，未新增 GPU 同步。原生分支的 submit/wait 断言需 Apple 执行。
- [x] 逐帧采样、整窗口分析、nearest-rank 分位数、旧格式下界/缺失/丢失标识，以及设置页与诊断共用 Bundle HEAD 均有本地连线/数学测试；App XCTest 与 UI 编译待 Apple。
- [x] C0 后同版本 8 游戏真机归因样本已采集，origin bytes/calls/wait 与总体计数 8/8 精确对账且 overflow 为零，见第 2.5 节。
- [ ] 补齐正式性能配对条件：设备、OS、三层完整 commit、包哈希、分辨率、热状态、运行时间、存档/输入步骤与逐帧覆盖；逐帧数据合并后计算 p50/p95/p99，不平均窗口分位数。没有同条件 before/after，不宣称改善；该待验项不阻止开展 C4 的源码审计与实现。

#### C4. CPU transition：第一实施项，对高频 `DivisibleTransHandler` 建立 GPU 保留路径

主回归为千恋万花（1920×1080 output，1920×1440 与 1920×1080 source）；DRACU（1280×720 output、1280×960 source）为第二样例。第 2.5 节已确认两者触发，先保存具体存档/操作步骤、handler 类型与完整版本，再锁定首批支持域。

2026-10-07 本地签收：用户选择首批直接覆盖全部七种内置 extrans（mosaic、wave、ripple、turn、rotatezoom、rotatevanish、rotateswap）。独立 compute 契约、原软件参数/定点表、COW/lease/安全拒绝、64 条有界 handler 诊断及只读 C/Swift bridge 已接入。1962 个真实 CPU/MSL scalar 精确案例、七种 pipeline CPU 回退与 dispatch 后异常不重跑、MetalLayer 4/4、backend 1/1、frame 检查和两份完整生产 TU syntax 通过，见 [C4 记录](NATIVE-METAL-P2C-C4-BASELINE.md)。第 2.6 节随后以三款 iPhone 实际场景确认 crossfade、wave、mosaic 和 universal 触发时的 GPU/零 CPU 行为及零像素往返；其余 handler 的原生覆盖、完整 Apple 矩阵与同热状态性能配对仍待验，不由 portable 或单次日志替代。

- [x] 首批七种内置 extrans 映射与 provider/尺寸/参数/持续帧/回退诊断已完成；未知 provider 保留 CPU。实际游戏 handler、规则/源数及存档/步骤等待新真机日志确认，不把已有 Universal transition 当作任意 CPU transition 的覆盖证明。
- [x] 本地支持域内 source/output GPU 驻留、alpha、clip、原时间步进、别名拒绝、COW/lease 和 session 语义已通过 portable；原生 GPU 与中断/场景切换真机验证待验。
- [x] 七种 GPU kernel 已替代支持域的 scanline 像素计算，不依赖 `LockCPUWriteForOverwrite()` 假装 GPU 化；必要参数表与行数据上传单列。
- [x] `sourceRevision=56b2799f8068` 的千恋、DRACU 和天使纷扰日志确认实际 transition 为 GPU 调用、`cpuCalls=0`、无 `transition.source`/`transition.outputOverwrite` 像素传输；这只覆盖已观察到的 handler/尺寸/设备场景，见第 2.6 节。
- [ ] 补齐七种 handler 的原生像素、实际传输/submit/wait 门槛、Apple CI/App/Swift 矩阵，以及以 nominal/same-thermal 条件重复三次的性能配对。已观察到的 iPhone 日志不能替代未触发 handler 或受控配对。

验收：对上述已支持的 transition 重放，`transition.source` readback calls/bytes、`transition.outputOverwrite` upload calls/bytes、该 transition 的 CPU fallback 和归因 sync wait 均为零；无新增每帧 submit 或等待，并与软件参照逐像素一致或有预先定义的容差。按 handler 区分支持/回退域，报告同热状态 frame-time p50/p95/p99、长帧和原始样本覆盖；必要参数/rule 首次上传单列，不与每帧 output 上传混计。未支持类型必须正确回退且可从 origin 聚合中辨认，不能以总 readback 为零掩盖它。

#### C1. `shrinkCopy`：第二实施项，消除同步 lock 路径

2026-10-08 本地签收：两个入口已接入专用两 pass 整数计算与事务输出；Area 保留原 double 权重、32/64 位模算术、分数边缘和 alpha 规则，Fast 保留逐轴 floor/alpha=255。源在 COW/resize 前保留，O(W+H) 证明安全自别名，其余顺序依赖仍 CPU；callback/ObjThis、lease、预算与故障拒绝有回归。60 个真实插件精确对照、2048 组别名证明、12 个故障事务、两套 CTest 4/4和1/1、TJS 1/1、25 个解析 fixtures、frame及完整生产TU syntax通过。新增 bounded per-read wait 与只读 C/Swift桥，并修复 logger 的 1024-byte 结构化字段剪裁；新增 Swift export 测试未在本机执行。详见 [C1 基线](NATIVE-METAL-P2C-C1-BASELINE.md)，不由 portable 结果宣称真机收益。

- [x] 两种入口、原权重/裁剪/源捕获和目标写域已审计，按方法/原因/别名及原 origin 同时归因；历史日志不能分辨的方法由新版本重采确认。
- [x] 同会话 RGBA8、无租约的支持域及可证明等价自别名已接 GPU；完整覆盖和部分 ROI 分开提交，不改滤镜、边缘/alpha或区域外像素。
- [x] 顺序相关 alias、custom binding、资源/管线/整数能力/预算失败正确 CPU 续跑；Fast resize 仅一次，提交后异常不得 CPU replay。CPU table 分配和采样/算术安全边界已修复。
- [ ] Apple Objective-C++/MSL、Swift/App/真实logger、原生GPU资源/submit/wait及三款游戏同条件配对有新证据。

验收：对可重放的、源和目标均已驻留的 `shrinkCopy` 支持域，`shrinkCopy` 归因的 readback calls/bytes/sync wait 均为零，且无整图 CPU upload；完整覆盖写不会先读旧目标。DRACU-RIOT!、天使纷扰和 9-nine 的同条件日志分别报告次数、bytes、p50/p95 wait 与长帧数；仍需 CPU 的调用有明确原因，不能混入 `other`。

#### C2. LayerEx/raw-pixel：收敛 GPU↔CPU ping-pong，而非假定插件可自动 GPU 化

2026-10-09 C2B 本地签收：After_P2C_C2A 三份 `sourceRevision=3cc3971f518c` 日志确认流程图的 drawLine/drawPath/drawImageStretch 为剩余实际绘制消费者；千恋保存小图的消费者已精确关联为 `Layer.saveLayerImage` / `saveBookMarkToFile`。本批选择流程图三入口，保留 pinned plutovg 1.3.3 原栅格化/paint 采样，以有序整数 span 做 GPU 合成并只提交 ROI。record、借用/逃逸 surface、租约、转换、副作用与超限保留具名 CPU 路由，返回值装箱仅在提交后执行一次。MetalLayer 5/5（含真实上游/overlay 完整像素对照）、Backend/TJS 各 1/1、34 项解析、frame/point trace、完整 Draw/native Layer syntax 通过；当时未提交；首批随后已发布。详见 [C2B 独立基线](NATIVE-METAL-P2C-C2B-BASELINE.md)。Apple/原生 kernel、新真机与配对仍待验，不宣称全部 C2 完成或性能改善；9-nine Copy/blur 和同步保存未纳入本批优化。

2026-10-09 C2B 修复签收：首批代码随后已提交至三层 `0b557122fca5 / 860d3de7e804 / 6e75bbb32739`。After_P2C_C2B 真机日志暴露真实 Appearance/Path 的 direct-native 注册与旧 Gdip wrapper 预检不符；旧夹具也人为包装，未覆盖该差异。本轮改用直接 adaptor，测试使用生产构造与默认 boxing/unboxing，旧预检真实注册复现失败后修复通过。新增固定方法×路由×原因表完整聚合，详情仅留 9 个方法/路由保护样例＋23 个原因样例；native `metal.layerSpan` v2 在原窗口结束时输出聚合/样例/总账，以 generation＋windowID 对账，App 经只读 C accessor 附带 ID，旧 C struct/profile 字段/周期不变。MetalLayer 6/6、Backend/TJS 各1/1、51项解析、生产 emitter→parser 5,071 次路由对账、C importer/桥接/接线、frame/point/syntax通过；八份旧日志哈希不变，历史省略39条仍未知。该修复随后已发布；5987acb 与 e3b0a5e 样例确认实际 GPU，完整 Apple 联合构建/同步仍待验，见 [命中与统计修复记录](NATIVE-METAL-C2B-HIT-FIX-BASELINE.md)。Nekopara 明确延后，不由此宣称性能改善。

2026-10-09 后续日志与补修：上述类型/统计修复已提交至三层 `5987acb274a7 / 1f8c04839d2b / e3302bed3d4f`。After_P2C_C2B-2八份日志的626个完整work窗口全部v2对账，聚合/代表/总账无缺行或容量丢失；千恋drawLine1/drawPath10均GPU，两份天使各GPU4/CPU67（arguments33、record33、vectorSource1）。千恋剩余流程图回读为范围外drawRectangle，天使正常样例仍3次/8,029,788 bytes/136.633458ms wait，不宣称完整C2B往返消除或性能改善。本轮真实绑定复现并修正严格数量与NCBind最小数量的差异，三个入口允许原生签名之外未消费的额外参数，少参数错误及安全域保持。用户提供的天使hsvcpick字节码确认设置direct缺失是选项模块旧字段读取；固定完整SHA/类/方法/指令签名的临时副本仅更改local字符串映射，绑定既有hsvDirectSysButton，无全局/Layer假属性或VM语义修改。MetalLayer6/6、Backend/TJS各1/1、四套其它TJS依赖目标及51解析通过，真实字节码仅1byte静态变化，无游戏初始化执行；修复已发布至 e3b0a5e04488；After-3 确认原 arguments33 转 GPU 及 HSV applied，完整设备设置交互闭环仍待验。见 [参数与设置兼容修复记录](NATIVE-METAL-C2B-ARITY-BASELINE.md)。

2026-10-08 C2A 本地签收：已完成有界消费端/shrink 输出 identity 归因、LayerExDraw 元数据构造和按需像素租约，以及 NCBind 参数/错误顺序、raw-first、外部重入/COW/resize 的兼容修复。MetalLayer 4/4、Backend/TJS 各1/1、32项解析、frame/point-trace及六个生产 TU syntax通过；诊断开关真实 facade 事务的精确像素与计数一致。Draw 边界使用真实 NCBind/TJS 和 canvas double，真实 plutovg/Apple/App及新真机链路待验，不宣称性能改善或全部 C2 完成。详见 [C2A 基线](NATIVE-METAL-P2C-C2A-BASELINE.md)。既有 CPU 保存同步语义、plutovg算法和实际绘制写域保持；新 GPU 算子/异步保存留待消费端证据选定。

**C1 后的新增归因样例：缩放结果的 CPU 消费者。**2026-10-08、`sourceRevision=349f8945d605` 的 [After_C1 千恋原始日志](../perf_log/After_C1/Mikage-diagnostics-229C6FE2-D579-4FDD-A647-D5642EDFBB58.jsonl)中，两次 `shrinkCopy` 均为 GPU、无 shrink 归因像素读回/等待/上传，但同窗口仍有两次 496×279 `bitmap.scanline` 读回，合计 1,107,072 bytes / 18.387917 ms wait。对应 texture ID 分别为 441、8670，发生在开局与退出附近；不能视为同一有效内容被重复读回。尺寸和窗口关联提示缩放后的 CPU 消费，尚不能证明调用者是保存缩略图、图片编码或某个插件。

- [x] C2A 实现并经三款日志确认：真实成功 scanline 读回的有界消费端归因、纹理/会话/内容版本/最后写入者、原生入口和 TJS 栈，以及 shrink 输出版本关联。保存小图链路已确认；位置保持 unverified/unavailable，采样超额明确报告。复用既有测量，原 C0 总量不重复计数。
- [ ] 对后续绘制或有已知 GPU 等价实现的消费者，直接消费 GPU 纹理，消除获取 CPU 像素的边界；保持原像素、alpha、COW/lease 与操作顺序。
- [ ] 对必须 CPU 编码/保存的消费者，保留 GPU 缩放，只对实际需要的小图做一次读回，并复用同一内容版本的有效 CPU 缓存。现有缓存已避免有效内容重复读取；不同纹理或 GPU 修改后的新版本不得合并为缓存命中，也不得以失效像素填补同步读取。
- [ ] 仅在消费协议允许延后完成、且存在合适的既有提交时机时，评估提前准备不可变版本的读回及后台编码；保持脚本返回时的像素/文件完成与错误语义、场景退出和跨会话寿命。同步指针或立即要求保存完成的调用保留必要同步边界；异步准备不得新增强制提交或将等待转移到另一未计量路径。
- [ ] 将 `LayerExBase`、`LayerExDraw`、`ScopedLayerPixels` 等调用按只读、完整覆盖写、局部读改写和真 CPU 算法分类，并捕获调用栈/功能样例。每类明确 CPU cache、dirty ROI、lease、COW、alpha cache 和跨会话资源语义。
- [x] C2A 本地：LayerExDraw 构造、纯属性/测量及无 record 的状态操作不获取像素；实际绘制、record及保存保持保守租约，内部重绘共享一次，外部更换目标的重入挂起/恢复正确，原拒绝顺序保持。消费端事件、预算/代际、版本精确关联和分析器已提交，三款 C2A 真机样例确认消费链；GPU 化与完整原生签收由 C2B 单列。
- [x] C2B 本地：三个流程图入口的原始 span/采样与整数 GPU 合成；独立 Unsupported/Count、稳定行内顺序、64 MiB 与每行 512 references 预算、ROI 外像素、COW/lease/Clip、提交前唯一 CPU 续跑、提交后异常不重放、返回值及装箱副作用、逃逸图像别名均通过。本地证据见独立基线；普通 Layer 名称/编号不变。
- [x] C2B 命中与统计修复本地：真实 direct-native Appearance/Path、GdipImage wrapper/default conversion 注册回归；完整五指标聚合及32代表、保护位置、溢出/缺行/空窗/开关/换代/迟到identity检查通过。总量和原因分布使用v2聚合与窗口总账，代表不重复计数；C0像素传输和执行次数保持，详见修复记录。
- [ ] C2B Apple/真机：完整 App/插件及 Objective-C++/MSL、default/forced compute、device/simulator、诊断开关 submit/wait 与新流程图日志。支持域 target readback/整图 output upload 为零；source/参数传输另报，所有 CPU 路由须具名，不把零 C0 像素上传当作零参数传输。
- [x] C2B e3b0a5e 新日志完整性：After-3 三份297完整窗口继续v2对账；千恋绘制GPU及天使arguments33转GPU，BD7898C1为320路由GPU132/CPU188，详情容量省略0。此样例不替代本轮v3重采。
- [x] C2B 5987acb样例完整性：8份626窗口对账通过、零容量丢失；千恋drawLine/drawPath与天使drawLine出现真实GPU。代表省略仅重复详情，不丢聚合；其余域与重采门槛保持。
- [x] C2B 参数/HSV兼容本地：额外参数真实绑定与少参数错误回归通过；严格已知版本字节码映射修复只改临时副本1byte，原创VM回归和用户字节码静态差异检查通过，无游戏原文进入tracked测试。
- [ ] C2B 设备设置闭环/本轮重采：After-3已确认arguments支持域GPU与compat.bytecode applied；设置进入/颜色选择/修改/退出/再次进入的完整闭环仍待验。本轮record/vectorSource/rectangle/clear支持域扩大后须新采v3日志；Nekopara延后。
- [ ] 对有已知 GPU 等价实现的功能接入专用 GPU 路径；对 ABI 必须提供 raw pointer 的功能，设计单次、区域化的 acquire/release 与上传边界。不能通过延迟 dirty、复用失效 CPU buffer 或虚构 full overwrite 改变插件可见像素。
- [x] 2026-10-10 LayerExImage 本地首批：After_P2C_C3 的 F63 DRACU 样本暴露另一插件的颜色处理重放，125 次大图及190次小图 read，合计2749.160326 ms wait；旧样本没有具体方法/参数，不能视为 C3 回归或把它全部分配给 light。新增五个方法的有界 v1 参数/阶段/路由与完整总账，构造仅 metadata；先接精确 light LUT（包括 alpha=0 RGB），原 Gamma 规则不变，其余四个方法具名 CPU。真实 NCBind/原 CPU 算法、COW/Clip/lease、失败不重放、overflow/换代/生产 emitter→parser 与原回归通过，见 [独立记录](NATIVE-METAL-LAYEREX-IMAGE-BASELINE.md)。本项扩展 C2 更广 CPU 消费域，不替代 LayerExDraw 剩余绘制/导出/源桥接工作。
- [ ] LayerExImage Apple/新 DRACU 重采：验证原生 light 路径、默认/强制 compute、完整 App；按 v1 确认实际颜色方法及参数后再扩大支持域。原 CPU 方法只移动 acquire/origin 不算优化；支持域检查全链 read/upload/wait 与同热状态三次配对。
- [ ] 以天使纷扰的 1052×900 `layerExDraw.write` → `layerExBase` 交替访问为主回归，并加入千恋的 960×863、186×936 LayerEx 样例。重构前先证明两次完整往返由同一调用序列造成；重构后要么消除其中可 GPU 化的一段，要么记录为何 ABI/算法必须保留边界。

验收：样例的像素、raw-pointer 可见性、区域外内容、COW/lease 和资源释放保持正确；一次逻辑插件处理不会在没有新的 CPU 消费者需求时对同一完整纹理产生两次 GPU→CPU→GPU 往返。若语义确实要求多次 CPU 阶段，报告每段 origin、区域和 wait，而不是把它标成“已优化”。

新增缩放消费者样例按 **shrink → scanline → 消费者** 的整条链路验收：同条件报告总 readback calls/bytes、sync wait、CPU 消费/编码时间与逐帧长帧数，不能仅因 `shrinkCopy` 栏为零或 origin 改名而签收。可 GPU 化的消费者要求该链路像素读回为零；确需 CPU 保存的小图允许具名的一次必要读回，并验证保存结果、同步返回/异常、同版本缓存及版本变化后的正确失效。当前两次小图读回不作为 C1 算法失败，也不由其字节量较小推断等待可忽略。

**C2 后续范围：CPU 消费链（2026-10-09 After_P2C_C2B-3）。**详细范围、依赖、原始输入哈希与验收见 [CPU 消费链专项](NATIVE-METAL-P2C-C2-CPU-CONSUMERS.md)。BD7898C1的82窗口/320路由完整对账，原71调用中的arguments33已转GPU；但同构窗口仍有3次/8,029,788bytes回读，首次CPU消费者移到drawRectangle。整段继续滚动/再次绘制的LayerEx完整C0总量为66read＋66upload、每方向243,585,196bytes，read wait486.163046ms。CPU路线为record126、vectorSource62；已采到56次drawImageStretch回读212,083,200bytes，另6条read详情未采，不能补给任何方法，也不能按CPU调用数推算readback。

2026-10-09 后续四批本地签收：vectorSource原重放/span、record增量事务、drawRectangle及经实际覆盖证明的clear CPU overwrite已完成。成功whole-texture read新增与C0同锁的64槽聚合/overflow；路由v3有15＋17代表位，旧日志兼容。74调用选定链为73 GPU＋1 CPU overwrite，原精确像素和诊断开关一致；MetalLayer7/7、Backend/TJS各1/1、56解析、emitter→parser、frame/point/importer/生产syntax通过。Apple/完整原生及匹配真机重采仍待验，见 [实施记录](NATIVE-METAL-P2C-C2-REMAINDER-BASELINE.md)。

- [x] 参数补修样例：首个71次窗口GPU4/CPU67→GPU37/CPU34，arguments拒绝消除；该签收只覆盖实际支持域，不代表完整C2或整链路零往返。
- [x] vectorSource本地：原矩阵/重复项/paint/Clip顺序重放捕获后GPU合成；脚本参数固定同步采样寿命，GPU仅留复制参数，借用/逃逸/非法几何/超限具名回退。
- [x] record/drawRectangle本地：纯矩阵元数据不acquire；录制增量先暂存/预留容量，GPU后无分配发布，提交前唯一续跑/后失败不重放。原录制导出/重绘/保存同步副作用、返回/装箱/更新和COW/lease保持。
- [x] clear本地：原SRC span逐行证明完整覆盖才使用CPU overwrite，跳过旧值回读；部分/历史Clip、alias、lease、对象结果析构重入等保留原CPU，后续必要输入上传单列。45组精确对照及整链通过，设备设置场景待验。
- [x] C2A成功read聚合本地：64槽按method/nativeEntry/access/origin完整累计，容量/超长四指标分别overflow；read/caller/producer仍有界且缺失显式。无额外GPU查询/同步；alphaToProvince、缩略图及内部record重绘仍按必要CPU边界具名。
- [ ] **saveDataPack系统变量保存缺失** 单列功能兼容修复；不以跳过保存、空实现或吞异常降低CPU统计。Nekopara仍延后，C3/P2D仍保持各自范围。

专项验收同时要求精确像素/脚本/record与raw-pointer语义、整链路read/upload/wait及参数分账、诊断开关无副作用、GPU前失败唯一CPU续跑/后失败不重放，以及相同设备/场景/热状态每组三次真机原始帧样本配对。GPU命中增加、origin改名或缺失样本补零均不替代传输/同步改善证明。

#### C3. `bitmap.update`：按帧聚合小更新与 encoder 生命周期

- [x] 调用链审计（2026-10-09，第 2.7 节与[发热来源分析](NATIVE-METAL-THERMAL-ANALYSIS.md)第 4 节）：`bitmap.update` 的主体是 `LayerBitmap.cpp:2504` `InternalBlendText` 逐字形经单张共享 `_CharacterTexture`（`:2502`）调用 `Update()`（`:2568`）；`MetalLayerRenderManager.cpp:499` 直接转发到 `MetalRenderBackend.mm:1723` `UpdateLayerTexture`，每次申请 staging 并新建 blit encoder，`:811` `Blit()` 无条件 `EndOrdinary()`，下一次 `OperateRect` 经 `:857` `OrdinaryRender` 重新 `Pass(target,false)`。五份日志每秒 325–820 次、平均 0.5–1.2 KB、占 blit encoder 83–89%，每个 blit 平均额外打断 1.1–1.4 个 pass。启动资源加载（`image.load`）、movie frame（`AlphaMovie.frame`/`video.frame`）与 LayerEx 写入已有独立 origin，不混入此项；脚本原始位图写入走 lease 路径并标注 `bitmap.cpuWrite/lock/scanline`，本批可忽略。
- [x] 字形本地：可选 `TryBlendGlyph` 接入backend独占shared R8 atlas，逐次同步复制不可覆写快照并立即复用原整数算子；同一未提交命令可连续保持ordinary pass。原scratch只作拒绝/CPU回退，不暴露atlas给软件消费者；保持原像素、参数、裁剪和阴影/正文顺序。所有Submit封存页，必须以完成状态复用，不按帧数推断GPU完成。
- [x] tiny update本地：≤64 KiB对齐数据同步复制入1 MiB arena切片，连续更新共用upload encoder；下一render/compute/blit/clear/readback/capture/Submit/销毁边界关闭，重叠ROI及不同目标严格保序，大更新/容量不足走原staging。
- [x] 资源/诊断本地：16 MiB atlas含Reset后未完成旧页；16 MiB单命令arena预留，沿用原staging/submission预算与退休池。C0 shared逻辑字节不归零；有界累计glyph/tiny/encoder/CPU聚合及生产emitter→parser通过。
- [ ] Apple/真机：shared ownership、default/forced compute、diagnostic submit/wait、App及device/simulator、nominal三次配对待验；不以portable或native测试正文类型检查替代。详见[C3独立基线](NATIVE-METAL-P2C-C3-BASELINE.md)。

验收：小 bitmap 与字形的像素、dirty ROI、可见时序及 CPU/GPU 所有权正确；在选定的高频文本场景（千恋/DRACU 的 skip 或即时显示）中，blit encoder 与 render pass 数不再与 `bitmap.update` 次数近似一一对应（受真实依赖限制的例外必须记录）。报告每秒 update、blit encoder、render pass、submit、CPU encoding 时间和 frame-time，而不是只报告累计 MiB；性能改善按 D2 口径在 nominal 下配对，不得把 C3 收益与 P2D 混报。

### P2D：普通 Layer overdraw 与滚动背景性能专项

目标：处理已证实的 **GPU-bound ordinary Layer fragment workload**，特别是滚动/缩放背景叠加 full-surface Copy、CopyColor、Fill、Alpha 与 alias/snapshot 时的 pass 数、像素量和 presentation backpressure。它不属于 P2C 的 CPU/GPU boundary：`gpuSyncWaitMS=0`、读回或上传为零不能证明此类场景足够快；也不以减少诊断计数、降低画质或改变脚本绘制顺序为目标。第 2.6 节的天使纷扰道路场景是首要回归；第 2.7 节证明碎片化与 overdraw 在五款游戏的整段游玩中持续存在（去掉 blit 后仍有约 8–19 pass/帧，Fill 占像素 38%），并给出 D3 的固定开销项。

#### D0. 有界 hot-layer 归因：先把过量像素连回资源和 Layer

**首轮本地实现完成，设备归因待验：**stable Layer/texture/session/contentVersion、资产/cache/COW 来源、256 操作/64 资源采样、clear/pass 来源总账、32KiB/s 输出预算与只读分析器已接入；下列复现与归因门槛仍需新日志签收，见 [P2D 基线](NATIVE-METAL-P2D-BASELINE.md)。

- [ ] 在已存在的 `metal.layerWork` / GPU-stage 诊断之上增加**采样式**关联：operation kind、目标/源 resource generation、rect/clip、实际/缩放/alias 像素、render-pass 或 encoder、snapshot 原因；将 resource generation 在可用时关联到 asset 名与 Layer 对象/调用类别。不得长期输出每次 operation 的无界日志、持有已销毁对象，或为取名新增 GPU readback、submit、等待和资源强引用。
- [ ] 以命令像素量、fragment time、`nextDrawable` wait 或长帧为触发阈值采样，并保留未采样窗口的聚合计数。先验证 `街_通学路a.png` 是否实际参与 hotspot，而不是用加载时间直接给某个 Copy/Alpha 定责。
- [ ] 对每个 sampled hotspot 输出 Copy/CopyColor/Fill/Alpha 的像素贡献、目标完整覆盖与不透明性、clip/dirty ROI、alias/snapshot、操作顺序和中间结果是否被读取。诊断数据只用于定位；不能将重叠 GPU stage 时间相加为 command 总时长。
- [ ] 以第 2.7 节为起点，把去掉 blit 后剩余的约 8–19 pass/帧常数项，以及抽样 clears p99 78–391/帧（`MetalRenderBackend.mm:987` `Create()` 每张纹理一次 clear pass、转场 cache 分配）连回 Layer 树目标切换、cache 分配与纹理创建；先得到 pass 来源分布，再决定同目标 pass 复用或延迟 clear 的策略。

#### D1. 在严格顺序语义下减少 full-surface 工作和 pass

**首轮保守子域本地实现完成，原生及性能待验：**普通 Layer pending-zero、首个 tile pass 融合清零、完整 upload/GPU blit 取消初始 clear、无同目标活跃 pass 时完整 RGBA Fill→load-action clear，以及所有消费者初始化保护。`MIKAGE_METAL_LAYER_LAZY_INIT=0` 恢复旧创建/Fill路径用于 A/B。一般 ROI、跨目标 batching 和道路专用路径继续保留为后续候选。

- [ ] 先区分必要重绘（动画位置、透明合成、规则/clip、目标旧值或 alias 所要求）与可证明冗余的 full-surface operation。为道路滚动建立可重放存档/输入、资源尺寸、层级及逐帧 operation 序列，不以单个 heartbeat 推断所有帧。
- [ ] 只在像素/顺序证明成立时采用 dirty ROI/scissor、同目标 pass 复用/安全 batching、已知完全覆盖下的冗余 clear/fill 消除、或专用滚动背景路径。目标/源别名、COW、lease、透明 alpha、clip 边缘、目标旧值依赖和中间可见结果必须先裁决；不能用“上一帧纹理复用”替代脚本要求的当前帧组合。
- [ ] 对需要 snapshot 的操作保留正确旧图语义；仅在 source/target 不重叠或已有等价快照时减少 allocation/copy。禁止为降低 `aliasPixels` 删除正确性所需的 alias 保护。
- [ ] Fill 占第 2.7 节抽样像素 38%（五份合计 1.55 Gpx），Alpha 26%、Copy 20%、CopyColor 12%。先用 D0 证明哪些 Fill 是完全覆盖前的冗余清除或整层清屏，再按 dirty ROI、已知完全覆盖消除或 `MTLLoadActionClear`/`DontCare` 替代；不能删除脚本依赖旧像素或部分覆盖的 Fill。

#### D2. 以 drawable backpressure 与热状态作为独立性能指标

- [ ] 同时记录每个完整 command 的 GPU time、fragment stage、dispatch/encoder/pass 数、各 kind pixel、snapshot/alias bytes、CPU frame wall、`nextDrawable` wait、`gpuSyncWait` 与 thermal state。明确 `nextDrawable` 是 presentation backpressure，不能计成 CPU script 或 readback wait。
- [ ] 在 `thermalState=0/nominal` 下用相同预热、分辨率、存档、输入和计量窗口重复至少三次；热状态升高的独立样本另报，以评估 DVFS 放大效应，不能和冷机窗口相加或互相替代。

#### D3. 固定开销：0 帧转场与静止帧重复 present

- [ ] 转场 begin/stop 抖动：第 2.7 节五份日志每秒 2–9 对 begin/end，27–90% 以 `frame=0` 结束。`tjsNativeLayer.cpp:7602` `StartTransition` 在 `withchildren` 时 `IncCacheEnabledCount()`（`:4207`）→ `AllocateCache()`（`:4162`）分配整幅 cache 纹理（后端 `Create()` 附带 clear pass）并整幅标记重算，`Update(true)` 触发整树重合成；`InternalStopTransition`（`:7799`）后释放或留待 compact。先用 D0 采样证明每次的像素与 pass 代价，再评估同帧内 begin→stop 且未绘制任何转场帧时延迟 cache 分配/重合成；保留 `TransSrc`、exchange/swap、完成事件顺序、`getTransTick` 与 continuous hook 等可观察语义，不改变脚本可见的层状态与时序。调用频率来自游戏脚本，不以修改脚本行为为目标。
- [ ] 静止帧重复 present：`KRKRSession.swift:512` 每个 display tick 调 `MikageKRKRStep`，`sdl3_app.cpp:618` → `TVPCompositor.cpp:635` → `MetalRenderBackend.mm:1358` `EndFrame` 无条件 `nextDrawable`/`presentDrawable`/`Submit()`。评估窗口纹理内容版本未变时跳过 present（或空闲时降低 display link 频率），同时保持异步 alpha 的 presentation gating、显示版本语义、输入响应延迟与前后台/截屏行为；静止时抽样 GPU 0.3–0.7 ms、CPU 约 1 ms/帧，这是常驻底噪，收益按整段游玩的热状态曲线与能耗评价，不按峰值帧。

验收：转场结果画面、层顺序/位置/可见性、完成回调顺序逐像素与原路径一致；静止跳过 present 不改变任何可见帧、不延迟首个变化帧，且 presentation 版本/alpha gating 仍成立。两项均在 nominal 热状态下三次配对报告 pass/帧、GPU command、`nextDrawable` 等待与热状态曲线，并与 C3、D1 分别计量。

验收：先得到 D0 的 asset→Layer→operation 证据与可重放最小路径；随后对该路径的画面、alpha、clip、操作顺序、COW/alias 及场景切换逐像素或预定义容差一致。优化前先预注册目标设备/热状态下的 GPU command p50/p95、fragment p50/p95、`nextDrawable` p50/p95、rectPixels、pass/encoder 数、frame p50/p95/p99 和长帧数；仅在同条件重复样本显示这些指标改善时签收。不得把 C1 shrinkCopy 或 C4 transition 的改善计入 P2D，也不得因为降频/热状态变化宣称 overdraw 优化成功。

### P3：通用三角形能力

目标：覆盖旧 GL 的一般 `OperateTriangles()` 形状；这是扩展引擎能力，不只是删除 `count!=2` 检查。

- [ ] 明确顶点/UV/clip 数据结构、每输入坐标、三角形覆盖规则、绕序、重叠次序和裁剪边界。
- [ ] 首批支持单输入、已验证的 RGBA blend 和 Copy；多输入 mesh 必须逐种算子扩展，不能接受任意数量后静默丢弃输入。
- [ ] 构造独立的受控 CPU rasterizer/数学样例，结合旧 GL 输出检查通用域；不向现有仅接受 quad 的软件函数传任意 mesh。
- [ ] 对严格匹配当前软件 affine 的情况继续保留旧快路；通用 rasterization 的 top-left/像素中心规则与软件不一致时分开描述。
- [ ] 对没有安全 CPU 实现的新输入域，明确返回能力不足/受控错误，或先提供正确回退；禁止 release 下依赖被移除的 assert 保证安全。
- [ ] 保持同目标重叠绘制的顺序，验证 tile/compute 切换、snapshot 复用、暂存顶点寿命与提交预算。

验收：单个三角形、多个不连通三角形、共享边、重叠三角形、裁剪和极小三角形均有确定结果；声明支持域在原生 GPU 上验证；软件不支持域不会发生越界或误调用。

### P4：按需恢复历史扩展接口

触发条件：发现真实插件/脚本调用、准备恢复旧插件，或明确要求旧 GL 接口全兼容。未触发时保留缺口清单和错误提示，不占核心路线的首轮工期。

- [ ] 15 个方法按 color modulation 与 alpha-test 两个维度实现；给颜色、threshold、丢弃像素是否保留目标 alpha 建立契约。
- [ ] 先建立软件/数学参照或可运行旧 GL 样例，再接 Metal；当前软件没有这些名称，不能直接复用其空指针查找结果。
- [ ] `BeginStencil/EndStencil/SetRenderTarget` 若要恢复，必须恢复完整调用协议、状态复位和目标尺寸变化；不把它简单等同于现有 Emote mask。
- [ ] 动态 GLSL 若有真实需求，单独决策：支持有限已知 shader 的显式注册，或开展完整编译兼容项目。不能声称原生 Metal 自动支持任意 GLSL。

验收：每项扩展有实际调用样例、参数契约和错误路径；没有调用证据的能力标记“延后”，而非标成已完成。

### P5：已有路径的性能、资源和最终验收

- [ ] 复测当前 tile 合成；新增像素族继续共享整数逻辑，记录实际活动路径和 shader 初始化失败。普通 Layer 滚动/overdraw 的定位、候选优化和专项门槛归 P2D；P5 只覆盖 P2D 签收后的跨场景、长时间与资源生命周期回归。
- [ ] 分析真实 alias snapshot 面积、生命周期、分配和 pass 切换；优先减少可证明冗余的复制。GPU ping-pong 本身不是失败。
- [ ] 对 blur 超 64 MiB 临时预算的输入评估分块/条带算法，保留正确 halo 和 source==target 语义。没有明确需求前不取消预算。
- [ ] 将大核/偶数核/非对称 area 与当前软件规则锁定；是否恢复另一种 alpha-aware BoxBlurAlpha 作为单独兼容变更评估。
- [ ] 复核 P2C 之外的低频 CPU 边界：截图、命中测试、异步 alpha 和尚未触发的插件。CPU transition、`shrinkCopy`、LayerEx/raw-pixel、`bitmap.update` 的实施与专项验收归 P2C；这里仅处理新证据和跨路径回归，不把“未触发”写成零回读。
- [ ] 独立重放 NEKOPARA Vol.4 的零 readback 场景，审计 Emote mesh/vertices/indices、capture 区域与 GPU copy bytes、GPU stage/submission 和 presentation wait。先确认工作量与依赖，再评估冗余 draw/capture；不将 GPU submission 与重叠 stage 时间相加，不把 GPU 内部 capture copy 字节计为 CPU upload/readback。
- [ ] 独立调查冥契的牧神节 `bitmap.scanline` 调用者与 command/in-flight queue 等待：81,748 bytes / 50.564 ms read wait 和约 450.85 ms `queueWaitNS` 分别报告。捕获实际依赖、提交预算与在途深度，避免用一次小回读解释全部 queue wait，或通过新增 submit/同步将等待转移到别处。
- [ ] 保存原有异步 alpha 的 presentation gating、不可变显示版本和 simulator guard；新算子正确失效 alpha cache，HDA 仅在真正不改变 alpha 时保留缓存。
- [ ] 测试场景切换、前后台、截屏、分辨率变化、显存压力、资源销毁、异步回调与跨会话缓存。
- [ ] 沿用 `metal.gpuStages`、`metal.layerWork`、fallback/readback/upload 来源及 triangle profile；必要时新增方法×拒绝原因×像素量统计，保持有界且不添加同步。

验收：见第 8 节。发布结论必须区分“功能正确”“避免了某类传输”“同条件性能改善”，不合并为一句“全 GPU 已优化”。

## 7. 建议提交拆分与依赖

| 提交包 | 内容 | 前置 | 风险/验证重点 |
| --- | --- | --- | --- |
| A | 清单、别名/能力测试、kind 与统计约束、动态编译失败保护 | 本文 | 小；原行为不变 |
| B | RemoveOpacity、反预乘、Gamma LUT 参数支持 | A | 中；mask 格式、alpha、参数寿命 |
| C | 普通混合 7 项、SD 2 项 | A | 中；整数与输入契约 |
| D | affine alpha/const/additive 扩展 | A、对应像素族 | 高；采样边界与别名 |
| E | Ps 11 项及 affine 中适用的像素族 | A、D 的接口 | 中；表函数、变体差异 |
| F | 单源 perspective | D、语义裁决样例 | 高；坐标/多 quad/退化输入 |
| G0 | P2C origin 聚合、无同步诊断和场景基线；真机归因已签收 | F、现有 `metal.layerWork` | 中；原生开关开销与正式性能配对仍待验 |
| G4 | 高频 CPU transition GPU 保留路径；已有本地实现及实际场景证据 | G0、千恋/DRACU 实际 handler 样例 | 高；时间/规则语义、三源与回退边界 |
| G1 | `shrinkCopy` GPU 路径与完整覆盖写裁决；本地实现及 portable 验证完成 | G0、resize/copy 语义样例 | 高；filter、ROI、alias、旧目标读取；Apple/配对待验 |
| G2 | LayerEx/raw-pixel边界；C2A/C2B及vectorSource/record/drawRectangle/clear本地已实现，见[专项](NATIVE-METAL-P2C-C2-CPU-CONSUMERS.md) | G0、原plutovg/生产绑定/录制事务 | 高；ABI、COW/lease和CPU cache；Apple、整链路真机重采/配对及更广域待验 |
| G3 | `bitmap.update` 字形 atlas/staging 与 encoder 批处理；调用链已核对（第 2.7 节） | G0 | 高；更新顺序、可见时序、槽位复用与资源寿命 |
| H | P2D hot-layer 关联、目标切换/clear pass/Fill 冗余（D0/D1）、0 帧转场与静止 present 固定开销（D3）、backpressure 配对 | G0、天使纷扰道路重放、第 2.7 节会话证据 | 高；顺序/alias/clip 正确性、转场可观察语义、诊断零同步、热状态配对 |
| I | 通用三角形与安全支持域 | D、独立参照 | 高；软件回退不通用 |
| J | 按需历史 color/AlphaTest/stencil | 明确调用需求、I 中所需能力 | 高；扩展旧协议 |
| K | snapshot/blur/低频边界、Emote GPU/queue 专项调查与真机收口 | 对应功能包稳定；独立调查可先开展 | 高；归因分开，收益只能同条件实测 |

每包均包括必要的 production scalar 对照、路由与资源测试、原生 GPU 测试和文档更新。不要把所有 Metal 修改合成一个大提交，再补测试。

P2C 的 G1/G2/G3 本地实现及 portable 验证已完成，仍待 Apple/配对验收；第 2.7 节证明剩余发热来自 pass 碎片化与 overdraw，后续优先 **G3 原生/配对验证 → H（D0/D1 → D3）实施**。G4 已有本地实现和第 2.6 节实际场景证据，仍待完整原生/配对验收。H 的 D0 可以在 G0 之后立即开展，不等待 G1–G3；D1/D2 的优化提交独立签收，并在 I（P3）之前完成。保留包编号与 C4/C1/C2/C3 对应，独立依赖仍为 G0，不人为增加串行实现依赖。

跨仓库位置：主要实现属于 core；host 只在新增诊断/配置确有需要时变更；测试/CI/计划属于主仓库。实现完成后的提交和 submodule 指针更新按 core → runtime/host → 主仓库顺序处理。本计划不执行提交或推送。

不在源码静态审计阶段承诺精确人日；最大不确定项是 raw-pixel ABI 的必要边界、transition 类型的真实覆盖、hot-layer 的资产/顺序归因、几何兼容裁决、旧接口真实需求及 Apple 设备验证。A/B/C 是确定性较高的首批，G1–G4/H/I/J 需阶段性评估。

## 8. 验证矩阵与完成标准

### 8.1 像素与参数

| 维度 | 最低覆盖 |
| --- | --- |
| 通道/透明度 | 0、1、127、128、254、255；边界组合、固定种子随机；独立透明 RGB |
| opacity | 0、1、127、128、254、255；无效值的受控处理 |
| 混合变体 | normal/HDA/`_d`/`_a`、满 opacity 特支；普通与 Photoshop 方法分开 |
| 表函数 | 可穷举的通道二元组合全部遍历；检查实际软件初始化后的绑定 |
| Gamma | identity、逐通道不同 gamma/上下限、连续改参数、两个版本在同一 command buffer |
| 格式 | RGBA8/R8、错误格式拒绝、padded pitch、通道顺序 |
| ROI | 1×1、单行/列、奇数尺寸、四边裁剪、空交集、区域外像素不变 |
| 几何 | 1:1、缩放、镜像、旋转、错切、透视、shared edge、多 quad 和退化输入 |
| alias/COW | 同像素别名、偏移重叠、独立 reference、共享图像、static image、COW 后转换 |
| 资源 | CPU pin/lease、dirty ROI、同一 buffer 多次操作、切换目标、销毁/解绑/重建 |

整数颜色转换和混合以逐字节一致为默认门槛。采样/几何若客观存在旧软件与硬件 rasterization 差异，先固定允许差异的坐标域和理由，再执行验收，不能事后放宽容差使测试通过。

### 8.2 四层验证

1. **Portable 数学与路由**：生产 shader helper 的可移植提取、当前软件方法、真实 manager+COW/缓存逻辑。设备替身只能验证路由/资源契约，不能验证 GPU 执行。
2. **macOS 原生 Metal**：编译生产 MSL，验证绑定、实际像素、alias/order、提交与等待。默认 tile 和强制 compute 分别运行。无法初始化 Metal/返回 77 必须报告 skip。
3. **iOS device + simulator 构建**：验证真实框架、插件、宿主与编译条件；两类构建都成功仍不等于手机性能通过。
4. **iPhone/iPad 场景**：同场景画面、透明与输入、帧耗时和温度条件、内存、前后台/换游戏。性能测试关闭 Metal validation，正确性测试开启。

### 8.3 GPU 驻留与同步门槛

- 先上传测试输入并建立资源，再清零计数。对声明支持域内的操作，要求 `cpuFallbacks=0`、像素 `readbackBytes=0`、无重新上传整图。
- 最后为像素比较读取结果的 readback 单独计量，不将验收读回算成生产路径回退。
- 参数/LUT/顶点上传是必要 GPU 输入，单独记录；不能用“绝对零上传”误拒正确实现。
- 正常提交预算触发允许保留；不新增每算子提交或 `waitUntilCompleted`。诊断开启不得增加额外提交/等待。
- 对应禁用条件和设备失败必须有测试，确保回退保留输入、区域外像素和参数状态；失败不能先改一部分目标，再重复执行整个软件操作。
- 对 P2C，先同时查看按 origin 聚合与 texture-ID top-N，报告 calls、bytes、wallNS、sync waitNS；`other` 只能是有界的补充桶，不能作为关键 stall 的最终归因。对不需要 CPU 的指定支持域，逐 origin 断言为零；对 ABI/算法确需 CPU 的路径，断言一次边界的区域、原因和顺序正确，而不是用全局总数掩盖它。
- 每次回归保留 C0 对账：在同一 session、计数 epoch 与完整窗口内，origin upload/read bytes（含显式 overflow）与总体计数一致。第 2.5 节样本的 read calls/waitNS 还与 syncWaits/syncWaitNS 精确一致；若后续引入其它同步或不同计数域，须逐项解释差异，不能改写 origin 以凑齐总数。已知 origin 不应溢出，出现 missing/overflow/采样丢失必须报告覆盖范围。
- 小传输验收必须同时记录次数、每秒峰值、encoder/submit 数、CPU encoding 时间和 frame-time 分布。小于 1 MiB 的 readback 仍可能等待前序 GPU 工作，禁止以累计 bytes 小而跳过同步指标。
- 对 P2D，将 `nextDrawable`、`gpuSyncWait`、CPU script/encoding 与 GPU command/fragment 时间分开计量；`nextDrawable` 只能说明 drawable/presentation backpressure，不能归入 readback、CPU script 或任一重叠 stage。按 command 记录的 operation pixel、pass/encoder、alias/snapshot 和 fragment 数据须与完整命令边界对应，不将重叠 stage 相加为总 GPU 时间。

### 8.4 真机场景与指标

| 场景 | 主要验证 | 基线来源/限制 |
| --- | --- | --- |
| 9nine UI/效果 | Gamma 与 Ps/普通混合、透明和短时尖峰 | D1 曾记录少量 Gamma 回退；需新版本同条件复测 |
| 天使纷扰设置/鉴赏 | Stretch、复制/混合、fallback 和传输 | 已有修复不能列为本计划新成果 |
| 天使纷扰后期剧情 | tile 与 compute 成本、持续帧时间及热状态 | D1 有低回读下 GPU/展示等待证据，不能只看回退数 |
| 天使纷扰走路/道路滚动 | 普通 Layer hot-layer、full-surface overdraw、pass 数、fragment 与 `nextDrawable` backpressure | P2D 首要回归；第 2.6 节仅签收触发/归因，先补 asset→Layer→operation 采样与 same-thermal 配对 |
| 千恋万花 CPU transition 流程（首要回归） | source/rule/output 驻留、画面/时间步进、传输与长帧 | P2C C4；C0 前为 263 次 output / 2,080.37 MiB、40 次 source / 763.42 ms wait；C4 后样本为 545 frame / 545 GPU / 0 CPU，仍需受控配对 |
| DRACU-RIOT! CPU transition 流程（交叉回归） | 源输出尺寸不同、handler 支持与回退、同步 | P2C C4；C0 前 17 次 source / 132.93 ms、14 次 output；C4 后 crossfade 449 / 449 GPU / 0 CPU，验证 1280×960 → 1280×720 的实际语义 |
| DRACU-RIOT!、天使纷扰、9-nine 缩放触发流程 | `shrinkCopy` 小读回的 p50/p95 wait、长帧 | P2C C1；C0 后分别约 177.2、368.2、47.6 ms wait；9-nine readback 全来自 shrinkCopy |
| 天使纷扰与千恋万花 LayerEx 触发流程 | raw-pixel boundary、同 texture ping-pong、CPU wait | P2C C2；天使重放 1052×900 样例，千恋加入 960×863/186×936；需确认插件功能与调用栈 |
| 高频 UI/文本流程（少女领域、DRACU-RIOT!、9-nine、青空、千恋各至少一个） | `bitmap.update` rate、blit encoder/submit、CPU encoding | P2C C3；调用链已证实，本地atlas/tiny合批完成；native/nominal配对待验，不将相关性当成性能收益 |
| 千恋万花流程图首次/再次打开 | 图像加载、脚本时间与渲染的分离 | 解码/首次加载成本不由 Metal 算子覆盖自动解决 |
| affine/perspective 专用场景 | 正确坐标、边缘、COW、回退来源 | 若游戏缺少稳定样例，先制作可重放最小脚本 |
| Emote 静置及连续点击 | mesh、mask、capture、显示版本与输入 | 与 ordinary Layer 缺口分开归因；保留 async alpha 行为 |
| NEKOPARA Vol.4 Emote 场景 | 零 readback 下的 mesh/capture GPU 工作量与呈现等待 | P5 独立调查；C0 后 readback/wait 均为零，P2C 不能作为该场景全部性能问题的解决验收 |
| 冥契的牧神节 scanline/队列场景 | 小读回单次 stall、command/in-flight queue wait | P5 独立调查；81,748-byte read / 50.564 ms 与约 450.85 ms queue wait 分开计量 |
| 重复换游戏/前后台/截图 | 资源寿命、内存与 session cache | 不把单场景通过视为全生命周期通过 |

记录设备/OS、三层 commit、渲染开关、场景与输入步骤、分辨率、诊断模式、初始/末尾热状态、运行时间。帧时间记录 p50/p95/p99 和长帧数；固定热状态下的冷启动与稳定运行分别比较，至少重复 3 次。

C4/C1/P2D 正式 before/after 优先等待设备恢复 `thermalState=0/nominal`，采用相同预热、计量窗口与输入步骤，并记录沿途热状态。温度阶段不同的窗口分开报告或重采，不混入同条件百分比；第 2.5 节大部分样本与第 2.6 节道路样本均非受控同热状态配对，只签收归因与路径触发。存档/操作步骤、完整版本或逐帧覆盖缺失时明确列出，不由当前日志补造条件。

同时记录 fallback 按原因/方法分布、像素回读/上传、GPU snapshot 字节、GPU stage 与 command 时间、CPU/nextDrawable 等待、内存峰值。stage 可能重叠，不能相加为 command 总预算；旧累计 MB 不直接换算成物理带宽或功耗。

### 8.5 现有验证入口

以下是实施阶段的命令模板，**不是本轮已执行结果**。在仓库根目录运行；各平台补齐现有 CMake 依赖。

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure --verbose

cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel
ctest --test-dir build/metal-render-tests --output-on-failure
```

Apple 配置参照现有 `.github/workflows/ios.yml`，提供 SDL3 的 `CMAKE_PREFIX_PATH`。T1 的 Apple CTest 已注册默认和 `MIKAGE_METAL_LAYER_TILE_RENDERER=0` 两套；现有 CI 还包含 Release composition workload。继续使用，不重复搭一套只验证测试替身的流水线。

新增更细的 fallback/采样强制开关时，必须保证它只用于验证，并记录实际路径；当前强制 compute 不代表已经覆盖不支持 read-write texture 的 snapshot compute 设备路径，必要时补可控注入。

### 8.6 分阶段签收

- [x] P0：静态清单与运行时名称/对象关系一致，已有 portable 像素/路由回归通过；原生 Metal 与新真机基线待验，见签收范围。
- [ ] P1：24 个矩形语义缺口完成，全部支持域/回退域有证据。
- [ ] P2：常用 affine 混合及单输入 perspective 留在 GPU，几何边界裁决完成。
- [x] P2C C0 真机归因：`4943c0e47665` 的 8 份日志 origin bytes/calls/wait 精确对账，overflow 为零；已足以安排 C4 → C1 → C2 → C3。
- [x] P2C C4 本地与实际场景：七种真实 handler 1962 精确案例、拒绝/异常/COW/lease、诊断与原回归通过；`56b2799f8068` 的千恋、DRACU、天使纷扰实测 transition 均为 GPU/0 CPU/零像素往返。全 handler 原生矩阵、受控同热状态配对仍待验，见第 2.6 节与 C4 独立基线。
- [x] P2C C1 本地：两种 shrinkCopy 的精确整数算法、安全自别名、COW/resize/lease、事务拒绝与诊断日志闭环已实现；60 个真实插件精确对照、2048 组依赖证明、12 个故障事务和原 portable 回归通过。签收时未提交，当前 C1 包已提交；Apple/Swift/App、真实 GPU 同步和同条件真机配对仍待验，见 [C1 独立基线](NATIVE-METAL-P2C-C1-BASELINE.md)。
- [x] P2C C2A 本地：消费端/shrink 输出有界关联、元数据操作零像素访问、绘制/record租约及原错误顺序、外部重入目标替换/resize通过生产边界测试；32项解析和原回归通过。真实plutovg/Apple/App及新真机链路按后续记录验收；本包已提交；见 [C2A 独立基线](NATIVE-METAL-P2C-C2A-BASELINE.md)，不替代全部C2验收。
- [x] P2C C2B 本地：流程图三个绘制入口、原 plutovg CPU/采样与 GPU 整数组合、真实完整像素 oracle 和生产绑定事务通过；首批与类型/统计修复已提交，5987acb样例支持部分实际GPU及626窗口对账。本轮最小参数和已知HSV字节码补修后，MetalLayer6/6、Backend/TJS各1/1、其它TJS依赖回归和51解析通过；补修已提交，After-3参数命中和v2对账通过；完整Apple/设置闭环及C2签收待验，见 [C2B 基线](NATIVE-METAL-P2C-C2B-BASELINE.md)、[类型/统计记录](NATIVE-METAL-C2B-HIT-FIX-BASELINE.md)及[本轮记录](NATIVE-METAL-C2B-ARITY-BASELINE.md)。
- [ ] P2C C0 剩余验证：Apple 原生诊断开关无额外 submit/wait、App XCTest/界面、统计开销及同热状态性能配对有独立证据；不由对账签收替代。
- [ ] P2C：按 origin 的无同步诊断与同条件基线齐全；已选择的 `shrinkCopy`、LayerEx、tiny update、transition 子项分别满足其专属正确性和传输/同步门槛。未选择或仍必须 CPU 的子项有具名原因。
- [x] P2C C2后续本地选定域：vectorSource/record/drawRectangle/clear与有界成功read聚合完成，原像素/事务/计数回归通过；见[实施记录](NATIVE-METAL-P2C-C2-REMAINDER-BASELINE.md)。此项不签收更广CPU消费、原生或性能。
- [ ] P2C C2原生与整链重采：新v3/CPU聚合完整对账，涵盖录制导出/重绘和必要CPU保存，不能仅移动回读来源；Apple/App及三次受控配对仍待验。
- [ ] P2C C3：2026-10-10字形atlas/tiny合批本地实现，MetalLayer7/7、Backend2/2及生产TU/生命周期检查通过；详见[C3基线](NATIVE-METAL-P2C-C3-BASELINE.md)。原生/真机仍须证明高频文本blit/pass不再≈update，像素/时序/所有权正确，并nominal配对报告update、encoder、pass、submit、CPU encoding与帧时间；必要snapshot-compute例外单列。
- [ ] P2D：天使纷扰道路滚动完成 D0 asset→Layer→operation 归因；严格保持 Layer 像素/顺序/alias 语义的优化在 nominal/same-thermal 重复样本中降低 GPU fragment/command、`nextDrawable` backpressure、pass 或冗余像素与长帧；D3 的 0 帧转场与静止 present 改动保持可观察语义并同条件配对。C1/C4 的传输改善与 C3 的字形改善不得冒充 P2D 收益。
- [ ] P3：通用三角形域有独立参照与安全拒绝/回退。
- [ ] P4：逐项标明“未触发/延后/已验收”，不作为当前核心功能完成的隐含条件。
- [ ] P5：Apple 测试与 device/simulator 构建通过，目标真机场景达到预先保存的正确性和性能门槛。

允许声明核心路线完成的条件是 P0/P1/P2/P2C/P2D/P3/P5 验收齐全，P4 有明确处置记录。允许先发布较小功能包，但只能声明该包已覆盖的输入域，不能写“Kirikiroid 全兼容”。

## 9. 初始审计与 P0/P1A/P1B/P2A/P2B 实施状态

初始审计已完成：固定版本源码获取、旧 GL/当前软件/Metal 注册与别名核对、接口及几何/blur/回读审计、现有测试与 CI 阅读、实施依赖和验收计划。

P0/P1A/P1B/P2A/P2B 与 ARC 补修已提交；用户报告 P2A 测试暂未发现问题。P1B 能力审计为 70/70/0/15；P2A/P2B 的几何实现和证据各自保存。P2C C0 的诊断、逐帧、解析与版本 HEAD 已提交；独立 [C0 记录](NATIVE-METAL-P2C-C0-BASELINE.md)保留 simulator 大数组 Swift importer 失败、只读 C accessor 补修及当时待验状态。C0 该次记录的主仓库 HEAD 为 `4943c0e47665433ef0c1d139d9043c408ef5c33a`，新增 8 份同短 revision 真机日志已验证 origin 对账；该证据不自动签收全部 Apple CI、App XCTest 或诊断开关开销。

仍未完成：C0剩余Apple专项与同热状态性能对照、C4全handler原生/App/受控配对、C1 Apple/Swift/原生和配对、C2完整Apple/App/设置闭环与本轮v3全链路重采、更广CPU消费域、C3原生/真机配对、P2D、P3–P5和新原生像素/性能基线。C1/C2A/C2B、参数/已知HSV补修已发布；After-3确认参数GPU命中、HSV applied及297窗口对账。本轮vectorSource/record/drawRectangle/clear和成功read聚合本地完成，证据见独立记录；不以portable或旧日志替代新原生验证。P2D与P5保持独立范围，性能结论仍要求同热状态配对。2026-10-09 的 After_P2C_C2_1 五份日志已把剩余发热定位到 pass 碎片化（C3）与 overdraw/固定开销（P2D D0/D1/D3），回读/同步不再是主要开销；证据与调用链见第 2.7 节和[发热来源分析](NATIVE-METAL-THERMAL-ANALYSIS.md)，`scripts/analyze-thermal-passes.py` 可对新日志重算同一组指标。

## 附录 A：完整名称对照

“已映射”只表示名称能取得非 Unsupported 的描述符；实际 GPU 路由还受第 4.3 节约束。几何列中的例外优先于名称状态。

| 名称 | 旧 GL 注册行 | 当前软件注册 | Metal 描述符/差距 | 工作项 |
| --- | ---: | --- | --- | --- |
| `AddBlend` | 2987 | 有 | 已映射：`Add` | 保留并回归 |
| `AdditiveAlphaBlend` | 3013 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaBlend_HDA` | — | 有 | 共享 `AdditiveAlphaBlend` 对象 | 保留别名测试 |
| `AdditiveAlphaBlend_a` | 3018 | 有 | 已映射：`AdditiveAlpha` | 保留并回归 |
| `AdditiveAlphaToAlpha` | 3246 | 有 | 已映射：`AdditiveAlphaToAlpha` | P1A 本地完成；Apple/真机待验 |
| `AdjustGamma` | 2731 | 有 | 已映射：`AdjustGamma` | P1A owned LUT；Apple/真机待验 |
| `AdjustGamma_a` | 2771 | 有 | 已映射：`AdjustGamma`，premultiplied flag | P1A owned LUT/索引修复；Apple/真机待验 |
| `AlphaBlend` | 2881 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_HDA` | — | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_SD` | 2912 | 有 | 已映射：`AlphaSD`，单输入 | P1A 本地完成；alpha=0 |
| `AlphaBlend_a` | 2932 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaBlend_color` | 2887 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_AlphaTest` | 2893 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_a` | 2939 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_a_AlphaTest` | 2945 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_d` | 2973 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_color_d_AlphaTest` | 2975 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaBlend_d` | 2963 | 有 | 已映射：`Alpha` | 保留并回归 |
| `AlphaTest` | 2897 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `AlphaToAdditiveAlpha` | 3256 | 有 | 已映射：`AlphaToAdditiveAlpha` | 保留并回归 |
| `ApplyColorMap` | 2590 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `ApplyColorMap_a` | 2608 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `ApplyColorMap_d` | 2598 | 有 | 已映射：`ColorMap` | 保留并回归 |
| `BoxBlur` | 3277 | 有 | 已映射：`BoxBlur` | P5：预算/语义边界 |
| `BoxBlurAlpha` | 3292 | 有 | 已映射：`BoxBlur` | P5：预算/语义边界 |
| `ColorDodgeBlend` | 3026 | 有 | 已映射：`ColorDodge` | P1A 本地完成；Apple/真机待验 |
| `ConstAlphaBlend` | 2709 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_HDA` | — | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_SD` | 2788 | 有 | 已映射：`ConstAlphaSD` | 保留双源路径 |
| `ConstAlphaBlend_SD_a` | 2793 | 有 | 已映射：`ConstAlphaSD`，premultiplied flag | P1A 独立双源 ARGB 公式 |
| `ConstAlphaBlend_SD_d` | 2796 | 有 | 已映射：`ConstAlphaSD` | 保留双源路径 |
| `ConstAlphaBlend_a` | 2714 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstAlphaBlend_d` | 2722 | 有 | 已映射：`ConstAlpha` | 保留并回归 |
| `ConstColorAlphaBlend` | 2644 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `ConstColorAlphaBlend_a` | 2649 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `ConstColorAlphaBlend_d` | 2665 | 有 | 已映射：`FillBlend` | 保留并回归 |
| `Copy` | 2587 | 有 | 已映射：`Copy` | 保留并回归 |
| `CopyBlueToAlpha` | — | 有 | 已映射：`CopyBlueToAlpha` | 保留并回归 |
| `CopyColor` | 2699 | 有 | 已映射：`CopyColor` | 保留并回归 |
| `CopyMask` | 2703 | 有 | 已映射：`CopyMask` | 保留并回归 |
| `CopyOpaqueImage` | 2692 | 有 | 已映射：`CopyOpaque` | 保留并回归 |
| `DarkenBlend` | 3034 | 有 | 已映射：`Darken` | P1A 本地完成；Apple/真机待验 |
| `DoGrayScale` | 3267 | 有 | 已映射：`GrayScale` | 保留并回归 |
| `FillARGB` | 2633 | 有 | 已映射：`Fill` | 保留并回归 |
| `FillColor` | 2636 | 有 | 已映射：`FillColor` | 保留并回归 |
| `FillMask` | 2639 | 有 | 已映射：`FillMask` | 保留并回归 |
| `LightenBlend` | 3040 | 有 | 已映射：`Lighten` | P1A 本地完成；Apple/真机待验 |
| `MulBlend` | 2999 | 有 | 已映射：`Mul` | P1A 独立对象/flags；当前保留 alpha |
| `MulBlend_HDA` | 3006 | 有 | 已映射：`Mul`，HDA flag | P1A 独立对象/flags；保留 alpha |
| `MultiplyAlpha` | — | 有 | 已映射：`MultiplyAlpha` | 保留并回归 |
| `PerspectiveAlphaBlend_a` | 2923 | 有 | 共享 `AlphaBlend_a` 对象 | P2B：单 RGBA 源透视支持域本地完成，Apple/真机待验 |
| `PsAddBlend` | 3058 | 有 | 已映射：`PsAdd` | P1B 本地完成；Apple/真机待验 |
| `PsAddBlend_color` | 3066 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAddBlend_color_AlphaTest` | 3068 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsAlphaBlend` | 2917 | 有 | 已映射：`PsAlpha` | P1B 本地完成；Apple/真机待验 |
| `PsColorBurnBlend` | 3175 | 有 | 已映射：`PsColorBurn`，三表资源 | P1B 本地完成；Apple/真机待验 |
| `PsColorDodge5Blend` | 3168 | 有 | 已映射：`PsColorDodge5` | 保留并回归 |
| `PsColorDodgeBlend` | 3159 | 有 | 已映射：`PsColorDodge`，三表资源 | P1B 与 Dodge5 分开验证 |
| `PsDarkenBlend` | 3190 | 有 | 已映射：`PsDarken` | P1B 本地完成；Apple/真机待验 |
| `PsDiff5Blend` | 3202 | 有 | 已映射：`PsDiff5` | P1B 源先 fade 再 difference |
| `PsDiffBlend` | 3196 | 有 | 已映射：`PsDiff` | P1B difference 后插值 |
| `PsExclusionBlend` | 3209 | 有 | 已映射：`PsExclusion` | P1B 本地完成；Apple/真机待验 |
| `PsHardLightBlend` | 3143 | 有 | 已映射：`PsHardLight` | 保留并回归 |
| `PsHardLightBlend_HDA` | — | 有 | 共享 `PsHardLightBlend` 对象 | 保留别名测试 |
| `PsLightenBlend` | 3184 | 有 | 已映射：`PsLighten` | P1B 本地完成；Apple/真机待验 |
| `PsMulBlend` | 3110 | 有 | 已映射：`PsMul` | 保留并回归 |
| `PsMulBlend_HDA` | — | 有 | 共享 `PsMulBlend` 对象 | 保留别名测试 |
| `PsMulBlend_color` | 3120 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsMulBlend_color_AlphaTest` | 3123 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsOverlayBlend` | 3135 | 有 | 已映射：`PsOverlay` | 保留并回归 |
| `PsOverlayBlend_HDA` | — | 有 | 共享 `PsOverlayBlend` 对象 | 保留别名测试 |
| `PsScreenBlend` | 3219 | 有 | 已映射：`PsScreen` | 保留并回归 |
| `PsScreenBlend_color` | 3228 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsScreenBlend_color_AlphaTest` | 3231 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSoftLightBlend` | 3151 | 有 | 已映射：`PsSoftLight`，三表资源 | P1B 使用真实初始化软件表 |
| `PsSubBlend` | 3083 | 有 | 已映射：`PsSub` | P1B 本地完成；Apple/真机待验 |
| `PsSubBlend_color` | 3091 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `PsSubBlend_color_AlphaTest` | 3093 | 无 | 当前未注册 | P4：按调用需求恢复 |
| `RemoveConstOpacity` | 2683 | 有 | 已映射：`RemoveConstOpacity` | 保留并回归 |
| `RemoveOpacity` | 2619 | 有 | 已映射：`RemoveOpacity`，R8 mask | P1A 同尺寸合法 ROI；缩放/镜像拒绝 |
| `ScreenBlend` | 3047 | 有 | 已映射：`Screen` | P1A 本地完成；Apple/真机待验 |
| `SubBlend` | 2992 | 有 | 已映射：`Sub` | P1A 本地完成；Apple/真机待验 |
| `UnivTransBlend` | 2805 | 有 | 已映射：`UnivTrans` | 保留三源路径 |
| `UnivTransBlend_a` | 2856 | 有 | 已映射：`UnivTrans` | 保留三源路径 |
| `UnivTransBlend_d` | 2832 | 有 | 已映射：`UnivTrans` | 保留三源路径 |

## 附录 B：实施时最容易漏掉的修改点

| 修改 | 必须联动检查 |
| --- | --- |
| 新增 operation kind | 枚举编号、MSL switch、输入判断、统计数组/循环、日志解析、设备替身与 native tests |
| 新增 source 类型 | C2 格式/输入数检查、C3 资源绑定、MSL channel、R8 pitch、错误格式回退 |
| 新增透明度变体 | canonical/alias 对象、SetParameterOpa、HDA/`_d`/`_a`、point/alpha cache invalidation |
| 新增 Gamma/LUT | 参数快照、版本缓存、资源存活、tile/compute 绑定、连续多次编码 |
| 新增几何 | backend 默认能力、manager 路由、坐标/clip、支持域、fallback 安全性、诊断 |
| 放宽 alias | 明确旧图 snapshot 或顺序相关语义，COW/reference，以及部分写入后失败 |
| 优化 blur | 64 MiB 预算、halo、边缘分母、整数溢出界限、偶数核、源目标重叠 |
| 改合成 pass | 目标切换、upload/blit/compute/readback 顺序、呈现/截图、批处理和提交预算 |
| 改资源缓存 | pin/lease、dirty ROI、CPU cache、会话解绑、游戏切换和异步回调 |
| 改 CPU pixel 边界 | 调用栈/用途分类、完整覆盖证明、ROI、source/target 驻留、CPU cache、COW/lease、alpha cache、同步 wait 与回退可见性 |
| 合并 tiny upload | dirty-rect 顺序、同帧可见性、中间读取、staging 生命周期/背压/上限、encoder/submit 预算、场景切换与跨会话销毁 |
| 优化 ordinary Layer overdraw | asset→resource→Layer→operation 有界采样、各 kind 像素/clip/opacity、pass/encoder、alias/snapshot、顺序语义、fragment/command 与 `nextDrawable` 分账、same-thermal 配对 |
| 改传输诊断 | 按 origin 聚合与 texture 明细同时保留、calls/bytes/wallNS/waitNS、一致的 origin 名、`other` 上限、关闭诊断后的时序不变 |

[k-register]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2587-L3307
[k-readback]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2114-L2143
[k-fetch]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2175-L2208
[k-geometry]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L3759-L4032
[k-interface]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/RenderManager.h
[k-tests]: https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl_test.hpp
[c-mapping]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L2705-L3122
[c-manager]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/MetalLayerRenderManager.cpp
[c-metal]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/backend/MetalRenderBackend.mm
[c-shaders]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/backend/MetalLayerShaders.h
[c-software-geometry]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L3674-L5030
[c-imageutils]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.cpp#L244-L539
[c-operation]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/LayerRenderOperation.h
[c-interface]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/TVPCompositor.h
[c-render-interface]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/RenderManager.h
[c-blend-bindings]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/gl/blend_function.cpp
[c-tvpgl]: https://github.com/itsCheney/krkrsdl3/blob/49bf9f70fb4866515a26e6dd9a7aa89fa2447083/core/render/gl/tvpgl.cpp
[t-layer]: ../Tests/MetalLayer/MetalLayerTests.cpp
[t-layer-cmake]: ../Tests/MetalLayer/CMakeLists.txt
[t-backend]: ../Tests/MetalRenderBackend/MetalRenderBackendTests.cpp
[t-ci]: ../.github/workflows/ios.yml
[d-third]: THIRD-ROUND-LAYER-PERFORMANCE.md
[d-followup]: FOLLOWUP-LAYER-PERFORMANCE.md
