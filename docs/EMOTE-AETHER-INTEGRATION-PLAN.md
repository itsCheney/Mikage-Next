# AetherKrkr Emote 动画核心接入计划

日期：2026-10-03

状态：首轮候选实现已从 `emote_dev` 合并至 `metal_dev`；用户反馈测试场景动画连贯，完整兼容性验收仍按本文推进。
当前实现和限制见 [首轮实现记录](EMOTE-AETHER-INTEGRATION-STATUS.md)。

## 1. 目标与接入决策

将 AetherKrkr 公开实现中的时间轴、变量动画器、差分混合及经过验证的节点插值规则接入现有 KRKRSDL3 Emote 插件，解决时间推进错误、姿态跳变和过渡缺失，同时保留现有 Metal 绘制与图层合成链路。

采用“动画核心适配”的方式。不要整目录替换 `plugins/emoteplayer`，也不要把 AetherKrkr 的 Godot 渲染、PSB 对象体系和宿主构建系统一起引入。

本计划的成功需要两类证据：动画轨迹符合预期，以及同场景真机顿挫得到改善。HUD 显示 60 FPS、代码已使用 easing 或单元测试通过，均不能单独证明实际卡顿已经解决。

### 范围

| 纳入 | 本次不纳入 |
| --- | --- |
| 每个播放器、每条 timeline 的独立运行状态 | Godot GPU bridge、Godot 宿主 |
| 循环、非循环、播放/停止、查询和 flags 适配 | AetherInternal 私有包或原生 SDK 后端 |
| `setVariable` 的时长/easing、过渡队列 | 胸部、头发、服饰的完整物理解算 |
| 主 timeline、差分 timeline、混合与淡入淡出 | 替换现有 PSB 加载器与共享字节缓存 |
| 节点 frame type、曲线和网格控制点插值 | 重写 Metal 后端、双人 GPU 性能优化 |
| clone、存读档、暂停恢复、会话清理 | 导入商业游戏数据或自行发送/发布代码 |
| 动画轨迹诊断和回归夹具 | 与 Emote 无关的插件补全 |

如果实际症状主要来自绘制节奏或 GPU 等待，保留已证实的动画修复，另记录渲染问题；不要为了得到更平滑的观感任意改变动画轨迹。

## 2. 参考版本、事实与待验证语义

源实现固定为 [AetherKiri/AetherKrkr 的 fa0f8af9614865aebcb839abd32bc181643d1e4a](https://github.com/AetherKiri/AetherKrkr/tree/fa0f8af9614865aebcb839abd32bc181643d1e4a)。后续不跟随 `main` 自动更新。实施时另记录当前 app、build fork 和嵌套 core 的实际提交与工作区差异；现有 README 中的 pin 不能代替现场记录。

| 项目 | 当前 KRKRSDL3 | AetherKrkr 公开实现 | 接入要求 |
| --- | --- | --- | --- |
| Timeline 状态 | `currTimeline` 共用 `currStartTick`；启动、停止、回绕都会改写它 | 每条 timeline 独立 `currentTime`、playing、blend 与 track 状态 | 移除跨 timeline 时间耦合 |
| 起播 | `playTimeline` 使用 `-10000` 起点 | 播放时从 0 初始化状态 | 明确初始变量、起始段和第一次更新行为 |
| 循环 | 回绕直接把起点设为当前 tick | 处理跨越边界，并保留剩余时间 | 验证端点包含规则和一次跨越多个周期 |
| 变量缓动 | timeline 线性插值；脚本过渡参数被忽略 | 按关键帧调度变量动画器，使用指数权重 | 迁移调度语义，不能只在旧 lerp 上加一个函数 |
| 节点 type | `2→2` 允许插值；另有 motion 终止拦截 | 节点 `0` 不可见、`2` 保持、`3` 向下一帧插值 | 以 PSB 版本和参考轨迹确认，区别于 timeline 变量帧 |
| 曲线 | 部分字段已解析，保存固定四个 x/y 控制点，未参与求值 | 多段曲线及按通道求值 | 扩展解析，逐通道验证字段含义与公式 |
| 时间单位 | D3D frames 转 ms 后除以默认 `speedRatio=20` | 普通 Emote ms 转 60 Hz frames；D3D 直接接收 frames | 先核对脚本契约，不直接改全局 speed/tickCount 语义 |
| 可变变量 | 写入文件 metadata、selector 和部分 motion 参数缓存 | Player 持有变量和动画器状态 | 共享同一资源的 player/clone 不得互相改写运行状态 |
| 渲染/物理 | 现有 `iTVPRenderBackend` 与 Metal 网格路径；物理尚未完整实现 | Godot 绘制与可选 native backend；公开物理入口依赖扩展 | 保留现有渲染；不承诺补齐私有后端能力 |

AetherKrkr 是逆向参考实现，源码中的二进制地址、注释和已有测试不等于官方规范。尤其以下事项必须先确认：

- 节点帧和 timeline 变量帧是否使用相同的 type 语义。
- easing 属于哪个关键帧，以及过渡为何存在一帧偏移。公开代码的变量调度与简单“两帧之间插值”不同。
- `ccc` 在不同求值路径中的用途；不能只根据本地“坐标曲线”注释认定所有通道都使用同一曲线。
- 多段 Bezier 的分段、参数归一化和 x/y 的含义。不能直接假定它等同于 CSS cubic-bezier。
- 差分、并行、队列、instant variable 与原始 flags 的关系；不直接照搬枚举名称解释 bit 含义。
- `loopBegin` 前的起始段、`loopEnd` 端点、type 0 终止帧以及结束后保持值。

### 许可证处理

AetherKrkr 声明 [GPL-3.0-or-later](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/README.md)，本项目 runtime 的已有条款见 `Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt`。

在复制或修改其代码、测试前，记录来源、版权声明、适用条款以及与本项目分发方式的兼容结论。不能把复制后的代码自动归入当前 runtime 的许可证。若许可路径未确定，可继续做接口设计、数据取证和行为实验；实现需采用确认可用的许可路径，或从独立确认的格式/行为规格实现，并重新核对来源。

## 3. 目标架构与文件边界

```text
TJS EmotePlayer / Motion.Player / D3DEmotePlayer
                |
                v
接口适配：时间单位、原始 flags、可选参数、播放/暂停/seek
                |
                v
每个播放器的动画状态
  timeline clocks / variable animators / selectors / blend / ordered evaluation
                |
                v
本帧最终变量表 + 节点插值结果
                |
                v
现有 motion/ref 树、网格、EmoteHitFrame
                |
                v
iTVPRenderBackend -> Metal / 软件回退 -> 当前 Layer / D3DLayer
```

### 建议新增的内部模块

以下名称为拟定接口，不是已存在文件：

| 模块 | 职责 |
| --- | --- |
| `emoteanimation.h/.cpp` | 不依赖 SDL/Metal/TJS 的 timeline 状态、变量动画器、队列与确定性推进 |
| `emoteinterpolation.h/.cpp` | 经验证的 easing、曲线、节点保持/插值与网格控制点求值 |
| `emoteanimationadapter.h/.cpp` | 从当前 `emotefile` 数据生成控制描述；接入变量、selector 与 motion 参数读取 |

所有模块位于 `Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/`，使用现有 C++17 工具链。生产构建是否需要显式添加源文件，以实际 core/build CMake 为准。

数据分为只读定义与播放器状态。资源对象提供帧、范围、标签和默认值；player 持有当前值、timeline clock、游标、队列和混合状态。适配层沿用现有标签及尾随 NUL 查找约定，不在这次迁移中清理字符串格式。

必须贯通变量读取链：`emotemotionref::getTickByIdx`、file/engine 的变量查询、selector 派生变量以及 `motionName/varName` 参数缓存都读取当前 player 的结果。仅把新值放进 `_varCache`，而节点继续读取共享 `_metadata->_varList`，不算完成接入。

### 现有文件的改动范围

| 位置 | 计划改动 |
| --- | --- |
| `emoterunner.h/.cpp` | 接入独立控制状态和节点求值；更新参数读取；保留 ref 池、网格、绘制及命中快照 |
| `emotefile.h/.cpp` | 补充必要控制描述/曲线解析；维护当前 PSB 类型、缺省值和资源读取行为 |
| `emoteplayerclass.h/.cpp` | 普通 Emote 接口、暂停/结束判定、变量过渡、查询、序列化 |
| `emoteplayer.cpp` | TJS 可选参数注册与兼容回调 |
| `DrawDeviceD3D/D3DEmotePlayer.h/.cpp`、`DrawDeviceD3D_reg.cpp` | D3D 时间/参数转发和 clone 状态复制；不改变当前绘制目标 |
| `Tests/EmoteAnimation/`（新增） | 直接编译生产动画核心，验证轨迹和状态契约 |
| `Tests/EmoteMetadata/` | 扩展曲线/控制描述的生产解析夹具 |
| `.github/workflows/ios.yml` | 接入新增测试；保留原生 Metal、Layer、缓存与生命周期检查 |

## 4. 实施阶段与完成条件

### M0：基线、数据与契约确认

1. 固定来源版本，完成许可路径记录。阅读现有诊断说明，保留目前能复现的问题场景和构建标识。
2. 为每个 player 采集 `progress` 调用时间、输入 dt、主 motion 状态、timeline 时间、变量更新与姿态版本。用固定容量环形缓冲逐帧保存，每秒输出摘要，异常时输出边界前后片段；不通过“每秒只采一帧”代替逐帧诊断。
3. 同时关联 draw/prepare 的姿态版本、宿主 frame 编号和现有 GPU/等待记录。区分动画没有更新、变量本身跳变、画面重复和显示节奏不均。GPU 异步记录必须按编号关联，不能与平均 CPU 时间直接相加。
4. 用本地环境变量 `MIKAGE_EMOTE_PSB_PATH` 指定受测 PSB，导出 timeline/type/easing/instant variable/曲线键与长度分布。工具复用生产解析，不提交资源、完整 PSB dump 或私有路径。
5. 记录实际脚本调用：入口类别、参数数目、flags、过渡时长、speed/tickCount 使用、是否有 `progress(0)` 和主 motion 结束后继续播放的 timeline。
6. 明确时间契约：普通 Emote、Motion.Player、D3D 的 dt、transition、tickCount 和 speed 分别是什么单位。当前 D3D 默认转换为 `frames * 1000 / 60 / 20 = frames * 5/6`；将它作为待核对行为，不在未取证时直接判定正确或错误。
7. 有官方 PC 播放器/SDK 参考时，用相同资源及调用序列采集变量轨迹；没有时，将 Aether 公共实现作为候选参考，并明确未验证项。缺少真值不妨碍确定性状态修复，但影响曲线/type 改动的发布范围。

产出：时间/flags/帧语义契约表、基线 CSV、问题与触发条件列表、许可路径记录。无需启动完整 AetherKiri/Godot 才能做独立动画实验；若复用上游测试夹具，应抽出依赖并按许可保留来源。

完成条件：能够用记录区分“动画顿挫”和“渲染顿挫”，并为后续每个行为变更指定参考值或标记不确定性。

### M1：独立状态与时间轴接入

- 每个 player 创建独立的 timeline 状态和变量状态；同一资源在不同实例之间共享定义，不能共享可变控制状态。
- `playTimeline` 初始化该 timeline；重复播放、未知标签、并行/替换、停止单条和停止全部分别定义行为。删除 `-10000` 起点和全局 `currStartTick` 依赖。
- 首次播放按确认的契约处理从 0 到 `loopBegin` 的起始段；只在跨过循环边界时回绕。正确处理一次跨越多个周期、零长/无效循环、终止帧及停止后的最后姿态。
- 分离主 motion 的结束状态与控制 timeline 的可推进状态。保持游戏等待 `.playing/.animating` 的既有流程可结束，同时避免主 motion 结束后冻结仍活动的控制动画。显式停止、暂停和后台恢复分别处理。
- `progress` 推进动画，draw/命中查询只消费结果。一个 progress 后多次 draw 不得多推进；不引入第二个 continuous callback 重复推进。
- 保持临时开发模式 `legacy/integrated` 可选择（名称待实现），在创建会话时固定。模式用于 A/B 与回退，不增加面向玩家的设置；运行中不切换状态模型。

完成条件：短/长 timeline 同时播放互不重置；起播、循环、非循环终止与查询通过生产核心测试；多个实例共享资源时变量互不干扰。此阶段以时间和状态正确为目标，不宣称已经复现全部官方插值。

### M2：变量动画器、队列与混合

- 在核心中实现零时长立即赋值，以及有时长的当前值→目标值过渡。接入已确认的 easing 权重和队列/替换语义。
- Aether 候选权重为：easing=0 时 `w=1`，正值 `w=1+easing`，负值 `w=1/(1-easing)`；过渡比例为 `p^w`。确认 easing 的所属帧和时间窗口后再启用。
- 按原始关键帧调度过渡，正确处理一帧偏移、跨越多个关键帧、循环 seek 后旧队列清理、type 0 与 instant variable。不能用“当前帧到下一帧”的旧 lerp 伪装上游调度。
- 明确变量求值顺序：基础/脚本控制值、主 timeline、selector/instant 规则、差分加权贡献及已有范围限制。顺序由 M0 和参考轨迹确定，禁止依赖 unordered_map 的遍历顺序。
- 混合值保持为独立状态。淡出到 0 后按契约自动停止，淡入期间查询反映实际状态；循环接缝不能把刚设置的 blend ratio 重置为 0。
- 修改普通 Emote 的 `setVariable` 可选参数绑定，同时让 D3D 包装层完整转发 `time/accel`。保留旧的两参数调用和 native 内部调用。
- 适配我们现有 `setTimelineBlendRatio(name, ratio, time, easing)`、`fadeIn/OutTimeline` 接口。上游某些公开签名只有 name/ratio 或 duration/flags，并非所有过渡参数都已实现，不能直接替换 TJS 注册。

完成条件：变量轨迹、过渡中断、队列、差分叠加、淡入淡出和两类脚本入口通过测试；无过渡的旧调用保持兼容；上游参照与实际实现的差异均有记录。

### M3：节点 frame type 与曲线

- 将固定四点曲线扩展为按真实格式保存的多段数组，保留旧缺省值。解析缺失、长度不匹配、非法控制点、同时间关键帧时应得到确定行为，不产生 NaN/越界。
- 按通道确认 `ccc/acc/zcc/scc/occ/mesh.cc/cp` 是否存在及其用途；只接入实际解析且语义验证通过的通道。对坐标、旋转、缩放、透明度、颜色与网格分别验证。
- 区别节点帧与 timeline 变量帧。Aether 节点 `0/2/3` 规则是候选，先用真实帧序列和参考输出确认，再替换当前 type 条件。
- 对无曲线数据的通道保留已确认的线性/保持行为；不能强制所有轨迹一阶连续，也不能为了日志归零抹平作者设计的跳变。
- 只有新规则覆盖了对应终止场景，才移除现有 `2→0`、`3→0`、selfSyncTime 等拦截；先保留有回归依据的兼容行为。
- 网格只调整控制点的时间插值。继续走现有 GPU deform/CPU 网格回退，不在此阶段更换细分、蒙版、Z 排序、坐标投影和命中测试算法。

完成条件：type 与曲线案例有来源和参考输出，端点与必要的中间值正确；渲染、口型/眨眼、遮罩与触摸区域没有新增回归。未验证通道继续列为未完成，不能合并成“曲线全部支持”。

### M4：状态兼容、构建与真机验收

- 新状态快照包含 timeline 时间/flags/playing、变量基础值、动画器当前/目标/队列、blend、模式与时间转换标识。派生网格、GPU 句柄、资源指针和诊断缓冲不写入存档。
- 向现有 `serialize/unserialize` 增加带版本的可选状态字段；读取旧存档缺少字段时采用兼容默认值。快照结构在 M1 起设计，每个阶段增量覆盖，而非最后临时补上。
- 更新 D3D clone：复制值状态后与原实例独立推进。当前 clone 的“恢复后再逐个 setVariable”可能覆盖新动画器，需改成明确的完整状态复制流程。
- 验证 motion/角色切换、load/unload、清缓存、会话退出、暂停恢复和向前/向后 seek。更新后的游标/队列不能把旧角色的状态带入新资源。
- 完成 device 与 Apple Silicon simulator 的 framework 构建，并做 Metal 与软件回退检查。默认实现的切换在真机验收后单独提交。

完成条件：测试矩阵通过、同资源真机对比完成、已知限制记录清楚，可以明确判断本次迁移改善了哪些症状。

## 5. 验证矩阵与量化方式

### 自动化

新增 `Tests/EmoteAnimation` 直接编译生产核心；期望值来自独立手算、行为契约或确认的参考轨迹，不把同一套迁移代码复制为测试真值。

| 场景 | 必须验证 |
| --- | --- |
| 时间推进 | fractional dt、0 dt、不同步长、较大 dt；最终轨迹及跨越边界行为 |
| 多 timeline | 不同周期、错开起播、停止一条、重复播放、同帧多个事件 |
| 循环 | 非零 loopBegin、起始段、多周期越界、退化循环、合法数据自身跳变 |
| 非循环 | 首值、末值、终止帧、playing 查询、最终值保持与等待流程结束 |
| 变量 | easing 正/负/零、中断、队列/替换、instant/selector、派生参数 |
| 混合 | 主+diff、多 diff、权重变化、淡出停止、循环时 blend 保持 |
| 节点 | 0/2/3 转换、无曲线、单段/多段、非法曲线、角度边界、mesh 缺省 |
| 生命周期 | 同资源双实例、clone 后独立推进、快照往返、旧快照、seek、卸载清理 |
| TJS 接口 | 原生注册执行真实可选参数调用；普通/D3D 单位与转发契约 |

核心轨迹比较采用声明单位后的绝对+相对误差；阈值依据字段精度及参考日志精度确定。浮点变量、量化颜色、可见性和离散 selector 分别比较，不能共用一个模糊阈值。线性且无有意行为变更的场景应与旧实现一致。

M3 同时扩展 `Tests/EmoteMetadata` 验证生产 PSB 解析。既有缓存、TJS 生命周期、LayerInput、MetalLayer、MetalRenderBackend 测试继续作为接入检查；测试结果只代表实际执行的平台与路径。

### 离线与真机

同一 PSB、相同调用序列分别输出 legacy、integrated 与可获得的官方参考 CSV；记录每帧变量、timeline、边界、flags 与过渡事件。参考轨迹不存在时明确注明，不能称为“与官方一致”。

优先覆盖：出问题的单人大动作、循环呼吸/等待、表情/动作切换、口型眨眼、clone 转场及读档。另用至少一个不同资源验证通用性；双人场景用于兼容和性能回归，不把其 GPU 优化作为本次交付条件。

每个重点场景预热后重复采集，静置循环至少 30 秒，操作场景完成相同动作序列。记录设备、OS、构建、实际后端、热状态和低电量模式。

比较三类结果：

1. **动画正确性**：无意外跨 timeline 重置、起点越界或 clone 串扰；非有意跳变与参考轨迹相符。
2. **显示节奏**：progress 间隔、姿态更新次数、重复姿态/画面及帧间隔分布。连续相同值可能是合法保持帧，需结合数据判断。
3. **性能成本**：动画求值及整帧 wall time 的分布、分配/内存、现有 GPU 等待和提交/回读计数。测量新增动画模块与整条链路，不以平均 FPS 一项决定验收。

M0 建立数值预算，验收要求在相同环境下无超出该预算的新增成本；不能在结果出来后临时放宽阈值。诊断事件按意外重置、脚本瞬变、合法保持、数据跳变等分类，不要求所有关键帧的速度差分都为零。

## 6. 提交、回退与交付顺序

建议每个提交只引入一个可验证行为：

1. 诊断、契约记录和合成夹具。
2. 纯动画核心与独立 timeline/player 状态接入。
3. 变量动画器、主/diff 求值与 TJS/D3D 参数适配。
4. 节点 type 与各通道曲线（可拆成多次提交）。
5. 状态快照、clone/seek/生命周期的最终兼容检查。
6. 真机结果记录与默认模式切换。

核心文件在嵌套 `cpp` 子模块内。准备可复现交付时先形成 core 提交，再更新 build fork 的 core pin/构建，再更新 app 的 build fork pin；测试与计划按所属仓库提交。推送、发布与默认分支合并不属于本次“写计划”的操作。

开发阶段保留会话级 A/B 模式和基线轨迹。候选模式失败时停止扩大范围；不要在同一动画中途自动切到 legacy。默认切换后保留明确的回退提交，避免长期维护两套完整播放器。

## 7. 最小首轮交付与待准备条件

首轮实施以 **M0→M1→M2** 为范围：独立时间轴、变量过渡、必要的差分/混合接口和可重复对比。M3 依据真实 PSB 的曲线/type 触发情况推进；M4 的状态测试随各阶段加入，真机验收作为默认切换的最后条件。

需要准备的材料：

- 一个能稳定复现顿挫的场景及可合法本地使用的 PSB/调用记录。
- 如可获得，官方 PC 播放器相同资源的变量轨迹。
- 许可及分发路径结论。
- Apple 构建环境与目标 iPhone；当前 Windows 环境可做可移植核心/解析测试，不能完成原生 Metal 和真机验收。

材料尚未齐全时可先推进源码契约、独立核心、合成夹具与诊断；实际资源相关的语义和效果保持未验证状态，不用估测冒充验收。

## 8. 参考索引

上游链接均固定到评估提交：

- [Timeline 与动画器状态定义](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/RuntimeSupport.h#L69)
- [Timeline easing 转换和 PSB 控制解析](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/RuntimeSupport.cpp#L376)
- [播放 flags、变量接口与混合](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/PlayerQuery.cpp#L1332)
- [变量动画器、差分求值与边界推进](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/PlayerRender.cpp#L13118)
- [节点解析、曲线与插值](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/PlayerInternal.h#L1677)
- [普通 Emote 与 D3D 时间入口](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/EmotePlayer.cpp#L903)
- [扩展后端契约](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/MotionPlayerExtension.h#L132)
- [上游测试及其实际覆盖范围](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/tests/plugins/motionplayer-dll.cpp#L2283)

本地关联文档：

- [现有 Emote 实现说明](../Engine/KRKRRuntime/Source/docs/emoteplayer-analysis.md)
- [Metal 后端与子模块交付约束](METAL-RENDER-BACKEND.md)
- [KRKR 接入与真机验收](KRKR-INTEGRATION.md)
- [现有 PSB 解析/加载测试](../Tests/EmoteMetadata/README.md)
