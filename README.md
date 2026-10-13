# Mikage Next

[English](#english) · [简体中文](#简体中文)

---

## English

Mikage Next is a visual novel player for iPhone and iPad. It keeps all your games in one library and runs each one with the engine it was made for. KiriKiri (KAG / TJS) games play today, through an embedded build of [KRKRSDL3](https://github.com/itsCheney/krkrsdl3). ONScripter, Ren’Py and Artemis are next: their games are already recognized and listed, but stay disabled until their runtimes land.

Mikage ships no game content. You bring games you own.

### What it does

**Library**
- Every engine has its own folder in the app’s Documents: `krkr/`, `ons/`, `renpy/` and `artemis/`. Put a game folder in one of them with the Files app, or import a folder with the + button in the Library.
- The library rescans on launch, when the app returns to the foreground, and on pull to refresh. Folders keep their original names; Mikage’s own records live in Application Support.
- For KiriKiri, Mikage picks `startup.tjs` at the game root if there is one, otherwise the first XP3 among `启动游戏.xp3`, `startup.xp3`, `start.xp3`, `boot.xp3` and `data.xp3` that passes its signature check.
- Covers come from an image in the game folder, a picture you choose, or artwork from Bangumi or VNDB. A cover you set yourself is never replaced.
- Sort by recently played, date added, name or size. Games whose folder disappeared are kept as “missing” so you can relink or remove them.

**Playing**
- Games start inside the app and can be quit and relaunched, or switched for another game, without restarting Mikage.
- An in-game menu (floating button or three-finger tap) offers a screenshot, a performance overlay, “Mark Finished” and Quit. Quit goes through the game’s own exit confirmation, so it saves the way the game expects.
- The game pauses fully in the background and resumes where it was.
- Rendering is native Metal by default, including Emote animations, D3D layers and the built-in transitions. Software compositing presented through Metal is available as a fallback in Settings › Advanced.
- Saves stay in the game’s own `savedata/` folder.
- Options for the letterbox fill (game cover or black), honoring Silent Mode, and skipping translation-group credit videos.

**Bangumi and VNDB**
- Match a game to its Bangumi subject and/or VNDB entry to get its cover, synopsis, tags, developer, release date and screenshots. None of this needs an account.
- Sign in to Bangumi (or paste an access token) and add a VNDB token to sync your status, rating and comments. Mikage can move a game to “Playing” after a session and record start and finish dates on VNDB. Tokens are kept in the device Keychain.
- Title display: Bangumi Chinese name, original title, romanized title or folder name.

**Storage**
- Settings › Storage shows what Mikage uses: game files, saves, cover cache and diagnostic logs, with games sorted largest first.
- Per game: show in Files, export saves, remove the game but keep its saves (they come back when you import it again), or delete everything.
- Clear logs, and free space in one step by removing finished games while keeping their saves.

**Languages:** English, 简体中文 and 日本語.

### Status

| Engine | Detected | Plays |
| --- | --- | --- |
| KiriKiri (KRKRSDL3) | Yes | Yes |
| ONScripter | Yes | Not yet |
| Ren’Py | Yes | Not yet |
| Artemis | Yes | Not yet |

ZIP import and Wi-Fi upload aren’t available yet. The plan for the next engines is in [docs/PHASE-2-MULTI-ENGINE-PLAN.md](docs/PHASE-2-MULTI-ENGINE-PLAN.md).

### Building

Requirements: macOS with Xcode 26, CMake, XcodeGen and Git. vcpkg is bootstrapped by the runtime build. The first build compiles FFmpeg, SDL3 and the other runtime dependencies, so it takes a while.

```sh
git clone --recursive https://github.com/itsCheney/Mikage-Next.git
cd Mikage-Next
brew install cmake xcodegen
bash scripts/build-krkr-ios.sh   # builds build/KRKRRuntime.xcframework
xcodegen generate
open Mikage.xcodeproj
```

Choose the Mikage scheme, set your own signing team, and run. The deployment target is iOS 16.

To use Bangumi website sign-in instead of pasted tokens, register an app at [bgm.tv/dev/app](https://bgm.tv/dev/app) and pass `BANGUMI_APP_ID` and `BANGUMI_APP_SECRET` as build settings (see `project.yml`). Left empty, Bangumi falls back to access tokens.

Every push to `main`, `beta` or `metal_dev` also runs the [iOS build workflow](.github/workflows/ios.yml) on GitHub Actions. It runs the tests, builds the runtime and the app, and uploads an unsigned `Mikage-unsigned-<run>.ipa`. Sign it yourself before installing.

### Branches

- `main`: stable.
- `beta`: testing ahead of `main`.
- `metal_dev`: active development.

### Repository layout

| Path | Contents |
| --- | --- |
| `App/` | The SwiftUI app: library, game details, player, settings, storage, Bangumi/VNDB linking, string catalog |
| `App/Engine/` | The bridge that hosts the KiriKiri runtime inside the app |
| `Sources/VNCore/` | Swift package: library scanning and import, path safety, Bangumi and VNDB clients, matching |
| `Engine/KRKRRuntime/` | The KRKRSDL3 runtime, pinned as a submodule ([krkrsdl3_build](https://github.com/itsCheney/krkrsdl3_build) → [krkrsdl3](https://github.com/itsCheney/krkrsdl3)) |
| `Tests/` | Swift unit tests and native runtime tests |
| `scripts/` | Build scripts and diagnostic analysis tools |
| `docs/` | Design notes, investigations and performance baselines |

### License

Mikage Next is free software, released under the [GNU General Public License v3.0 or later](LICENSE).

The bundled KiriKiri runtime carries its own licenses: see [Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt](Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt) and the notices inside the runtime submodule. Read them before distributing builds.

KiriKiri, ONScripter, Ren’Py, Artemis, Bangumi and VNDB are the work of their respective authors. Mikage is not affiliated with any of them.

---

## 简体中文

Mikage Next 是一款适用于 iPhone 和 iPad 的视觉小说播放器。所有游戏集中在同一个游戏库里，每个游戏用它原本的引擎运行。目前通过内嵌的 [KRKRSDL3](https://github.com/itsCheney/krkrsdl3) 支持 KiriKiri（吉里吉里，KAG / TJS）游戏。ONScripter、Ren’Py 和 Artemis 是接下来的目标：这些游戏已经能被识别并显示在游戏库中，但在对应运行时接入之前暂时无法启动。

Mikage 不附带任何游戏内容，请使用你自己拥有的游戏。

### 功能

**游戏库**
- 每个引擎在 App 的“文稿”中有自己的文件夹：`krkr/`、`ons/`、`renpy/` 和 `artemis/`。用“文件”App 把游戏文件夹放进去，或者在游戏库右上角点 + 导入文件夹。
- 启动、回到前台和下拉刷新时会重新扫描。游戏文件夹保持原名，Mikage 自己的记录保存在 Application Support 中。
- KiriKiri 游戏优先使用根目录的 `startup.tjs`；没有的话，依次在 `启动游戏.xp3`、`startup.xp3`、`start.xp3`、`boot.xp3`、`data.xp3` 中选择第一个通过签名校验的 XP3。
- 封面可以来自游戏文件夹里的图片、你自己选的图片，或者 Bangumi / VNDB 的封面。你手动设置的封面不会被覆盖。
- 可按最近游玩、添加日期、名称或大小排序。文件夹不见了的游戏会标为“缺失”，可以重新关联或删除。

**游玩**
- 游戏在 App 内运行，可以退出后重新启动或切换到另一个游戏，无需重启 Mikage。
- 游戏内菜单（悬浮按钮或三指轻点）提供截图、性能信息、“标为已通关”和退出。退出会走游戏自己的退出确认，按游戏原本的方式存档。
- 切到后台时游戏完全暂停，回来后从原处继续。
- 默认使用原生 Metal 渲染，包括 Emote 动画、D3D 图层和内置转场效果。设置 › 高级中可以改用“软件合成 · Metal”作为备选。
- 存档保存在游戏自己的 `savedata/` 文件夹中。
- 可设置画面两侧的填充（游戏封面或黑色）、是否遵循静音模式，以及跳过汉化组片头视频。

**Bangumi 与 VNDB**
- 把游戏匹配到 Bangumi 条目和/或 VNDB 条目，即可获取封面、简介、标签、开发商、发售日期和截图。这些都不需要登录。
- 登录 Bangumi（或粘贴 Access Token）并添加 VNDB Token 后，可以同步游玩状态、评分和短评。游玩结束后 Mikage 可以把游戏标为“在玩”，并在 VNDB 上记录开始和通关日期。Token 保存在设备钥匙串中。
- 标题显示可选 Bangumi 中文名、原名、罗马字或文件夹名。

**存储空间**
- 设置 › 存储空间显示 Mikage 占用的空间：游戏文件、存档、封面缓存和诊断日志，游戏按大小从大到小排列。
- 单个游戏可以在“文件”中显示、导出存档、删除游戏但保留存档（重新导入后存档自动恢复），或全部删除。
- 可以清除日志，也可以一键删除已通关的游戏并保留其存档来释放空间。

**界面语言：** English、简体中文、日本語。

### 进度

| 引擎 | 识别 | 运行 |
| --- | --- | --- |
| KiriKiri（KRKRSDL3） | 支持 | 支持 |
| ONScripter | 支持 | 尚未支持 |
| Ren’Py | 支持 | 尚未支持 |
| Artemis | 支持 | 尚未支持 |

ZIP 导入和 Wi-Fi 上传暂未实现。后续引擎的接入计划见 [docs/PHASE-2-MULTI-ENGINE-PLAN.md](docs/PHASE-2-MULTI-ENGINE-PLAN.md)。

### 编译

需要 macOS 和 Xcode 26、CMake、XcodeGen、Git。vcpkg 会由运行时构建脚本自动准备。首次构建需要编译 FFmpeg、SDL3 等依赖，耗时较长。

```sh
git clone --recursive https://github.com/itsCheney/Mikage-Next.git
cd Mikage-Next
brew install cmake xcodegen
bash scripts/build-krkr-ios.sh   # 生成 build/KRKRRuntime.xcframework
xcodegen generate
open Mikage.xcodeproj
```

选择 Mikage scheme，设置你自己的签名 Team 后运行。最低部署版本为 iOS 16。

如需通过 Bangumi 网页登录（而不是粘贴 Token），请在 [bgm.tv/dev/app](https://bgm.tv/dev/app) 注册应用，并通过构建设置传入 `BANGUMI_APP_ID` 和 `BANGUMI_APP_SECRET`（见 `project.yml`）。留空时 Bangumi 使用 Access Token 登录。

每次推送到 `main`、`beta` 或 `metal_dev` 时，GitHub Actions 会运行 [iOS 构建流程](.github/workflows/ios.yml)：运行测试、构建运行时和 App，并上传未签名的 `Mikage-unsigned-<编号>.ipa`。安装前需要自行签名。

### 分支

- `main`：稳定版。
- `beta`：先于 `main` 的测试版。
- `metal_dev`：日常开发。

### 目录结构

| 路径 | 内容 |
| --- | --- |
| `App/` | SwiftUI App：游戏库、游戏详情、播放器、设置、存储空间、Bangumi/VNDB 关联、字符串目录 |
| `App/Engine/` | 在 App 内承载 KiriKiri 运行时的桥接层 |
| `Sources/VNCore/` | Swift 包：游戏库扫描与导入、路径安全、Bangumi 和 VNDB 客户端、条目匹配 |
| `Engine/KRKRRuntime/` | 以子模块固定版本的 KRKRSDL3 运行时（[krkrsdl3_build](https://github.com/itsCheney/krkrsdl3_build) → [krkrsdl3](https://github.com/itsCheney/krkrsdl3)） |
| `Tests/` | Swift 单元测试和原生运行时测试 |
| `scripts/` | 构建脚本和诊断分析工具 |
| `docs/` | 设计笔记、问题排查记录和性能基线 |

### 许可证

Mikage Next 是自由软件，以 [GNU 通用公共许可证第 3 版或更高版本（GPL-3.0-or-later）](LICENSE) 发布。

内置的 KiriKiri 运行时有其自身的许可证，见 [Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt](Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt) 以及运行时子模块中的声明。分发构建产物前请先阅读。

KiriKiri、ONScripter、Ren’Py、Artemis、Bangumi 和 VNDB 归各自作者所有，Mikage 与它们没有任何关联。
