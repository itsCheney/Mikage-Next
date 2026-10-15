# Mikage Next

简体中文 · [English](README.en.md)

Mikage Next 是一款适用于 iPhone 和 iPad 的视觉小说播放器。所有游戏集中在同一个游戏库里，每个游戏用它原本的引擎运行。目前通过内嵌的 [KRKRSDL3](https://github.com/itsCheney/krkrsdl3) 支持 KiriKiri（吉里吉里，KAG / TJS）游戏，通过 [ONScripter Yuri](https://github.com/YuriSizuku/OnscripterYuri) 支持 ONScripter 游戏，通过 [openArtemis](https://github.com/itsCheney/openartemis) 试验性地支持 Artemis 游戏。Ren’Py 是接下来的目标：这类游戏已经能被识别并显示在游戏库中，但在运行时接入之前暂时无法启动。

Mikage 不附带任何游戏内容，请使用你自己拥有的游戏。

## 功能

**游戏库**
- 每个引擎在 App 的“文稿”中有自己的文件夹：`krkr/`、`ons/`、`renpy/` 和 `artemis/`。用“文件”App 把游戏文件夹放进去，或者在游戏库右上角点 + 导入文件夹。
- 启动、回到前台和下拉刷新时会重新扫描。游戏文件夹保持原名，Mikage 自己的记录保存在 Application Support 中。
- Artemis 游戏文件夹中需要有 `root.pfs`（或其他 `.pfs` 包），或解包后带 `system.ini` 的工程目录。
- ONScripter 游戏文件夹中需要有 `0.txt`、`00.txt` 或 `nscript.dat` 等脚本文件。文件夹里有 `default.ttf` 时使用它，否则使用系统字体（日文游戏用冬青黑体，中文游戏用苹方）。
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

## 进度

| 引擎 | 识别 | 运行 |
| --- | --- | --- |
| KiriKiri（KRKRSDL3） | 支持 | 支持 |
| ONScripter（ONScripter Yuri） | 支持 | 支持 |
| Ren’Py | 支持 | 尚未支持 |
| Artemis（openArtemis） | 支持 | 试验性 |

ZIP 导入和 Wi-Fi 上传暂未实现。后续引擎的接入计划见 [docs/PHASE-2-MULTI-ENGINE-PLAN.md](docs/PHASE-2-MULTI-ENGINE-PLAN.md)。

## 编译

需要 macOS 和 Xcode 26、CMake、XcodeGen、Git。vcpkg 会由运行时构建脚本自动准备。首次构建需要编译 FFmpeg、SDL3 等依赖，耗时较长。

```sh
git clone --recursive https://github.com/itsCheney/Mikage-Next.git
cd Mikage-Next
brew install cmake xcodegen
bash scripts/build-sdl3-ios.sh   # 生成各引擎共享的 build/SDL3.xcframework
bash scripts/build-krkr-ios.sh   # 生成 build/KRKRRuntime.xcframework
bash scripts/build-ons-ios.sh    # 生成 build/ONSRuntime.xcframework
bash scripts/build-artemis-ios.sh  # 生成 build/ArtemisRuntime.xcframework
xcodegen generate
open Mikage.xcodeproj
```

选择 Mikage scheme，设置你自己的签名 Team 后运行。最低部署版本为 iOS 16。

如需通过 Bangumi 网页登录（而不是粘贴 Token），请在 [bgm.tv/dev/app](https://bgm.tv/dev/app) 注册应用，并通过构建设置传入 `BANGUMI_APP_ID` 和 `BANGUMI_APP_SECRET`（见 `project.yml`）。留空时 Bangumi 使用 Access Token 登录。

每次推送到 `main` 或 `beta` 时，GitHub Actions 会运行 [iOS 构建流程](.github/workflows/ios.yml)：运行测试、构建运行时和 App，并上传未签名的 `Mikage-unsigned-<编号>.ipa`。安装前需要自行签名。

## 分支

- `main`：稳定版。
- `beta`：先于 `main` 的测试版。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `App/` | SwiftUI App：游戏库、游戏详情、播放器、设置、存储空间、Bangumi/VNDB 关联、字符串目录 |
| `App/Engine/` | 在 App 内承载 KiriKiri、ONScripter 和 Artemis 运行时的桥接层；`SDLHost/` 是各 SDL 引擎共用的部分（横屏、窗口切换、逐帧驱动、前后台） |
| `Sources/VNCore/` | Swift 包：游戏库扫描与导入、路径安全、Bangumi 和 VNDB 客户端、条目匹配 |
| `Engine/KRKRRuntime/` | 以子模块固定版本的 KRKRSDL3 运行时（[krkrsdl3_build](https://github.com/itsCheney/krkrsdl3_build) → [krkrsdl3](https://github.com/itsCheney/krkrsdl3)） |
| `Engine/ONSRuntime/` | 以子模块固定版本的 ONScripter Yuri（[Mikage 分支](https://github.com/itsCheney/OnscripterYuri)），经 sdl2-compat 运行在共享的 SDL3 上 |
| `Engine/ArtemisRuntime/` | 以子模块固定版本的 openArtemis（[Mikage 分支](https://github.com/itsCheney/openartemis)），直接运行在共享的 SDL3 上 |
| `Engine/MikageSDLHost/` | 基于 SDL 主回调的引擎运行时编译进去的共享宿主层（KiriKiri 和 Artemis）：驱动 SDL 主回调、停止与前后台、音频会话、每局之间的复位 |
| `Tests/` | Swift 单元测试和原生运行时测试 |
| `scripts/` | 构建脚本和诊断分析工具 |
| `docs/` | 设计笔记和接入记录 |

## 许可证

Mikage Next 是自由软件，以 [GNU 通用公共许可证第 3 版或更高版本（GPL-3.0-or-later）](LICENSE) 发布。

内置的 KiriKiri 运行时有其自身的许可证，见 [Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt](Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt) 以及运行时子模块中的声明。内置的 ONScripter Yuri 以 GPL-2.0-or-later 发布，它播放 MIDI 用的 GeneralUser GS 音色库有自己的许可证，均见 [Engine/ONSRuntime/README.md](Engine/ONSRuntime/README.md)。内置的 openArtemis 以 MIT 或 GPL-3.0-or-later 双许可发布，Mikage 按 GPL-3.0-or-later 使用，见 [Engine/ArtemisRuntime/README.md](Engine/ArtemisRuntime/README.md)。分发构建产物前请先阅读。

KiriKiri、ONScripter、Ren’Py、Artemis、Bangumi 和 VNDB 归各自作者所有，Mikage 与它们没有任何关联。
