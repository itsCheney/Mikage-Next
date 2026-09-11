# KRKR 接入记录

Mikage 已将 KRKRSDL3 封装为可嵌入 SwiftUI 宿主的 `KRKRRuntime.xcframework`。runtime、对应源码补丁、Swift session 和 PlayerView 启停链路均已接入主工程；不再使用只显示演示界面的占位 session。

## 固定版本

- `krkrsdl3_build`: `66fb7d9533478d33317208cd8ec8696ab9340d6f`
- 官方构建仓库锁定的 `krkrsdl3` core: `5a8bd422f82d3758045f403520a64b772a59f40c`
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`
- 上游核心：https://github.com/krkrsdl3/krkrsdl3
- 上游构建：https://github.com/krkrsdl3/krkrsdl3_build

完整对应修改位于 `Engine/KRKRRuntime/Patches/`，公共 C API 位于 `Engine/KRKRRuntime/Host/`。`third_party/` 仅用于本地检查，不提交。

## 已接入

1. `scripts/build-krkr-ios.sh` 从固定提交构建 device 与 Apple Silicon simulator framework，再合成 XCFramework。
2. 保留上游独立 iOS App target；仅在 `KRKR_HOST_LIBRARY=ON` 时排除 `sdl3_entry.cpp` 并生成动态 framework。
3. Swift 宿主在主线程调用 `SDL_SetMainReady`，通过 `CADisplayLink` 驱动 `SDL_AppEvent` 与 `SDL_AppIterate`。
4. SDL window 绑定当前 `UIWindowScene`。游戏启动时自动请求横屏，结束后恢复系统方向策略。
5. 游戏从 `Documents/krkr/<游戏文件夹>` 扫描，目录保持用户命名；隐藏 UUID 和历史位于 Application Support。目录和启动目标均经过 containment 与符号链接校验，默认存档保存在游戏目录的 `savedata/`。
6. KRKR `TVPInvokeMenu`、三指手势和原生悬浮球统一回调 Swift 菜单。悬浮球使用 iOS 26 Liquid Glass，并支持安全区域内拖拽。
7. 菜单显示时游戏 frame loop 继续运行；SwiftUI 透明浮层直接覆盖 SDL window，底层 Metal 游戏画面保持可见。
8. 正常退出依次执行 Application.OnExit、插件/脚本 VM/窗口/纹理清理、render backend 销毁、SDL window/context/audio 清理，并重置触摸静态状态。
9. UI test 提供无商业内容的空 `startup.tjs` fixture，执行 A→B→A 同进程启停 smoke test。
10. 工程、scheme、target、IPA 与 artifact 名统一为 Mikage，Bundle Identifier 为 `moe.cheney233.mikage`。
11. 目录启动目标在 Swift 与 Objective-C++ 边界统一补全结尾 `/`；桥接层捕获 `eTJS`、标准 C++ 和未知异常，将启动错误返回 Swift，避免异常越过 C API 导致 `SIGABRT`。
12. 启动前由 Swift 等待 `UIWindowScene` 完成横屏 geometry update，再创建 SDL window；iOS 使用 high-pixel-density drawable 和真实像素 viewport。
13. 游戏菜单、性能 HUD 与悬浮按钮直接挂载到 SDL window，Metal 画面不再通过 `CALayer.render` 伪截图作为背景。
14. 前后台切换会暂停 frame loop、Wave/Video 音频流、视频时钟和 `AVAudioSession`；回到前台按原播放状态恢复。
15. TJS global、对象池、扩展类注册和一次性系统状态已改为可重入，CI 使用对象池压力脚本执行 10 次 A/B 交替启停。
16. 图像缓存读写与 compact 使用同一递归锁，防止异步图片加载破坏缓存哈希链；触摸先映射到 drawable 像素，再由 KRKR 仅执行一次 letterbox 逆变换。
17. 退出时清空普通、输入、窗口与 continuous 事件，并丢弃会话级 compact/continuous hook；仅显式标记的进程级静态缓存回调跨会话保留。
18. 每个 VideoOverlay 都加入会话注册表；退出时同步释放 active/cached player 及其解码线程、overlay 纹理和 SDL audio stream，再清空 host 音频注册表并关闭 SDL。
19. 返回游戏库前保持黑色 Player 过渡层，等待 WindowScene 与宿主窗口连续确认恢复进入游戏前的方向和 bounds 后才 dismiss，避免库页面短暂按横屏重排。
20. 每次会话开始和结束都把图像缓存开关、上限与格式 handler 表恢复到冷启动状态；异步图片错误写入各游戏 `savedata/krkr.console.log`，连续三帧无 KRKR 窗口时自动返回游戏库。

## 设计约束

- 自动模式不能假设所有游戏共享一个键位；当前不会向真实游戏伪造“自动”按键。
- iOS runtime 使用 SDL 系统 Metal renderer 或 KRKR OpenGL ES backend，不启用 Vulkan。
- framework 自带 `DroidSansFallback.ttf`，资源读取会在主 bundle 失败后回退到 framework bundle。
- App 不附带游戏或商业素材。
- 分发修改后的 KRKRSDL3 runtime 或商业游戏移植版前，必须遵守 `Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt` 的声明和源码公开条件。

## 真机验收仍需实际游戏

以下项目不能由空脚本或模拟器替代，发布前必须使用有权测试的真实 KiriKiri 游戏完成：

- 启动、单指触摸、双指右键、三指菜单、音频、视频和存读档。
- A→退出→B→退出→A，检查脚本 VM、插件、线程、音频、纹理缓存与全局状态。
- 连续 20 次启停后无持续内存上涨、后台残留音频或第二次启动崩溃。
- 切后台/回前台、旋转和音频中断后继续可用。
- 加密 XP3、游戏自带插件和不同 KiriKiri 版本的兼容性矩阵。

在这些真机测试通过前，只能声明“KRKR runtime 已集成并通过构建/生命周期 smoke test”，不能声明所有 KiriKiri 游戏均兼容。

## 后续阶段

KRKR 真机门槛通过后，按 `docs/PHASE-2-MULTI-ENGINE-PLAN.md` 抽象统一 adapter，并依次评估 ONScripter、Ren’Py 和 Artemis。KRKR 未通过真实游戏验收前，不改变默认单引擎路由。
