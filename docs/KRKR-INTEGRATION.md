# KRKR 接入记录

当前默认构建是原生界面与导入原型，**没有链接 KRKR**。`App/Engine/KRKRSession.swift` 只有宿主契约。

已下载并检查核心源码版本：`fb880072d603e4bcbdae68ea1e41d44dc25ab63d`。
上游：https://github.com/krkrsdl3/krkrsdl3
构建系统：https://github.com/krkrsdl3/krkrsdl3_build
本地 `third_party/` 不会打进原型 IPA，也不提交。

## 已确认的接入点

- `environ/sdl3/sdl3_app.cpp:198` 的 `SDL_AppInit` 解析参数、初始化 SDL、创建窗口/渲染器、创建 Application 并运行启动脚本。
- 同文件 `SDL_AppQuit:561` 调用 Application.OnExit、删除 Application、清理后端/窗口并 SDL_Quit。
- 退出时 `tvp_window` 和 `tvp_glContext` 未清零；触摸静态状态也不能假定已复位。仅调用现有 quit 不足以证明可重入。
- `environ/apple/apple_core.mm:57` 的 TVPInvokeMenu 是空实现，可连接原生菜单回调。
- SDL 回调返回 APP_SUCCESS/FAILURE 属于应用终止流程，需要把游戏停止和宿主退出分开。

## 下一阶段的实际工作

1. 固定构建仓库、核心子模块、vcpkg baseline；在独立 Actions 作业编译原版 iOS 目标，保留完整依赖日志。
2. 确定 UIKit/SDL 事件循环及 UIWindow 的归属，再抽出 session start、frame/event、stop。保留上游独立程序构建入口。
3. 将 TVPInvokeMenu 转发给主线程 Swift 回调；保存 renderer、输入选项在每次启动的配置中。
4. 实测 A→退出→B→退出→A，检查脚本 VM、插件、线程、音频、纹理缓存与全局状态。失败前不要宣称热切换受支持。
5. 接入真实的菜单/自动/鼠标事件映射。自动模式不能假设所有游戏共享一个键位。
6. 接入 libarchive（ZIP 导入）与 FlyingFox（前台局域网上传），大文件以流处理，支持取消、剩余空间检查、路径穿越防护及重名处理。
7. 启动游戏后不要自动弹出菜单，且应当能够自动横屏。菜单应当在游戏内可访问，且不阻塞游戏线程。
8. 悬浮球应当能够正确显示并能够被拖拽移动。

## 真机验收

- 已知兼容的小型测试游戏能启动、触摸、播放音频和存读档。
- 20 次启停后无持续内存上涨、后台残留音频或第二次启动崩溃。
- 切后台/回前台、旋转、音频中断后继续可用。
- 两次安装有独立目录；默认 savedata 留在各自游戏目录，导出时整目录处理。
- 未连接的性能数据不使用伪造 FPS/内存数字。

此列表是后续集成计划，不是本轮已通过的验收。
