# A → B → A 进入剧情闪退

输入 `Mikage-diagnostics-3AAA32A0-63A4-41FC-BACB-FCAA3AA24719.jsonl`，2,224 条合法 JSON 记录，来自构建 `0211e4fd4d17`。文件仅作为数据解析。逐会话累计值、采样和 SHA-256 见 [汇总](game-switch-crash-log-summary.json)。用户确认：第三次启动后，从主界面进入剧情立即闪退。

## 日志能够确认的内容

原进程 pid 16907 的顺序为《天使纷扰》→《青空下的加缪》→《天使纷扰》。前两局均有退出请求、成功清理与 `game.end`；第三局在 06:02:45.596 UTC 启动成功，最后记录在 06:02:51.946，随后 06:02:55.205 出现 pid 16913 的新进程。第三局没有退出请求或正常结束记录。

| 会话 | CPU fallback | 累计回读 MB | 采样 RSS 峰值 MB |
| --- | ---: | ---: | ---: |
| A1 天使纷扰 | 0 | 10.049 | 770.114 |
| B 加缪 | 0 | 0.026 | 761.823 |
| A2 天使纷扰 | 0 | 0.556 | 938.131 |

MB 是十进制，传输量取各会话最后心跳；RSS 是心跳采样，不能当作闪退瞬间峰值。三局始终使用 Metal / gpu-metal，热状态均为 2。上一批缓存修复在此次记录中没有出现 `sourceUnavailable` 回退复发。

A2 最后心跳 FPS 37.19，最大 step 306.24 ms，GPU 驻留纹理约 122.20 MB；CPU fallback 为零，Emote CPU capture 回读也为零。最后一条是 `画面_黒.png`（2120×1280）解码 21.479 ms。它限定了日志停止的位置，不能据此断言 PNG 解码器是崩溃点。A1 退出期间有 `voicemode.tjs` / 保存异常处理的脚本异常日志，但之后退出完成；需与第三局无正常结束的进程中断区分。

这份 JSONL 没有异常类型、原生崩溃栈或 Jetsam 原因，也未显示本次确实启用了 XP3 辅助 VM / 自定义 Emote 解密。无法确认是原生访问错误、GPU 故障还是系统内存终止，更不能仅凭 RSS 938 MB 推定达到某个固定系统阈值。

## 源码确认并修复的生命周期缺陷

1. **TJS 引擎关闭会销毁其他存活引擎的对象。** `tTJS::~tTJS` 原先无条件 force-clear 进程级 script / object / native register 池。XP3 解密会创建辅助 TJS 引擎；销毁任一辅助引擎或游戏引擎，会使其他引擎留下悬空对象。现改为每个引擎正常释放自己的引用，仅最后一个引擎关闭时清理共享池和正则库；字符串/保留字等已有引用计数仍逐引擎平衡。
2. **XP3 解码器缓存跨游戏保留。** active decoder、线程退出后的 idle decoder、脚本文本和 managed 标记均无会话清理。主 VM 清理共享池后，原缓存会继续指向被销毁的对象。现退出时在图像/音频解码工作停止后、主 VM 销毁前清理所有辅助 VM；插件卸载也做幂等清理。更换过滤脚本同时清理 active 与 idle 缓存；辅助 VM 的回调和绑定上下文在 VM 仍存活时释放。增加 `xp3.sessionReset` 数量日志以确认实际使用情况。
3. **Emote 静态工作图层、窗口和解密回调未按会话释放。** 工作图层原始指针在析构后仍非空，新局可能跳过创建；解密回调替换泄漏引用，file 借用回调又没有自身所有权。现保存后、renderer / VM 清理前解除工作图层，清空静态身份/seed/模式；析构只移除自己对应的缓存身份；回调替换平衡引用，每个 file 持有自己的回调/上下文。增加 `emote.sessionReset` 身份日志，不记录对象内容或密钥。
4. **初始位图返回已销毁 holder 中的引用。** 没有现存 Layer/Bitmap 时，`TVPGetInitialBitmap` 的临时 AddRef / Release 会销毁 holder，然后返回其中对象的引用。现返回持有 texture 引用的位图值，并以作用域保证异常路径也释放 holder。TJS API 保持原样，所有内部 C++ 调用者随 framework 一起重编译。
5. **脚本最终析构可重新填充图像缓存。** 原退出清理在 VM 析构前执行，finalizer 之后仍可能留下缓存。现析构结束后再清理一次，再回收 GPU 纹理。

这些是源码和回归能够确认的错误；缺少原生 crash report 时，仍不能逐一认定其为这份真机记录的直接原因。

## 验证与复测

- 新回归用真实 TJS 引擎和生产 XP3 decoder / reset / replace 方法，覆盖 50 次辅助引擎缓存清理，另覆盖 50 次主/辅助引擎共存及两种关闭顺序。存活引擎在对方关闭后仍需执行函数、读写 global、创建 Array/Dictionary。
- 把旧版生产 `tjs.cpp` 单独链接到新回归，稳定触发 SIGSEGV：`tTJSVariant::Clear → VariantArrayStack::BeginShutdown → tTJS::~tTJS → XP3FilterDecoder::~XP3FilterDecoder → ClearXP3Decoders`。修复后的同一回归通过。这是合成生产代码复现的栈，不是真机此次闪退的栈。
- Emote 回归使用真实 TJS variant / closure 引用计数和生产 manager / file 方法，覆盖 50 个 VM、100 次工作图层创建/清理、反复安装同一回调、替换后的 live file、绑定上下文、重复 session reset，以及旧 adaptor 析构不清除新会话缓存。
- Metal Layer 回归新增无现存 holder 时取得初始位图，跨三个会话 detach 后像素仍有效；保留此前四会话文字缓存和逐像素对照。退出顺序回归同时检查 Emote 清理发生在 VM/renderer 可用期间、VM finalizer 重新填充的图像缓存最终为空。

本地回归与语法检查通过后推送完整三层仓库，由 Apple CI 编译 framework、App、IPA 并验证原生 Metal tile / compute 两条路径。本地替身与合成脚本不能替代真机游戏资产复测。

真机先完整结束旧进程，再冷启动 A → 进入剧情 → 退出 → B → 进入剧情 → 退出 → A → 进入剧情，重复至少三轮；再做 A → A 对照。若仍中断，需要同次录制及 iOS 的 `Mikage*.ips` 或 `JetsamEvent*.ips`，将异常栈/终止原因与 session reset 数量、最后心跳内存对齐后继续定位。
