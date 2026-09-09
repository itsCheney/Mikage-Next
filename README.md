# Mikage Next

Mikage Next 是原生 iOS KiriKiri 视觉小说播放器。界面使用 SwiftUI 与 iOS 26 原生 Liquid Glass 控件，游戏运行时由 KRKRSDL3 驱动。

当前实现包括游戏库、文件夹导入、XP3/startup.tjs 检测、网格与列表、搜索排序、自定义封面、设置持久化、KRKR 启停、SDL 触摸/音频/视频链路、游戏内菜单、可拖拽悬浮球、截图分享和独立 savedata。应用不附带任何游戏内容。

目前只启用 KiriKiri 引擎；其他引擎将在 KRKR 真机兼容性稳定后再适配。ZIP 解压、Wi-Fi 上传和替换 App 图标尚未接入。

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

首次启动游戏库为空。通过右上角加号选择包含 `startup.tjs` 或有效 XP3 的游戏目录。每个导入使用独立 UUID 目录，默认存档位于该游戏目录的 `savedata/`。

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
