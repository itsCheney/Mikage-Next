# 第二阶段：多引擎接入计划

## 目标

在 KiriKiri/KRKRSDL3 通过真实游戏验收后，把 Mikage 从单引擎播放器演进为具有统一生命周期、检测、存档隔离和测试体系的多引擎宿主。第二阶段优先评估并接入 ONScripter、Ren’Py 和 Artemis；不以扩展名识别或“能够编译”冒充游戏兼容。

## 第二阶段启动门槛

只有以下 KiriKiri 条件全部满足，才开始合并第二个引擎：

1. Xcode 26 device/simulator framework、Mikage App、IPA、UI tests 和 A→B→A runtime smoke test全部通过。
2. 至少 3 个有权测试、结构不同的 KiriKiri 游戏完成启动、单指触摸、双指右键、三指菜单、音频、视频、截图和存读档。
3. 每个样本完成 A→退出→B→退出→A；连续启停 20 次无第二次启动崩溃、残留音频或持续内存增长。
4. 切后台/回前台、旋转、音频中断、来电中断和低内存场景行为明确。
5. 加密 XP3、原生插件和不支持脚本能够给出可理解的错误，不破坏游戏文件或已有存档。
6. KRKRSDL3 的许可证声明、完整对应修改和第三方许可证随分发物提供。

未满足门槛时，第二阶段只允许做文档研究和隔离的编译 spike，不允许改变默认启动路由。

## 先做统一架构

当前 `AppModel` 直接持有 `NativeKRKRSession`。第二阶段第一项工作是抽出不包含 KRKR 名称的公共边界：

```text
GameScanner
  -> EngineDetector[]
  -> EngineMatch(engine, confidence, evidence, versionHints)
  -> EngineRegistry
  -> GameEngineAdapter.prepare/start/stop
  -> EngineSession(state/menu/snapshot/foreground)
```

建议类型：

- `EngineID`: `kirikiri`, `onscripter`, `renpy`, `artemis`。
- `EngineEvidence`: 文件名、签名、目录结构、版本提示及冲突原因。
- `EngineDetector`: 只读扫描并返回带证据的候选，不启动代码。
- `EngineRegistry`: 根据 `GameRecord.engine` 创建 adapter；不在视图中写 switch。
- `GameEngineAdapter`: 验证资源、准备启动配置、创建 session、声明能力。
- `EngineCapabilities`: 菜单、截图、鼠标语义、视频、外部插件、热重启等能力位。
- `EngineSession`: 统一 `start/requestStop/setForeground/showMenuOverlay/hideMenuOverlay/snapshot` 和状态回调。
- `SaveLocationPolicy`: 每个导入 UUID 独立存档根；引擎原有相对路径映射到该根，禁止越界。

检测必须保留 evidence 并处理冲突。例如同一目录同时出现 `startup.tjs` 与 `game/script.rpyc` 时标为 ambiguous，不能静默选择第一个。

## 优先级一：ONScripter

### 候选基线

先比较主线 ONScripter/ONScripter-EN 与 ONScripter-RU 的维护状态、iOS 工程、Unicode、视频和脚本兼容性。ONScripter-RU 仓库自带 Xcode 工程和 UIKit 支持，但其 README 明确说明包含项目特定修改，不能直接假设适配普通 NScripter 游戏。主线/ONScripter-EN 使用 GPL-2.0，分发模型必须先通过许可证审查。

参考：

- https://github.com/umineko-project/onscripter-ru
- https://github.com/bitmingw/ONScripter
- https://github.com/Galladite27/ONScripter-EN

### 检测证据

按强到弱记录组合证据：`0.txt`/`00.txt`、`nscript.dat`、`nscr_sec.dat`、`arc.nsa`/`arc.sar`、`default.ttf`。字体或单个 NSA 文件不能单独判定。

### 接入步骤

1. 固定源码和依赖提交，建立独立 `ONSRuntime.xcframework` 构建 job。
2. 移除独立 App main，提供与 KRKR 同形的 host C API；SDL window 必须复用统一 WindowScene/overlay 策略。
3. 把脚本目录和存档目录分开传入；验证 ONS 是否允许显式 save path，否则通过受控 working directory 映射。
4. 映射单击、右键、跳过、回看和系统菜单；能力表中明确哪些操作由游戏脚本控制。
5. 用最小公开/自制 NScripter fixture 做启动、文本、选择支、存读档、音频和退出测试。
6. 再建立日文、中文、加密脚本、NSA/SAR 与视频样本矩阵。

### Go/No-Go

- Go：可作为 framework 重复启停，许可证方案可接受，至少两个普通 NScripter 样本通过。
- No-Go：只能以 GPL 不兼容方式静态并入闭源分发、只能服务单个 Umineko 项目、或无法安全分离存档路径。

## 优先级二：Ren’Py

Ren’Py 官方支持生成 iPhone/iPad Xcode 工程，但官方文档也说明 iOS 支持仍属 work in progress，工程与特定 Ren’Py 版本绑定，升级后需要重新生成。Mikage 的目标是加载用户导入的游戏，因此不能把“官方能打包单个游戏”直接等同于“一个宿主可运行任意 Ren’Py 游戏”。

参考：

- https://www.renpy.org/doc/html/ios.html
- https://www.renpy.org/doc/html/changelog.html
- https://www.renpy.org/doc/html/license.html

### 检测证据

组合检查 `game/`、`game/script.rpy` 或 `script.rpyc`、`.rpa`、`renpy/`、可执行包中的版本元数据。必须输出 major/minor 版本提示；仅有 `.rpa` 不足以判定。

### 接入步骤

1. 选定一个官方仍支持 iOS 的 Ren’Py 版本，保存完整许可证清单和构建工具链。
2. 先做“固定版本、自制最小游戏”的独立 iOS 构建；验证 Python runtime、SDL/Metal、字体、音视频和触摸。
3. 研究 generated Xcode project 中 runtime 与 game payload 的边界，判断能否合法、稳定地改造成可复用 framework。
4. 建立版本兼容策略：只启动明确支持的 Ren’Py 范围；版本不匹配时提示，不尝试用最新 runtime 强行加载。
5. 禁止导入或执行平台原生二进制 Python 扩展，除非经过签名、许可和安全审核；iOS 不允许运行下载的未签名本机代码。
6. 对 `.rpyc`、自定义 Python、屏幕尺寸、IME、视频格式和存档 pickle 兼容性建立样本矩阵。
7. 评估体积与内存预算；Ren’Py runtime 不应常驻到其他引擎 session。

### Go/No-Go

- Go：能把受支持版本范围做成可重复启停的隔离 adapter，许可证与 App Store 限制明确。
- No-Go：runtime 必须和每个游戏重新生成独立 App、无法安全加载用户 payload，或只能依赖未签名原生扩展。

## 优先级三：Artemis

这里的 Artemis 指商业视觉小说 Artemis Engine，而不是同名的 C#/MonoGame 游戏框架。当前没有确认到可公开获取、允许再分发且可嵌入 iOS 宿主的官方 runtime SDK，因此先做可行性阶段。

### 可行性阶段

1. 收集用户有权测试的 Artemis 游戏目录样本，只记录文件结构、magic、版本和平台信息，不上传商业资源。
2. 确认官方开发商、SDK、iOS runtime、再分发条款和技术联系人；没有书面许可或公开许可时不引入二进制。
3. 确定脚本/资源是否需要密钥、签名或 DRM。Mikage 不实现绕过加密、DRM 或访问控制。
4. 如果存在合法 SDK，先制作独立最小 App 和 capability report，再决定能否适配 `GameEngineAdapter`。
5. 如果不存在可用 SDK，只实现“识别并提示暂不支持”，不把第三方逆向 runtime 放进正式构建。

### Go/No-Go

- Go：获得合法可再分发 iOS runtime/源码、稳定 API 和无商业内容 fixture。
- No-Go：只能通过逆向私有二进制、提取密钥或绕过保护实现。

## 分阶段交付

### 2A：引擎无关重构

- EngineID、detector evidence、registry、adapter、capabilities、save policy。
- 将 KRKR 迁移到新接口，所有现有 KRKR 测试继续通过。
- library schema 升级并提供旧 `engine = kirikiri` 数据迁移。

### 2B：ONScripter spike 与接入

- 可复现 XCFramework、许可证、最小 fixture、adapter。
- ONS A→KRKR→ONS 跨引擎启停测试。
- 真机输入、音视频、存档和内存矩阵。

### 2C：Ren’Py 可行性与受限版本接入

- generated iOS project 分析报告。
- 固定版本 runtime spike 和体积/内存报告。
- 只有 Go 条件满足才接入默认构建。

### 2D：Artemis 法务/SDK 可行性

- detector evidence 草案、SDK/许可证结论、Go/No-Go 记录。
- 没有合法 runtime 时不安排实现里程碑。

## CI 与质量门槛

每个引擎使用独立构建 job、缓存键、许可证 artifact 和 smoke fixture。主 App job 只消费已验证的 XCFramework：

- 单引擎：A→B→A。
- 跨引擎：KRKR→ONS→KRKR；后续增加 Ren’Py。
- 每次 session 后检查 window 数、活动 display link、音频、后台线程和存档目录。
- 记录 IPA 增量体积、冷启动时间、峰值内存和首次画面时间。
- detector 单元测试覆盖阳性、阴性、伪装文件、符号链接、路径穿越和 ambiguous roots。
- runtime 失败不能损坏 library.json、原游戏目录或其他引擎存档。

## 第二阶段完成定义

第二阶段不是“列表里出现多个引擎名称”，而是：

1. KRKR 与至少一个新增引擎通过真实游戏真机矩阵。
2. 统一 registry/adapter 已取代视图和 AppModel 中的引擎特判。
3. 同引擎和跨引擎重复启停均稳定。
4. 检测证据、版本限制、能力差异和错误信息对用户透明。
5. 每个已分发 runtime 都有可复现构建、许可证清单和完整对应修改。
6. 不支持或存在法律/技术风险的游戏会明确拒绝，不静默降级或伪装成功。
