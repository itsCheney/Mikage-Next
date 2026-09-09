# Mikage Next

原生 iOS 视觉小说播放器工程，第一阶段按参考截图复刻游戏库、设置页和横屏游戏内菜单。

**当前是界面与本地导入原型，尚未连接 KRKR，不能运行真实游戏。**

已实现 SwiftUI 游戏库、网格/列表、搜索排序、文件夹导入、自定义封面、设置持久化、游戏内菜单演示、截图分享和云端构建流程。真实引擎、ZIP 解压、Wi-Fi 上传和替换 App 图标尚未接入。

渲染方式目前只保存偏好；性能面板不伪造 FPS/内存。演示作品仅用于复刻布局，不写入实际游戏库。演示封面为原创矢量示意，不包含游戏内容。

## Windows 预览

```sh
node scripts/preview-server.mjs
```

打开 http://127.0.0.1:4173 。无需 npm 依赖，支持两页切换、主题和点缀色、搜索、布局和横屏菜单演示。网页不执行游戏。

## GitHub Actions 编译

推送至 main，或在 Actions → iOS build → Run workflow 手动运行。

1. macOS runner 用 swift test 检查扫描、路径隔离、导入与持久化。
2. XcodeGen 根据 project.yml 生成工程。
3. Xcode 编译未签名 arm64 App，打包 VNPlayer-unsigned.ipa。
4. 编译成功上传 IPA artifact；随后运行模拟器 UI 测试并保留 xcresult 截图附件和日志。
5. 下载 artifact，解压取得 IPA。**未签名 IPA 不能直接安装，需先用你自己的工具/证书重新签名。** CI 不要求 Apple 密码或证书。

如果编译通过而后续 UI 测试失败，IPA 仍已上传，但 workflow 会显示失败。请查看具体步骤。

## Mac 开发

需要完整 Xcode，最低目标 iOS 16。

```sh
brew install xcodegen
xcodegen generate
open VNPlayer.xcodeproj
```

选择 VNPlayer scheme，设置自己的 Team 和唯一 Bundle Identifier 后运行。核心逻辑可用 swift test 验证。

首次启动游戏库为空，点“查看界面演示”核对布局。实际导入入口为右上角加号 → 导入文件夹。每次安装使用独立 UUID 目录，游戏文件可通过“文件”App 备份。

## 结构

- App/：SwiftUI 页面、状态、菜单演示及引擎协议。
- Sources/VNCore/：Foundation 游戏扫描、导入、索引。
- Tests/VNCoreTests/：核心测试。
- UITests/：模拟器交互和截图测试。
- preview/：Windows 可用的外观预览。
- .github/workflows/ios.yml：云端编译与测试。
- docs/KRKR-INTEGRATION.md：已核对的上游接入点和后续验收。

本地检查的 KRKR 核心版本为 fb880072d603e4bcbdae68ea1e41d44dc25ab63d。third_party/ 不提交，也没有链接到此原型。引擎接入时需同时固定构建系统、子模块和依赖版本。
