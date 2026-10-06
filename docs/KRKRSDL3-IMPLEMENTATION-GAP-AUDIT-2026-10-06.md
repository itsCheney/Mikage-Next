# Mikage KRKRSDL3 实现缺口审计

日期：2026-10-06。范围：`core/`、`environ/`、`plugins/`，补查构建配置、TJS 注册、调用方与 Metal 后端。只读审计，未修改运行时代码。

## 结论与阅读方式

**确实还有与 TextRender 同性质的缺口。** 既有“方法被注册、调用成功，但不产生要求的效果”，也有“软件实现存在，GPU/平台分支没有等价实现”。后者可能表现为图像错误，也可能只是正确但昂贵的 CPU fallback，不能混为一谈。

本报告将结果分为：

- **兼容缺口**：文本、绘图、输入、音频、视频行为缺失或错误。
- **性能缺口**：明确进入软件/同步回读的路径；标明触发条件，不将静态风险当成已测得的帧率或发热原因。
- **共同限制**：Aether/Kirikiroid2 同样未实现，不能声称迁移时漏掉了一个现成修复。
- **桌面/非 iOS 功能**：不应直接列入 iOS 游戏故障清单，但必要时仍需提供稳定的脚本兼容返回值。

优先检查 TextRender 的时序/布局、九宫格 GPU 分支、音频播放游标/声像、脚本合成 key-up；性能工作优先检查透视及非 Copy 三角形绘制。详细证据见后文。优先级表示建议处理顺序，不代表已在每个游戏上复现。

| 类别 | 最明确的例子 | Aether / Kirikiroid2 对照结论 |
|---|---|---|
| API/模块存在，实际未实现 | T2 expat 空注册；T3 PSB.load(octet)；T5 Emote 控制；E1 声像 | A 有对应解析/状态/后端路径；部分插件在 K 公开源码中无对应实现 |
| 无条件常量或忽略参数 | T1 全字符显示/空等待；T0 option/fontScale；E2 恒零延迟 | A 的 TextRender 和音频统计更完整，K 音频有实现但仍有分支限制 |
| 有代码但不可达/缩水 | T4 PSB v4 被前置拒绝；C2 合成 key-up；E3 音频游标 | A 支持 v4；K 的 key-up 实际入事件队列；两者有真实音频进度算法 |
| 非软件分支未完成 | C1 copy9Patch 中心/其他分区缺失、未初始化读取 | 三方共同继承，不能直接套用 A/K GPU 分支 |
| 性能路径缺口 | P1 透视全软件；P2 非受限 Copy 三角形软件；P3 整图同步回读 | A/K 的 GL 有相应 GPU 绘制，但它们的 CPU 像素访问也会读回 |
| 三方/跨后端共同缺口 | C3 frequency、C4 MIDI、T9 FFT、E5 视频控制 | 有些需要重新实现，参考仓库不是完整修复包 |
| iOS 通常无需复刻 | C9 桌面窗口/遮罩；C10 外部进程/诊断；windowEx 任务栏/DPI | 与游戏内文本、XML、音频等语义缺口分开处理 |

**对旧对话需要修正的一点：Aether 的 TextRender ruby/eval 也只是消费语法，没有完成注音绘制/表达式求值（T12）。** 它的 timing/newline 确实更完整，但不能据此称整个 TextRender 已完全实现。

## 版本、范围与证据边界

| 标识 | 仓库/目录 | 审计提交 |
|---|---|---|
| M | Mikage 主仓库 `D:/vn-sim` | `5e9c575c037fc701bcc3de6e1f08b93a9947e688` |
| B | `Engine/KRKRRuntime/Source` 构建仓库 | `add9561548c834535bbd131b182879769342257e` |
| M 源码 | `Engine/KRKRRuntime/Source/cpp`，itsCheney/krkrsdl3 | `1ee1e6de3a033e316e90882a5654a9278901a2d5` |
| A | `third_party/AetherKrkr`，AetherKiri/AetherKrkr | `fa0f8af9614865aebcb839abd32bc181643d1e4a` |
| K | `build/kirikiroid-audit-reference`，zeas2/Kirikiroid2 | `d1c2b1259423542c893e0b65eaeb46c848848f2b` |

审计开始时 M/B/cpp 工作区干净。A 的本地提交与本次 GitHub `commits/main` 返回一致；K 是本次从公开仓库取得的浅克隆。报告中的 K 专指这个公开版本，不代表闭源发行版、补丁包或其他 fork。已有 `build/kirikiroid-reference` 只有渲染参考文件，本次没有把它当成完整仓库。

可复查版本：[M 源码](https://github.com/itsCheney/krkrsdl3/tree/1ee1e6de3a033e316e90882a5654a9278901a2d5)、[A](https://github.com/AetherKiri/AetherKrkr/tree/fa0f8af9614865aebcb839abd32bc181643d1e4a)、[K](https://github.com/zeas2/Kirikiroid2/tree/d1c2b1259423542c893e0b65eaeb46c848848f2b)。下文简写路径均相对于各源码根；K 路径通常带 `src/`。

检索覆盖空函数、无条件常量返回、TODO/FIXME/stub/not implemented，以及对应模块的实现和注册差异；逐项追踪重要候选的实际分支。一次全目录标记扫描覆盖 519 个 C/C++/Objective-C++ 源码/头文件，得到 156 个命中行、52 个命中文件；**这些是候选检索计数，不是缺陷计数**，其中还包含第三方库注释。另行进行了无 TODO 的字段赋值、空实现及调用链搜索。

这是广域静态审计，不是形式化完整性证明，也没有重新编译 iOS、运行真机或重放用户之前上传的游戏脚本。当前会话的旧聊天预览仅用作线索；其中“所有 API 差异”“某游戏必崩”等结论不直接继承。源码能够证明空实现和路径差异；游戏是否调用、触发频率及耗时仍需最小 TJS 用例或真机诊断。


插件对比说明：K 公共 checkout 未发现 TextRender、LayerExDraw、psbfile、EmotePlayer、windowEx、expat 的对应插件实现。后文的 **K-无公开对应实现** 指本次完整文件检索结果，并结合 `src/plugins/Android.mk:7-10` 与 `src/plugins/InternalPlugins.cpp:4-8` 核对公开构建/注册方式；不推断商业发行版也缺少这些功能。M 的 `B/cmake/cmake_plugins.txt:9,32,46-48,63-67` 将下列主要候选纳入生产源清单。

## 插件：接口存在但行为缺失

### T0. 重新核验原 TextRender 问题：newline 缺注册，option/fontScale 也是空语义

M `plugins/textrender.cpp:340-352,1350-1361` 的类声明和注册清单均没有 newline；A `plugins/textrender.cpp:1578-1581` 实现 flush 后换行，`:1705` 注册。因此当前源码仍存在该 API 缺口；这是本轮源码核验，不依赖旧聊天的仓库快照。

更容易被编译成功掩盖的是：M `TextRenderOptions::deserialize:205-212` 只检查对象是否为空，不读取任何 option；`setOption:1222-1224` 正好委托它。M `fontScale:361` 虽可存取，但 `updateFont:1303-1315` 用未缩放字号，fontscale 的全文件引用仅为声明、序列化/反序列化和属性。`render:511` 收到 autoIndent/diff/all/same，却没有把参数接入相应状态；m_autoIndent 保持初始化值。`CharacterInfo:222-265` 的序列化也没有 delay 字段。

A `TextRenderOptions::deserialize:250` 起实际读取禁则/计时选项，`:982` 的 scale 参与字号处理，时序实现见 T1；K 无公开对应实现。调用这些接口的文本布局、缩放、双语分轨及逐字显示都需要针对性验证。不能只补 newline 就称完整兼容。这里不沿用旧聊天未经重新逐项核验的 API 数量、所有 ruby/eval 完整性或具体游戏行为结论。

证据：[M TextRender 声明](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/textrender.cpp#L340)、[M option](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/textrender.cpp#L205)、[A newline](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/textrender.cpp#L1578)。

### T1. TextRender 时间控制被解析后丢弃，显示计数缺少时间参数

- **M 证据**：[M/plugins/textrender.cpp:677-695](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/textrender.cpp#L677) 的 `%d`（文本速度）和 `%w`（等待）只读取整数；`:698-731` 的 `%D` 同步时间/标签只读取参数；`:792-793` 的 `\\k` 分支不记录等待。`:1332-1338` 的 `getKeyWait()` 总是返回新空数组，`:1340-1343` 的 `calcShowCount()` 无参数、返回全部 `m_characters.size()`。`:111` 的 `renderDelay` 初始为 1000，`:373` 将其作为普通存储属性暴露；`:1357-1365` 注册这些方法/属性。
- **A 证据**：[A/plugins/textrender.cpp:1160-1213](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/textrender.cpp#L1160) 更新 character delay、增加 wait、同步 timing；`:1253-1261` 记录 key wait；`:1599-1619` 返回包含 `pos/time` 的真实等待数组；`:1622-1630` 的 `calcShowCount(int elapsed)` 根据字符 delay 和 elapsed 计算。`:1698`、`:1707-1708`、`:1763` 注册相关接口。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：游戏通过 `TextRenderBase.render()` 输入这些控制符后调用 `getKeyWait()` 或 `calcShowCount(elapsed)` 来执行逐字显示/点击等待，本地无法从该对象获得正确的时间序列。`calcShowCount` 原生签名已缩水；不能仅凭空函数编译成功认定文字控制兼容。**高**。这是播放节奏正确性问题，不直接证明低 FPS。

### T2. expat.dll 可以登记加载，但没有 XMLParser 类

- **M 证据**：[M/plugins/expat.cpp:3-17](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/expat.cpp#L3) 有模块名、空 init/done 和注册回调。全量 `M/plugins` / `M/core` 检索没有 `XMLParser` 实现。
- **A 证据**：[A/plugins/expat.cpp:184-221](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/expat.cpp#L184) 实现 Expat parser 与 `XML_Parse`；`:301-340` 注册 `XMLParser.parse/parseStorage`；`:424-441` 初始化时将 `XMLParser` 加入全局对象。
- **K 证据**：K-无公开对应实现（虽 [K/src/plugins/Android.mk:36](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/plugins/Android.mk#L36) 有 Expat include 目录，不能据此当成 XMLParser 插件）。
- **实际行为与触发条件**：脚本 `Plugins.link("expat.dll")` 后创建 `new XMLParser()`，模块回调成功不产生 XMLParser，脚本仍会缺成员/类。**高**，属于依赖 XML 数据的游戏兼容性缺口。

### T3. PSBFile.load(octet) 接受参数却不解析

- **M 证据**：[M/plugins/psbfile/PSBFile.cpp:542-571](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/psbfile/PSBFile.cpp#L542) 注册 `load`；`:564-567` 对 `tvtOctet` 只写日志；`:570` 随后返回 `TJS_S_OK`。反例：`:515-531` 的构造器 octet 分支已有 `loadFromStream()`，说明不是整个解析器缺失。
- **A 证据**：[A/plugins/psbfile/main.cpp:488-540](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/psbfile/main.cpp#L488) 的 load callback，`:516-523` 调用 `loadPSBData(octet->GetData(), octet->GetLength(), ...)`；`:629-633` 注册 PSBFile/load。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：游戏从解密回调或其他存储得到 octet，再调用现有对象 `.load(data)`。本地不会更新 root，并且没有为 result 设置 false；调用表面返回成功状态。构造器 `new PSBFile(data)` 能走另一条真实路径，不能把二者等同。**高**。

### T4. PSB v4 分支存在，但在更早的位置被拒绝

- **M 证据**：[M/plugins/psbfile/PSBFile.cpp:112-115](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/psbfile/PSBFile.cpp#L112) 对 version > 3 返回 false；`:200-218` 已有 version >= 4 的 extra chunk 读取代码，却被上述前置条件阻断。`:542-571` 是脚本 load 入口。
- **A 证据**：[A/plugins/psbfile/PSBFile.cpp:761-770](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/psbfile/PSBFile.cpp#L761) 处理 v4 extra chunk 字段；`:798-801` 仅拒绝 version > 4；`:488-540` 的 `A/plugins/psbfile/main.cpp` 连接脚本 load。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：调用 PSBFile 插件加载 header.version=4 文件必定失败；这是直接的支持范围缩水，不能看到后面的 v4 代码就宣称支持。**高**。Emote 自身 `emotefile` 是独立加载路径，本条不推断所有 v4 Emote 动画都失败。

### T5. Emote 剩余颜色、物理控制和风力接口为空

- **M 证据**：[M/plugins/emoteplayer/emoteplayerclass.cpp:1062-1065](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayerclass.cpp#L1062) 的 initPhysics、`:1373-1376` 的 setOuterForce、`:1392-1400` 的 startWind/stopWind 都仅日志。[M/plugins/emoteplayer/emoteplayer.cpp:51](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayer.cpp#L51)、`:63-67` 和 `:107`、`:119-123` 为两套脚本 facade 注册入口。[M/plugins/emoteplayer/emoteplayerclass.h:227-228](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayerclass.h#L227) 的 outline getter 返回 void variant、setter 空；[M/plugins/emoteplayer/emoteplayer.cpp:92](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayer.cpp#L92)、`:155` 暴露 outline。
- **A 证据**：[A/plugins/motionplayer/EmotePlayer.cpp:314-315](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/EmotePlayer.cpp#L314)、`:541-568`、`:948-950` 委托 Player。[A/plugins/motionplayer/PlayerCore.cpp:2518-2564](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/PlayerCore.cpp#L2518) 实际更新 wind state 并调用 backend；`:2568-2571` 清理 wind state；`:2577` 开始按 label 设置 outer force。`:1521` 为 initPhysics 实现。[A/plugins/motionplayer/Player.h:111-112](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/Player.h#L111) 至少保存/读取 outline，[A/plugins/motionplayer/main.cpp:151](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/main.cpp#L151)、`:348`、`:368-369`、`:398` 注册。outline 存储本身不证明视觉描边完成。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：脚本要求风、外力或物理初始化时，调用完成却没有改变这些状态；outline 赋值也不保存。**高**（空实现）；A 的最终原生 backend 是否存在/实际视觉物理效果属于另一级验证，本报告只确认它有状态与调用路径。
- **颜色也为空**：M [M/plugins/emoteplayer/emoteplayerclass.cpp:1333-1336](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayerclass.cpp#L1333) 的 setColor 仅日志，[M/plugins/emoteplayer/emoteplayer.cpp:59](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayer.cpp#L59)、`:115` 注册；A [A/plugins/motionplayer/EmotePlayer.cpp:454-458](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/EmotePlayer.cpp#L454) 保存颜色并调用 Player.setEmoteColor，[A/plugins/motionplayer/PlayerCore.cpp:2429](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/PlayerCore.cpp#L2429) 开始实际颜色状态/过渡逻辑，[A/plugins/motionplayer/main.cpp:358](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/main.cpp#L358)、`:630` 注册。K 仍为 K-无公开对应实现。游戏调用角色 tint/颜色变化时，本地没有效果，证据**高**。A [A/plugins/motionplayer/EmotePlayer.cpp:480](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/motionplayer/EmotePlayer.cpp#L480) 的 getColor 反而恒 0，因此也不能把 A 所有颜色查询均当成完整实现。
- **已实现反例**：[M/plugins/emoteplayer/emoteplayerclass.cpp:1098-1131](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayerclass.cpp#L1098) 的 progress 具有 integratedAnimation 时钟推进；[M/plugins/emoteplayer/emoteplayerclass.h:210-220](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoteplayerclass.h#L210) 的 tickCount seek、`:232-238` serialize/play 声明对应真实实现；[M/plugins/emoteplayer/emoterunner.cpp:1204-1229](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/emoteplayer/emoterunner.cpp#L1204) 有 animation 状态序列化/恢复。**不能把 Emote 整体或 timeline 动画误报为 stub。**

### T6. LayerExDraw.loadRecord 恒 false 且不读取文件

- **M 证据**：[M/plugins/LayerExDraw/LayerExDraw.cpp:2083-2086](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/LayerExDraw/LayerExDraw.cpp#L2083) 完全忽略 filename；`:2853` 注册 loadRecord。`:2070-2080` 的 saveRecord 已有 PNG 写出，因此这是读取端缺失。
- **A 证据**：[A/plugins/layerex_draw/blend2d/LayerExDraw.cpp:2536-2545](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/blend2d/LayerExDraw.cpp#L2536) 和 [A/plugins/layerex_draw/general/LayerExDraw.cpp:2741-2750](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/general/LayerExDraw.cpp#L2741) 虽最终仍返回 false，却已经加载图像、createRecord、redraw；blend2d 注册在 `:3790`，general 注册在 [A/plugins/layerex_draw/general/main.cpp:912](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/general/main.cpp#L912)。A backend 选择见 [A/plugins/layerex_draw/CMakeLists.txt:13-31](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/CMakeLists.txt#L13)。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：脚本尝试恢复已保存的记录文件，本地既不读也不绘制。**高**。A 返回值也不正确，是共同问题；不能把 A 的返回 false 误判成和本地一样无效果。

### 插件共同限制与反例

### T7. LayerExDraw.drawString 返回空边界，且忽略 pen 绘制项

- **M 证据**：[M/plugins/LayerExDraw/LayerExDraw.cpp:1793](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/LayerExDraw/LayerExDraw.cpp#L1793) 默认 bounds，`:1812-1825` 只处理 `drawInfo.type == 1`，`:1830-1831` 原样返回 bounds；RectF 默认字段为 0，见 [M/plugins/LayerExDraw/LayerExDraw.hpp:286-287](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/LayerExDraw/LayerExDraw.hpp#L286)。`:2840` 注册 drawString。真实 brush fillText 在 `:1822`，所以不是整个文字绘制为空。
- **A 证据**：[A/plugins/layerex_draw/blend2d/LayerExDraw.cpp:2162](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/blend2d/LayerExDraw.cpp#L2162)、`:2178-2205`、`:2211-2212` 也有同样空 bounds/只 brush 行为；`:2159-2160` 另有 SelfPathDraw 路径。general 的 [A/plugins/layerex_draw/general/LayerExDraw.cpp:2152-2153](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/layerex_draw/general/LayerExDraw.cpp#L2152) 同样先选择 self path，不能跨 backend 笼统声称 A 所有 drawString 更完整。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：脚本用 drawString 的 RectF 参与排版/命中范围，得到零面积；Appearance 只有 pen 时该分支不绘字。**高**，共同兼容性限制。
- **不要扩大 TODO 结论**：M `:1846-1872` 的 drawPathString2/measureString2/getGlyphOutline/getTextOutline 虽为空，但 grep 未找到本地调用，注册清单 `:2839-2842` 只暴露没有 2 后缀的方法。本地 drawPathString `:1760-1787` 已用 plutovg glyph path 绘字；这些孤立 helper TODO 不单独计作游戏可达 bug。M `forceSelfPathDraw` 属性（`:2599`）可存取，但 getSelfPathDraw 无实际调用，可作为模式选择语义缺口候选，优先级低于上面的直接证据。

### T8. PSB 虚拟存储目录枚举为空；物理本地路径不保证可用

- **M 证据**：[M/plugins/psbfile/PSBMedia.cpp:69-73](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/psbfile/PSBMedia.cpp#L69) 的 GetListAt 没有 lister.Add；`:75-79` 的 GetLocallyAccessibleName 不转换 name。[M/plugins/psbfile/PSB.cpp:25](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/psbfile/PSB.cpp#L25) 注册 storage media。[M/core/archive/TVPStorage.cpp:490-505](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/archive/TVPStorage.cpp#L490) 直接分派给该 media；`:927` 使用列表。
- **A 证据**：[A/plugins/psbfile/PSBMedia.cpp:1814-1816](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/psbfile/PSBMedia.cpp#L1814) 的 GetListAt 同样只有 TODO log；`:1818-1821` 的本地路径函数明确抛出“不支持非本地媒体”的异常；[A/plugins/psbfile/main.cpp:201](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/psbfile/main.cpp#L201) 注册 media。
- **K 证据**：K-无公开对应实现。
- **实际行为与触发条件**：脚本使用 PSB 虚拟目录枚举而非已知资源名直读，M/A 都没有列表。读取本地路径供只能接受文件路径的库使用时，M 保留虚拟名称而不是导出文件；A 会明确拒绝。**高**。虚拟媒体本来就不必支持物理路径，因此“未导出到本地”不单独认定为游戏必须修复的 bug。

### T9. FFT 可视化插件三方都是空壳

- **M 证据**：[M/plugins/fftgraph.cpp:3-10](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/fftgraph.cpp#L3) 注册空脚本函数 drawFFTGraph。
- **A 证据**：[A/plugins/fftgraph.cpp:3-9](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/fftgraph.cpp#L3) 一样。
- **K 证据**：[K/src/plugins/fftgraph.cpp:3-10](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/plugins/fftgraph.cpp#L3) 一样。
- **实际行为与触发条件**：脚本调用 drawFFTGraph，函数存在但没有图形或数值效果。**高**，三方共同的视觉兼容性限制，不是本地相比两方的独有缺陷。

### T10. Plugins.link/unlink 语义缩水；不意味着 iOS 应加载 Windows DLL

- **M 证据**：[M/plugins/TVPPlugin.cpp:39-48](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/TVPPlugin.cpp#L39) link 丢弃内部模块加载结果，unlink 恒 true；[M/core/script/tjsNativePlugins.cpp:20-43](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativePlugins.cpp#L20) 为脚本入口。[M/plugins/ncbind/ncbind.cpp:54-76](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/ncbind/ncbind.cpp#L54) 已有真正的静态 UnloadModule，但 TVPUnloadPlugin 没调用它。
- **A 证据**：[A/core/plugin/PluginImpl.cpp:638-690](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/plugin/PluginImpl.cpp#L638) 报告加载成功/失败并有别名/特定 fallback；`:694-704` 真正 UnloadModule，另处理代理文件系统。
- **K 证据**：[K/src/core/base/win32/PluginImpl.cpp:449-452](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/base/win32/PluginImpl.cpp#L449) 同样丢弃加载结果；`:486-509` 恒 true；[K/src/plugins/InternalPlugins.cpp:19-22](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/plugins/InternalPlugins.cpp#L19) 使用静态模块加载。
- **实际行为与触发条件**：脚本卸载后期望移除已注册的类/回调，本地仅返回 true；请求不存在模块也无直接失败信号，之后访问 API 才暴露问题。**高**，M 与 K 的共同限制。iOS 通过静态编入插件满足兼容是合理平台设计；Windows PE DLL 动态加载本身不列为 iOS 缺陷。全局退出 [M/plugins/TVPPlugin.cpp:111-113](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/TVPPlugin.cpp#L111) 仍执行统一卸载，不能据单模块 unlink 空实现直接推断跨游戏资源泄漏。

### T11. WIN32Dialog 包装真实移动端 UI，但参数/返回值缩水

- **M 证据**：[M/plugins/win32dialog.cpp:7-11](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/win32dialog.cpp#L7) 的 messageBox 忽略 type，总是传 `System.inform(...,2)`，只返回逻辑否定。
- **A 证据**：[A/plugins/win32dialog.cpp:7-24](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/win32dialog.cpp#L7) 解析三/四参数、区分 yes/no 并返回 IDYES/IDNO/IDOK/IDCANCEL；`:28-34` 提供相应常量。
- **K 证据**：[K/src/plugins/win32dialog.cpp:7-16](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/plugins/win32dialog.cpp#L7) 与 M 一样。
- **实际行为与触发条件**：游戏按 Win32 消息框 flags 或 ID 常量处理删除存档/退出确认时，包装函数的按钮类型和返回值语义可能不匹配。**高**（包装体）；实际 System.inform UI 呈现需设备验证。属于可跨 iOS 平台复现的脚本语义，不能因为名字含 Win32 就完全归为无关功能。

### T12. TextRender ruby/eval 也不是 A 的完整实现

另一个共同限制是 TextRender 的 ruby/eval：M [M/plugins/textrender.cpp:801-820](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/textrender.cpp#L801)、`:890-908` 不渲染 ruby、不求值 `$...;`；A [A/plugins/textrender.cpp:1267-1273](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/textrender.cpp#L1267)、`:1301-1308` 也只是消费语法，不产生 ruby/eval 结果；K 为 K-无公开对应实现。M ruby 循环的 `:817` 无条件 break 还使 `:818` 的字符串积累不可达，可能让剩余 ruby 文本进入普通解析，这是源码推断，需带括号文本用例确认。两方注册 render 见 M `:1346-1350`、A `:1698`。**共同未实现注音/求值为高证据；具体注音残留显示为中证据，未做游戏实测。**

### 插件中的桌面功能

| 功能 | M 证据 | A/K 对照 | 判断 |
|---|---|---|---|
| Windows 最大化/最小化查询 | [M/plugins/windowEx.cpp:558](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/windowEx.cpp#L558)、`:577` 固定 false，`:562-566`、`:581-585` 转发查询 | [A/plugins/windowEx.cpp:466](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/windowEx.cpp#L466) 恒 true、`:481` 恒 false；K-无公开对应实现 | 桌面窗口状态不适用于 iOS 的主游戏视图，通常低优先；若脚本据此跳过重要初始化，另作兼容分支排查 |
| 任务栏缩略图 / DPI awareness | [M/plugins/windowEx.cpp:1185](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/windowEx.cpp#L1185) 恒 true、`:1190` 恒 0；`:1230-1232` 注册 System 方法 | [A/plugins/windowEx.cpp:1601](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/windowEx.cpp#L1601) 的 setIconicPreview 也恒 true，`:1613` 注册；K-无公开对应实现 | iOS 无 Windows 任务栏和线程 DPI 上下文，合理降级，不计主要性能/游戏功能缺陷 |

### PSB 存储性能候选的边界

本次没有确认“某个空函数导致持续卡顿”或定量性能退化。大多数 stub 跳过工作，主要后果是缺效果/缺 API；不能把 TODO 数量当成性能瓶颈证据。

插件侧另一个需要测量的性能候选是 PSBMedia.Open：[M/plugins/psbfile/PSBMedia.cpp:57-64](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/plugins/psbfile/PSBMedia.cpp#L57) 每次重新打开原 PSB、分配 chunk 长度内存并完整 ReadBuffer；A [A/plugins/psbfile/PSBMedia.cpp:1497-1545](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/plugins/psbfile/PSBMedia.cpp#L1497) 有缓存查询，`:1572-1595` 复用 raw-to-BMP 转换缓存，但 `:1608-1612` 返回流仍复制数据。两方数据格式与转换路径不同，尚不足以判断谁更快。K 为 K-无公开对应实现。这是“需要按游戏读频率/字节数做 profiling”的候选，不是已证实的空实现性能 bug。

建议先用能调用这些 API 的游戏/小脚本验证这些高优先接口的输入、输出和失败方式，再决定补齐：TextRender timing/key wait、XMLParser、PSB octet load/v4、Emote wind/outerForce。A 源码只作为行为和完整性参照，需要结合本地接口适配，A 的每个实现并未在本次审计中经过 iOS 验证。

## core：脚本、图层与共享能力缺口

### C1. GPU `Layer.copy9Patch` 进入未完成分支，还在解析标记前读取未初始化 margin（优先级高；三方共同）

- [M/core/script/tjsNativeLayer.cpp:8732-8782](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeLayer.cpp#L8732) 注册脚本方法；8764 声明 `tTVPRect margin`，8765 调用；`core/render/ComplexRect.h:49` 默认构造函数为空，未初始化四个整数。`core/script/tjsNativeLayer.cpp:4913-4933` 将返回值直接写入 ImageModified，仅 true 时触发 Update。
- [M/core/render/LayerBitmap.cpp:634-706](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/LayerBitmap.cpp#L634)：32bpp、源 >=11x11、目标至少源尺寸减2，且 `!TVPGetRenderManager()->IsSoftware()` 时可达。653 进入 GPU 分支，661 等处先读 margin，而实际标记解析在后续软件分支才进行；702-703 中心块仅 `;`，706 无条件 false。底部/右侧及中心等九宫格复制不完整；即使早期分区发生写入，也向上报告失败、跳过正常 Update、脚本结果被 Clear。
- [A/core/visual/LayerIntf.cpp:15037-15082](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/LayerIntf.cpp#L15037) 同一注册，15068 局部 margin；`core/visual/LayerBitmapIntf.cpp:1175-1243` GPU 分支相同，1243 固定 false。
- [K/src/core/visual/LayerIntf.cpp:8000-8043](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/LayerIntf.cpp#L8000) 同一注册，8030 局部 margin；`src/core/visual/ComplexRect.h:52` 默认构造为空；`src/core/visual/LayerBitmapIntf.cpp:1139-1194` GPU 分支同样未完成、1194 固定 false。
- 触发：游戏界面通过 copy9Patch 拉伸按钮/消息框并使用 Metal/其他非软件后端。软件分支在下方实际解析标记和复制，因此不是所有 copy9Patch 都失败。属于继承的兼容缺口，不能据此声称 A/K 的 GPU 版已经正确实现，也不能把问题仅归因于本地 Metal。

### C2. 脚本合成 `postInputEvent("onKeyUp",...)` 本地吞掉（相对 K 的缩减；A 当前 host 也为空）

- [M/core/script/tjsNativeWindow.cpp:562-580](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWindow.cpp#L562) 注册并调用 PostInputEvent；`core/script/tjsNativeWindow.cpp:231-301` 解析 onKeyDown/onKeyUp/onKeyPress，其中284-286只给 onKeyDown 调用 PostKeyDown，onKeyUp 在校验 key/shift 后没有投递。返回 TJS_S_OK。
- [M/core/main/TVPWindow.cpp:144-159](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/main/TVPWindow.cpp#L144) 仅有 PostKeyDown/PostKeyPress；但417-429 已有真正 OnKeyUp，向脚本 owner 发送 onKeyUp 并转发 DrawDevice，表明缺口位于合成事件入口而非全体键盘抬起事件。
- [A/core/visual/impl/WindowImpl.cpp:1189-1241](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/impl/WindowImpl.cpp#L1189) 同方法对 onKeyUp 调用 Form->OnKeyUp；当前 `core/environ/stubs/ui_stubs.cpp:1487` HostWindowLayer::OnKeyUp 又为空，所以 A 的实际 host 路径也是不产生此事件。
- [K/src/core/visual/win32/WindowImpl.cpp:1252-1256](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/win32/WindowImpl.cpp#L1252) 调用 Form->OnKeyUp；`src/core/environ/cocos2d/MainScene.cpp:1209-1213` 调用 InternalKeyUp；1205 会 TVPPostInputEvent(new tTVPOnKeyUpInputEvent(...))，有实际抬起投递。
- 触发：游戏/插件用 postInputEvent 合成快捷键或虚拟键盘的释放，而不是宿主真实按键事件。可导致仅合成路径的按下/释放不成对。应优先补一个与 PostKeyDown 对称的队列入口；无需移植桌面窗体。

### C3. `WaveSoundBuffer.frequency` 改字段，未改变音频播放速率（三方共同）

- [M/core/script/tjsNativeWaveSoundBuffer.cpp:2694-2712](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWaveSoundBuffer.cpp#L2694) 注册 frequency getter/setter；2156-2160 的 SetFrequency 保存 Frequency，然后2148-2153 的 SetFrequencyToBuffer 内 SoundBuffer->SetFrequency 已注释，没有后端调用。
- [A/core/sound/WaveIntf.cpp:1542](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/sound/WaveIntf.cpp#L1542) 属性调用 SetFrequency；`core/sound/win32/WaveImpl.cpp:3511-3521` 也是存字段、后端调用注释。
- [K/src/core/sound/WaveIntf.cpp:1568](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/sound/WaveIntf.cpp#L1568) 调用 SetFrequency；`src/core/sound/win32/WaveImpl.cpp:3348-3360` 相同。
- 触发：游戏通过 frequency 实现快进、音调/速度变化。读回新值会成功，但该 API 路径没有改变实际播放速率。M 中1657/1675/1692/1694 又用 Frequency 估算 label 的时间与位置，所以调整字段还可能使 label 时间估算与实际音频不同。不能据此断言实际后端所有变速机制不可用；这里证明的是 frequency API 未将设置下发。

### C4. MIDI 类注册成功，但未启用宏时 open/play 成功空操作（三方共同，条件明确）

- [M/core/script/TVPScript.cpp:592](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/TVPScript.cpp#L592) 无条件注册 MIDISoundBuffer；`core/script/tjsNativeMIDISoundBuffer.cpp:1091-1117` open/play 仅在 TVP_ENABLE_MIDI 内调用 Open/Play，宏未定义时仍 TJS_S_OK。`core/script/tjsNativeMIDISoundBuffer.h:104-107` 禁用分支 SetVolume 空、GetVolume 固定0。
- [A/core/sound/MIDIIntf.cpp:62-86](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/sound/MIDIIntf.cpp#L62) 同条件和成功返回；[K/src/core/sound/MIDIIntf.cpp:66-91](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/sound/MIDIIntf.cpp#L66) 同样。
- 在 M Source 的可见 CMake、header、xcconfig、pbxproj 搜索未发现此宏的定义；这里只报告未定义宏的分支，不声称读取了所有实际 compiler invocation。
- 触发：游戏构建 MIDISoundBuffer 并用 MIDI 作为 BGM；类存在/方法成功不能作为 MIDI 支持的探测。属于文件音乐兼容缺口，有 iOS 相关性；修复需要可用合成器或明确的支持行为。

### C5. 专用触摸查询和 enableTouch 固定值（共享缺口；不代表触控模拟鼠标不可用）

- [M/core/script/tjsNativeWindow.cpp:1536-1598](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWindow.cpp#L1536) 注册 getTouchPoint；只在 index < GetTouchPointCount 时返回字典，否则 Clear（合法非负index在计数为0时均无结果）。1600-1628 getTouchVelocity false 时将输出清0；1729-1738 注册 touchPointCount，1757-1772 注册 enableTouch。
- [M/core/script/tjsNativeWindow.h:173-197](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWindow.h#L173) 转发至 TVPWindow；`core/main/TVPWindow.h:307-308` SetEnableTouch 空、GetEnableTouch false；317-323 的坐标、ID、计数全0、速度 false。阈值 setter/getter311之后确实存值，不能把所有 touch API 都说成空。
- [A/core/visual/impl/WindowImpl.cpp:2165-2253,2354-2364,2384-2400](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/impl/WindowImpl.cpp#L2165) 脚本入口转发；`core/environ/win32/TVPWindow.h:432-433,459-465` 同常量/空实现。
- [K/src/core/visual/win32/WindowImpl.cpp:2163-2245,2345-2359,2379-2398](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/win32/WindowImpl.cpp#L2163) 入口；`src/core/environ/win32/TVPWindow.h:422-423,438-444` 同空实现。
- 触发：游戏读取多点触控列表、点ID/速度或根据 enableTouch 判定专用触摸模式。属于潜在 iOS 游戏兼容缺口，但三方移动/host接口都继承此限制；常见点击剧情的鼠标事件路径不能由此判坏。

### C6. `ltEffect` / `ltFilter` 退化成 binder（三方共同；有实际语义缩减）

- [M/core/script/TVPScript.cpp:178](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/TVPScript.cpp#L178) 暴露ltEffect=6/ltFilter=7常量；`core/script/tjsNativeLayer.cpp:10652-10666` 注册 type；1616 起 SetType，其中1666-1678 设置 DisplayType=ltBinder、CanHaveImage=false、DeallocateImage。
- [M/core/script/tjsNativeLayer.cpp:7219-7226,7237-7241,7252-7258](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeLayer.cpp#L7219) binder 的子层绘制直接转发到父目标，目标类型继承父层，不存在所设置效果/滤镜的独立图像处理。
- [A/core/visual/LayerIntf.cpp:5073-5086](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/LayerIntf.cpp#L5073) 同处理（type setter17179）；[K/src/core/visual/LayerIntf.cpp:1513-1525](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/LayerIntf.cpp#L1513) 同处理（type setter9935）。
- 触发：脚本明确使用 ltEffect / ltFilter。这里只认定这两个类型没有独立 effect/filter 行为，不把所有使用 binder 的容器层或转场都判为缺失。

### 字体和桌面接口的条件性限制

### C7. `Font.doUserSelect` 总是返回0（三方共享的字体选择器缺口）

- [M/core/script/tjsNativeFont.cpp:465-485](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeFont.cpp#L465) 注册，校验4参数、读取 flags/caption/prompt/sample，477-478固定 ret=0，483成功返回，无UI。
- [A/core/visual/LayerIntf.cpp:18394-18419](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/LayerIntf.cpp#L18394) 真调用被#if0排除、else0；[K/src/core/visual/LayerIntf.cpp:11055-11078](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/LayerIntf.cpp#L11055) 相同。
- 触发：游戏设置菜单要求宿主字体选择对话框；表现总是取消/未选择。不是日常字体绘制缺失，也不要求为 iOS 实现原桌面对话框；需要的是有调用时的替代选择体验。

### C8. `Font.getList(flags)` 完全忽略 flags（三方共享的参数语义缩减）

- [M/core/script/tjsNativeFont.cpp:487-516](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeFont.cpp#L487) 注册并传flags；318-331独立Font分支直接TVPGetAllFontList；带Layer时最终 `core/render/LayerBitmap.cpp:2470-2478` 也全量枚举，flags从未读取。
- [A/core/visual/LayerIntf.cpp:18421-18448](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/LayerIntf.cpp#L18421) 方法；`core/visual/impl/LayerBitmapImpl.cpp:796-804` 同全量枚举。[K/src/core/visual/LayerIntf.cpp:11080-11108](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/LayerIntf.cpp#L11080) 方法；`src/core/visual/win32/LayerBitmapImpl.cpp:800-807` 同忽略flags。
- 触发：脚本以非0flags筛选字体；返回与0flags相同。此处不推断各位flag的具体含义，避免将实际平台枚举范围问题混在本条。

### C9. 窗口遮罩区域与桌面窗体属性为空/常量（三方共享，iOS低优先级）

- [M/core/script/tjsNativeWindow.cpp:441-450](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWindow.cpp#L441) setMaskRegion；326-336只检查draw device和主Layer存在，不按threshold创建区域；`core/main/TVPWindow.h:299` RemoveMaskRegion空。
- [A/core/visual/WindowIntf.cpp:1048-1056](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/WindowIntf.cpp#L1048) 注册；`core/visual/impl/WindowImpl.cpp:1803-1815` 实际Form->SetMaskRegion注释。[K/src/core/visual/WindowIntf.cpp:810-818](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/WindowIntf.cpp#L810) 注册；`src/core/visual/win32/WindowImpl.cpp:1823-1831` 同注释。
- [M/core/script/tjsNativeWindow.cpp:1048-1096,1220-1253](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWindow.cpp#L1048) left/top/focusable/borderStyle/stayOnTop注册；`core/main/TVPWindow.h:274-298,303-304` setter空、getter返回0/true/bsNone/false。[A/core/environ/win32/TVPWindow.h:392-419,427-428](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/environ/win32/TVPWindow.h#L392)，[K/src/core/environ/win32/TVPWindow.h:386-412,417-418](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/environ/win32/TVPWindow.h#L386) 相同。
- 触发：桌面异形窗口、全局置顶、桌面位置/焦点切换。iOS普通全屏视觉小说通常不需要这些桌面效果。不要仅因固定返回值存在就将合法的全屏/单窗口平台约束列作兼容阻断。

### C10. `System.system` / `dumpHeap` / `showVersion` 是共享的成功空行为

- [M/core/script/tjsNativeSystem.cpp:547-563](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeSystem.cpp#L547) system读取命令字符串、ret固定0，随后DeliverCompactEvent(MAX)清缓存，再成功返回；634-641 dumpHeap仅成功返回，656-663 showVersion仅成功返回。
- [A/core/base/impl/SystemImpl.cpp:1116-1137,1262-1268,1284-1290](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/base/impl/SystemImpl.cpp#L1116) 相同。
- [K/src/core/base/win32/SystemImpl.cpp:919-937,999-1005,1021-1027](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/base/win32/SystemImpl.cpp#L919) 相同。
- 触发：游戏启动外部工具、生成外部文件，或依赖返回码；命令未执行却读到0。桌面进程启动在iOS无一般对应能力，优先级低；但 system 确实有清缓存副作用，不能描述为完全没有行为。dumpHeap/showVersion属于开发诊断/UI低优先级。

### core 中排除的误报

- Layer.SetLeft/SetTop/SetPosition/SetBounds 的 TODO：[M/core/script/tjsNativeLayer.cpp:2100-2203](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeLayer.cpp#L2100) 实际改变 Rect、通知父层、ParentUpdate；SetBounds调用InternalSetBounds。不是空实现。TODO本身不证明缺失。
- Archive.Write固定0/Flush=false：[M/core/archive/TVPStorage.cpp:1056-1061](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/archive/TVPStorage.cpp#L1056) 在打开archive条目时明确拒绝非READ，XP3Archive.cpp:917-920和TVPArchive.h:131-132属于只读stream的实现，不是已授权可写存档路径的缩水。普通文件写入另走TVPCreateStream。
- [M/core/archive/TVPStorage.cpp:1484](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/archive/TVPStorage.cpp#L1484) return false仅对应文件选择结果为空；1466调用TVPShowFileSelector，不是无条件stub，平台实现由主审计负责。
- M `core/media/font/TVPFont.cpp` 的若干return0是字体路径不存在/打开失败；实际使用FreeType枚举。Font正常尺寸/face/bold/italic/测量等有实现，不能由doUserSelect的TODO泛化。
- `SendCloseMessage`（tjsNativeWindow.h:71）虽然空，但本轮搜索没有其他core调用，未作为实际游戏缺口计入。
- NativeEventQueue.h 的Allocate/Deallocate默认空不等于消息队列不可用；对象拥有软件事件队列，需核实调用而非名称。


## 音频与平台后端：明确的实现缺口

### E1. SDL3 声像/空间位置：写属性成功，声音不变（兼容，高）

**M 行为：** `environ/sdl3/sdl3_audio.cpp:195-196` 的 `SetPan` 只写 `_pan`，`GetPan` 返回字段；实际提交音频的 `AppendBuffer:197-214` 未用该字段做声道混音。`SetPosition:255-258` 更是空实现。

调用链分两条，不能只看后端函数：

- `VideoOverlay.audioBalance` 经 `core/media/movie/krffmpeg.cpp:121-126` 直接调用声卡 `SetPan`，因此改变返回值但不改变声像。
- `WaveSoundBuffer.pan` 经 `core/script/tjsNativeWaveSoundBuffer.cpp:2057-2085`，当前 `BufferCanControlPan` 仅在构造时置 false（1004），走 `Set3DPositionToBuffer:2112-2119`，最后进入空 `SetPosition`。只修 `SetPan` 仍修不好这条路径。

**A/K 怎么做：** [A/core/sound/win32/WaveMixer.cpp:124-137,187-190,430](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/sound/win32/WaveMixer.cpp#L124) 和 [K/src/core/sound/win32/WaveMixer.cpp:109-122,162-165,349](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/sound/win32/WaveMixer.cpp#L109) 的 SDL mixer 有左右声道音量重算和真实混音；OpenAL 后端分别在 A `857-881`、K `600-621` 把位置传给 OpenAL。**但 A/K 的 Wave 层也有相同的 capability 字段问题，SDL 基类的 `SetPosition` 仍为空**（A `WaveMixer.h:64-66`，K `:33-35`）；不能声称其所有配置下 Wave.pan 均正常。可参考其 mixer/OpenAL 实现，同时修正 M 的上层路由。

触发：左右声道演出、定位音效、电影 balance。属于游戏语义，绝非 iOS 无意义的桌面功能；不直接造成 GPU readback。

证据：[M SDL3 音频](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/environ/sdl3/sdl3_audio.cpp#L195)、[A mixer](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/sound/win32/WaveMixer.cpp#L187)、[K mixer](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/sound/win32/WaveMixer.cpp#L162)。

### E2. SDL3 延迟查询恒为 0（音画时序兼容，中高）

M `sdl3_audio.cpp:242-243` 的 `GetLatencySamples/GetLatencySeconds` 均恒为 0。调用方并非闲置：`core/media/movie/VideoPlayerAudio.cpp:415-425` 将其用于播放开始同步消息，`:584` 用于提交音频时的超时估算。

A `WaveMixer.cpp:393-418` 统计 mixer 未处理样本和内部缓存，再以浮点除采样率；K `:323-338` 也有样本统计，OpenAL 分支 `:630-648` 有队列/offset 计算。不过 K 的 SDL `GetLatencySeconds` 存在整数除法，不能原样照搬。A OpenAL 对应 `:892-914`。

影响：缓冲存在时上层仍按零排队延迟估计，存在启动同步、调度误差风险；不能据此断言所有视频必然音画不同步。这里没有 GPU readback，也没有足够证据证明它导致持续 CPU 高占用。

证据：[M 恒零实现](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/environ/sdl3/sdl3_audio.cpp#L232)、[A 真实统计](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/sound/win32/WaveMixer.cpp#L393)、[K 统计](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/sound/win32/WaveMixer.cpp#L323)。

### E3. 播放游标接口缩水成“剩余队列长度”，且单位错误（兼容，高；相关逻辑错误）

M `sdl3_audio.cpp:84` 将 `_frame_size` 设为 `BitsPerSample * Channels`，`:232-235` 返回 `SDL_GetAudioStreamQueued / _frame_size`。队列长度是尚待消费的字节，既不是已播放进度，除数也未从 bit 转为 byte。例如 16-bit 双声道每帧 4 byte，这里却除以 32。

上层 `tjsNativeWaveSoundBuffer.cpp:1624`、`:1937`、`:2250` 将结果用于当前块/位置/播放进度计算，因此不是一个无害的统计字段。可能影响标签事件和依赖音频位置的同步。此项属于“接口已有但用错误简化实现替代”的明确例子，不是空函数。

A `WaveMixer.cpp:400-405`、K `:329-333` 用已送出样本减未处理样本；各自 OpenAL 实现也跟踪播放 offset。修复 M 时需要跟踪累计提交量、已消费量以及 reset/seek 语义，不能只补 `/8`。

证据：[M 初始化](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/environ/sdl3/sdl3_audio.cpp#L81)、[M 调用方](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeWaveSoundBuffer.cpp#L1611)。A/K 比较见 E2 的 mixer 链接。

### E4. Apple 原生视频工厂是 stub，但当前 iOS 构建有 FFmpeg 替代（条件性兼容）

M `environ/apple/apple_video.mm:14-23` 两个工厂记录 not implemented 后返回 nullptr；`core/media/movie/krmovie.cpp:33-64` 接着创建 Null 播放器。`krnull.h:32-41` 的 Play 立即发送完成事件，**没有播放视频内容**。

然而当前 B `CMakePresets.json:193,215` 的 iOS 模拟器/真机均设置 `USE_FFMPEG=true`，`krmovie.cpp:73-81,90-98` 会选真实 FFmpeg 对象。`scripts/build-krkr-ios.sh:67,75` 使用这两个 preset。因此应列为“关闭 FFmpeg 时丢失影片的潜在平台缺口”，不能写成“Mikage iOS 所有 OP 都被跳过”。A/K 的 `core/movie/ffmpeg/KRMoviePlayer.*` / `src/core/movie/ffmpeg/KRMoviePlayer.*` 有真实解码播放器；这不是它们提供了一个可直接移植的 Apple 原生视频工厂的证据。

证据：[Apple stub](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/environ/apple/apple_video.mm#L14)、[工厂分支](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/media/movie/krmovie.cpp#L66)。

### E5. FFmpeg 视频显示/混合/色彩控制空壳（三方共同限制，兼容）

[M/core/media/movie/krffmpeg.h:61-107](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/media/movie/krffmpeg.h#L61)：`SetRect`、mixing bitmap、movie alpha/background setter、contrast/brightness/hue/saturation setter 等为空；部分 getter 返回固定值，部分不写输出。`KRMovieOverlay.h` / `KRMovieLayer.h` 未覆盖这些色彩/混合方法。脚本 `tjsNativeVideoOverlay.cpp:614-631` 下发矩形，`:2328` 及 `:2362-2467` 暴露 movie alpha/色彩 getter，故确有可达接口。

注意上层色彩 getter 预先把返回值设为 `-1`（例如 `:1261-1304`），这里应描述为“保持 sentinel、控制无效”，不能无依据报成未初始化内存。基类 `GetFrontBuffer/SetVideoBuffer` 虽也空，但 Layer 派生类有 override，不计为整条视频链缺失。

[A/core/movie/ffmpeg/KRMoviePlayer.h:98-167](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/movie/ffmpeg/KRMoviePlayer.h#L98)、[K/src/core/movie/ffmpeg/KRMoviePlayer.h:66-109](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/movie/ffmpeg/KRMoviePlayer.h#L66) 也保留相同限制；不能靠直接换成 A/K 文件补齐。三方 `SetStopFrame/GetStopFrame` 也空，但本次未找到 M 的直接 TJS 属性注册，列为底层待补，不与已注册色彩属性等量认定。

影响：需要可定位视频窗口、视频透明混合/调色的脚本；普通全屏影片播放不等于受阻。无直接 CPU fallback/readback 证据。

证据：[M 视频控制](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/media/movie/krffmpeg.h#L54)、[A 同源限制](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/movie/ffmpeg/KRMoviePlayer.h#L87)、[K 同源限制](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/movie/ffmpeg/KRMoviePlayer.h#L58)。

## Metal：会制造 CPU fallback / readback 的明确路径

以下与“API 完全没做”分开统计：软件 fallback 是实际实现，问题是缺少 GPU 等价路径或必须满足旧 CPU 指针契约。

### P1. 透视绘制无 GPU 路径（性能，条件触发明确）

[M/core/render/MetalLayerRenderManager.cpp:980-990](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L980) 的 `OperatePerspective` 不尝试 GPU，直接标记 Perspective rejection，建立 CPUViews 后调用 `Software()->OperatePerspective`，最后将目标标脏、计入 cpuFallbacks。

[A/core/visual/ogl/RenderManager_ogl.cpp:4867-4935](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/ogl/RenderManager_ogl.cpp#L4867)、[K/src/core/visual/ogl/RenderManager_ogl.cpp:3930-3993](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L3930) 则求透视矩阵、应用到 shader，并交给 OpenGL 三角形绘制。它们是 **OpenGL 后端实现**，不等同于可原样复制的 Metal 或 Aether 当前所有宿主后端。

触发：Layer 的透视/四边形 warp 请求实际抵达该方法。GPU 已写过且 CPU cache 无效时，对目标/源/参考纹理会发生整纹理回读；后续 GPU 使用还需上传 CPU 修改。不能仅凭这条路径就认定某个游戏每帧必经它。

证据：[M 透视 fallback](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L980)、[A GL 透视](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/ogl/RenderManager_ogl.cpp#L4867)、[K GL 透视](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L3930)。

### P2. 三角形 GPU 路径只接受受限 Copy/仿射条件（性能）

M `MetalLayerRenderManager.cpp:870-879` 要求两三角形、单输入、stretch 0..2、RGBA、`Copy` 且无 flags；`:917-937` 对其余情况调用软件三角形栅格化。因此非 Copy 混合、多三角形或不满足几何条件的请求即使有 Metal 后端仍会退回 CPU。

A `RenderManager_ogl.cpp:4669` 起、K `:3760` 起有通用三角形 GL 路径，接收 render method 并绘制三角形。不能把它们的 GL 浮点像素语义自动等同于当前软件参考；M 内部对镜像、别名等保守 fallback 有明确兼容理由。

这不是“所有仿射都没 GPU”，也不是“Emote 全部 CPU”：当前 Copy 仿射以及 Emote 自己的 GPU 绘制已有实现。应通过 `fallback.triangles`、具体 method、triangle 数量及回读量确定优化收益。

证据：[M 白名单与退回](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L870)、[A GL 三角形](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/ogl/RenderManager_ogl.cpp#L4669)、[K GL 三角形](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L3760)。

### P3. fallback 回读仍是整纹理同步回读（性能机制，不是另一个空 API）

M `CPUViews::Get:569-574` → `ScanLineForFallback:309-314` → `Read:238-255`。CPU cache 无效时调用 `ReadLayerTexture`，不是按本次 clip 调用 region 读回；`backend/MetalRenderBackend.mm:935-958` 做 texture→staging blit 并 `Submit(true)` 同步等待。

因此一块 1920×1080 RGBA 纹理的有效像素数据量为 **8,294,400 byte（约 7.91 MiB）**，即便本次只改一个小区域，当前 fallback view 仍可能读整块。该数不含 staging 行对齐；不同纹理可分别产生传输，同一纹理在 CPUViews 中复用。**缓存已经有效就不再回读**，不能把每次 fallback 都机械换算为该带宽。

A `RenderManager_ogl.cpp:2405-2428`、K `:2114-2143` 的 CPU scanline 访问也有整图 `glReadPixels`；它们并非所有读回都异步。优势应归因于避免某些绘制进入 CPU，而非“参考项目不存在 readback”。

证据：[M CPU view](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L564)、[M 同步读回](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/backend/MetalRenderBackend.mm#L935)、[A CPU scanline](https://github.com/AetherKiri/AetherKrkr/blob/fa0f8af9614865aebcb839abd32bc181643d1e4a/core/visual/ogl/RenderManager_ogl.cpp#L2405)、[K CPU scanline](https://github.com/zeas2/Kirikiroid2/blob/d1c2b1259423542c893e0b65eaeb46c848848f2b/src/core/visual/ogl/RenderManager_ogl.cpp#L2114)。

### P4. 原始图像指针把纹理固定在 CPU 路径（契约代价，不能当 stub 修掉）

[M/core/script/tjsNativeLayer.cpp:3008,3023](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/script/tjsNativeLayer.cpp#L3008) 将 `mainImageBuffer` / writable buffer 请求转为 `GetPersistentCPUData`；Metal `:366-374` 先读回并设 pinned。`IsCPUResident:319` 包含 pinned，GPU rectangle `:653` 拒绝此类目标，后续操作走软件。这是暴露稳定 CPU 指针带来的持续代价。

当前已有 overwrite 专用入口 `MetalLayerRenderManager.cpp:379-386`，可跳过旧像素回读；已有 scoped write/lease 处理，不能把此前已修复的持续上传问题重新报成未实现。A/K 原始像素 API 也需 CPU 数据，见 P3。

优化方向是先找出调用 `mainImageBuffer` 的脚本/插件，把可控的短期访问改用范围/生命周期明确的接口。不能在保留裸指针的调用者仍存在时直接取消 pinned，否则会破坏指针有效性和像素正确性。

证据：[M persistent pointer](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L366)、[M GPU gate](https://github.com/itsCheney/krkrsdl3/blob/1ee1e6de3a033e316e90882a5654a9278901a2d5/core/render/MetalLayerRenderManager.cpp#L650)。

### P5. 已有实现/保守返回，不应列成缺失

- `MetalLayerRenderManager.cpp:321` 的 `IsOpaque() == false` 是保守 opacity 声明；本次未证明它造成具体昂贵分支，不计为确定性能缺陷。
- `GetTextureData:559-560` 返回 false 前确实写出 CPU 指针与 pitch；软件 texture `RenderManager.cpp:725-730` 同样如此。只抓 `return false` 会产生误报。
- Metal 矩形已实现多种 blend、UnivTrans、双源转场、blur 等路径；不得仅沿用旧记录把它们整体写成未实现。
- `CreateTexture2D(stream)` 在 M Metal `:632` 委托到 software，而 software `RenderManager.cpp:3224` 返回 nullptr；A GL `RenderManager_ogl.cpp:4221`、K GL `:3441` 有 PVRv3 解析。**这是明确的底层能力差异，但本次未找到 M 游戏加载链调用这个重载，不列成已确认游戏兼容故障。**

## iOS 适用范围与修复顺序

| 归类 | 项目 | 处理方式 |
|---|---|---|
| 游戏兼容优先 | T0/T1 文本、T2 XML、T3/T4 PSB、C1 九宫格、C2 合成抬起、E1/E3 音频 | 优先做最小输入/输出用例，补行为和错误报告，不能只加名称或吞异常 |
| 有调用需求时补 | T5 Emote 颜色/风力、T6/T7 绘图记录/边界、T11 消息框语义、C3 变频、C4 MIDI、C5 多点触摸、C6 图层类型、E5 视频控制 | 依据游戏调用证据分批做；A/K 共同空壳不能作为完成标准 |
| 性能独立处理 | P1/P2 GPU 几何能力、P3 fallback 读回范围、P4 裸指针驻留 | 先采 method/纹理尺寸/调用次数/字节/等待时间；像素语义对齐后再替换路径 |
| 真正桌面低相关 | 置顶/边框/桌面位置/异形窗口、任务栏 preview、Win32 DPI、外部进程启动 | 通常保留明确降级，不投入 iOS 桌面 UI 复刻 |
| 不能因名字像桌面就忽略 | WIN32Dialog 返回 ID、MIDI 文件播放、XMLParser、字体筛选、输入事件 | 这些仍可能是移动端运行原脚本必需的语义 |
| 编译分支之外 | Android / Windows / OHOS / WASM / SDL2 特定 stub | 不直接计为当前 iOS 的运行时故障；需单独确认目标与宏 |

平台扫描中还看到 Android 非 FFmpeg 视频的帧定位/控制 stub（`environ/android/android_video.cpp:384,410-424`），OHOS input/dialog stub 和 SDL2 文件选择降级；B `cmake/cmake_environ.txt:78-86` 的 iOS 分支选用 Apple/iOS 与所选 SDL 源码，当前 `CMakeLists.txt:83-86` 要求 iOS 使用 SDL3。这些其他平台路径不应混进 iOS 缺陷数。反过来，Windows 视频文件首行虽然写 TODO，文件内部仍有真实播放器代码，不能按文件头把整个模块判空。

### 后续验收应验证行为，而不只是编译

下面是建议的验证用例，**本次尚未执行**：

| 用例 | 应观察的结果 |
|---|---|
| TextRender：两行/缩放/带速度与 key wait 控制的文字 | newline 可调用；缩放改变字宽/字号；显示计数随时间递增；等待位置和时间非空且正确 |
| XMLParser：小型 XML + 错误 XML | link 后类存在；节点事件/错误返回真实产生 |
| PSB：同一资源分别从 path、构造 octet、load octet 加载，再测试最小 v4 | root 内容一致；v4 extra chunk 可访问；失败不留下假成功或旧 root |
| copy9Patch：每个分区使用不同颜色，CPU 与 Metal 各拉伸一次 | 九个分区及返回边距一致；不读未初始化值；正确触发重绘 |
| 合成 keyDown/keyUp | 两个事件都到达脚本/图层，真实键盘路径不受影响 |
| 音频：已知采样数的左右声道信号 | pan/balance 改变声道；播放游标按消费递增，reset/seek 正确；延迟随队列变化 |
| 透视/非 Copy 三角形，分别用全图和小 clip | 图像对比、fallback 次数、readback bytes、CPU/GPU wait 同时记录；不能只看 FPS |
| 标题实际使用的 Emote color/wind/physics | 状态变化与可见结果分别验证；不要仅以 setter 成功作为通过 |

本轮只生成此 Markdown；源码和构建配置未改动，也未提交或推送。链接固定到上述提交，并已用本地对应文件核验存在性及起始行范围。无需为这次文档审计重复运行已有渲染/动画测试；旧 CI 或历史真机结果也没有被当成本轮兼容性验证。



