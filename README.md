# Mikage Next

Mikage Next 是原生 iOS 视觉小说播放器。界面使用 SwiftUI 与 iOS 26 原生 Liquid Glass 控件；当前可运行 KiriKiri 游戏，并为后续引擎预留统一游戏库。

当前实现包括按引擎目录自动扫描、文件夹导入、XP3/startup.tjs 启动目标检测、根目录封面识别、自定义封面、缺失游戏管理、可重入 KRKR 启停、Retina drawable、前后台完整暂停、同窗口游戏菜单、真实性能 HUD、截图分享和 savedata。应用不附带任何游戏内容。

目前只有 KiriKiri 引擎可启动；ONScripter、Ren’Py 和 Artemis 游戏可以被扫描并显示，但在对应运行时接入前保持禁用。ZIP 解压、Wi-Fi 上传和替换 App 图标尚未接入。

## Windows 预览

```sh
node scripts/preview-server.mjs
```

打开 http://127.0.0.1:4173 。网页仅预览游戏库、系统主题、搜索、布局和横屏菜单，不执行游戏。

## GitHub Actions 编译

推送至 main，或在 Actions → iOS build → Run workflow 手动运行。

1. macOS 26 runner 运行 VNCore 扫描、路径隔离、导入与持久化测试。
2. 从固定提交获取 KRKRSDL3、构建系统和 vcpkg 依赖，应用仓库内可审查的宿主补丁。
3. 构建 device 与 Apple Silicon simulator 两个 KRKRRuntime framework，并合成 XCFramework。
4. XcodeGen 生成 Mikage 工程，Xcode 26 编译未签名 arm64 App 并打包 `Mikage-unsigned.ipa`。
5. 模拟器运行界面截图测试及 KRKR A→B→A 同进程启停 smoke test。
6. 上传 IPA、完整构建日志、xcresult 和截图附件。

未签名 IPA 不能直接安装，需要使用自己的工具和证书签名。CI 不需要 Apple 密码或证书。

## Mac 开发

需要完整 Xcode 26、CMake、Git 与 vcpkg。首次构建会编译 FFmpeg、SDL3 和其他依赖。

```sh
brew install cmake xcodegen
bash scripts/build-krkr-ios.sh
xcodegen generate
open Mikage.xcodeproj
```

选择 Mikage scheme，设置自己的 Team 后运行。Bundle Identifier 为 `moe.cheney233.mikage`。

首次启动会在 App 的 Documents 根目录创建四个引擎目录：

- `krkr/<游戏文件夹>/`
- `ons/<游戏文件夹>/`
- `renpy/<游戏文件夹>/`
- `artemis/<游戏文件夹>/`

可以通过右上角加号导入，也可以直接使用“文件”App 放置游戏文件夹；Mikage 在启动、回到前台和下拉刷新时扫描各引擎目录的直属子目录。可见文件夹保持原名，UUID、游玩历史和手动封面只存于 Application Support。旧的 `Documents/Mikage/Games/<UUID>` 不会自动迁移或删除。

KRKR 优先使用根目录 `startup.tjs`，否则按 `启动游戏.xp3`、`startup.xp3`、`start.xp3`、`boot.xp3`、`data.xp3` 选择通过签名验证的 XP3。默认存档仍位于游戏目录的 `savedata/`。

## 结构

- `App/`：SwiftUI 游戏库、设置、PlayerView 和 KRKR session。
- `Engine/KRKRRuntime/`：公开 C API、完整对应补丁、许可证和固定版本说明。
- `Sources/VNCore/`：游戏扫描、路径安全、导入与索引。
- `Tests/VNCoreTests/`：核心单元测试。
- `UITests/`：界面截图与 KRKR 生命周期 smoke test。
- `scripts/build-krkr-ios.sh`：可复现的 device/simulator XCFramework 构建。
- `.github/workflows/ios.yml`：完整 engine + App 云端构建和测试。
- docs/PHASE-2-MULTI-ENGINE-PLAN.md：KRKR 真机验收通过后的 ONScripter、Ren’Py、Artemis 多引擎计划。

KRKRSDL3 的修改以补丁源码形式随仓库提供。分发修改后的 runtime 或商业游戏移植版前，请阅读 `Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt` 的再分发与源码公开条件。
