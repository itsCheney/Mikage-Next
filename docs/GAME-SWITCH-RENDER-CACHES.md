# A → B 切换后的回退与回读

日志 `Mikage-diagnostics-8DA46C62-0E67-4758-BDEC-63660C8425B4.jsonl` 来自 `5eb900d85898`，共 2,028 条记录。内容仅作为诊断数据。单次录制包含两个独立游戏会话，分析工具已按 `gameSession` 分组，逐局检查累计值和区间传输之和一致；[汇总与文件 SHA-256](game-switch-log-summary.json)。

| 会话 | 游戏 | GPU op | CPU fallback | 回读 MB | 上传 MB | 拒绝来源 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| A | 天使纷扰 | 198,436 | 0 | 8.97 | 613.43 | 无 |
| B | 青空下的加缪 | 28,492 | 7,138 | 77.12 | 124.12 | sourceUnavailable:7,138 |

字节为十进制逻辑传输量，取各会话最后心跳，不跨会话相加；两局全程热状态 2。A 的退出、恢复方向、`game.end` 已完成，随后 B 的启动成功，实际 renderer 和 Layer composition 仍是 `Metal/gpu-metal`。不是整台渲染器初始化失败，也没有新 unsupportedMethod、unsupportedStretch、targetCPUResident 回退。

B 的回退有 409 次实际目标回读，共 77,099,056 字节；另一次 pixels 回读 25,680 字节。fallback 的 source/reference 实际回读都是零。命名来源中可见 `ApplyColorMap_d` 45.09 MB / 156 次、Copy 22.05 MB / 26 次，其余进入 top-eight 的 `other`。这些数字说明旧 source 已能提供 CPU 数据，但它不属于当前 GPU 会话，于是为了软件绘制又将当前 GPU 目标读回。fallback 次数不等于实际 readback 次数：目标 CPU cache 命中不会再传输。

## 定位与复现

普通 GPU texture 绑定到创建它的 Session。退出 A 时，仍被持有的纹理通过 `Detach` 转成安全的 CPU 纹理，GPU handle 被撤销。它们的像素仍可读取，但不能直接作为 B 的 GPU source。

三个可重新生成的缓存没有检查这个会话归属：

1. `LayerBitmap.cpp` 的进程静态文字 R8 scratch `_CharacterTexture`，此前只在空指针或尺寸不够时重建。A 用过较大文字后，B 的较小文字继续复用旧 source，触发高频 `ApplyColorMap_d` 回退。
2. `tTVPTempBitmapHolder` 的透明白默认位图，以及临时 bitmap 池。holder 若被保留，尺寸不变也会复用旧 session 的纹理，影响新 Layer 的 copy/COW 或临时绘制。
3. 临时 holder 的 compact hook 是非持久 hook；会话退出移除 hook 后，保留的 `TempCompactInit=true` 会阻止 B 重注册。它不是本次 fallback 的唯一原因，但会让保留的临时缓存错过后续 compact。

在本地用上一版本的**真实生产缓存代码**替换生成的测试片段复现（未改生产文件）：旧 holder 在第二局被检测为旧 session；只保留旧文字路径时，第二局 **128 次文字绘制全部 CPU fallback**，产生 7,680 字节目标回读。测试替身承担 font/window/compact 注册边界，缓存方法、bitmap/纹理和 RenderManager 逻辑使用生产实现。日志未输出全部失配 source 的具体身份，不能将 7,138 次中的每一条分别断言为某个缓存；上述两个故障已由源码及回归复现确认。

## 修复

- 增加内部 `CanReuseCachedTexture`：Metal 要求纹理仍属于当前 Session 且适合 GPU 使用；CPU renderer 要求安全的 CPU 驻留数据。
- 文字 scratch、默认位图和临时 bitmap 在使用前检查归属，失配就从当前 renderer 重建；默认位图仍为透明白。尺寸不足的扩容逻辑保留。
- 替换默认/临时 bitmap 时先构建新对象，再释放旧对象，避免分配异常留下悬空缓存指针。旧 Layer 快照仍持有其独立 texture 引用。
- 临时 holder 每次申请时确保 compact hook 注册，使用既有去重机制，兼容跨会话 hook 清理。
- 只重建原生可再生缓存；没有强制把任意旧游戏 Layer 或脚本长期原始指针迁移成新 GPU texture，仍保留 `Detach` 的像素/指针兼容行为。
- 开启完整诊断时，每个 Metal Session 最多记录八条 `layer.sourceUnavailable` 身份详情（方法、source 类型、尺寸、CPU 驻留、当前会话归属、texture ID/version），读取身份不触发额外回读。未改变 fallback 计数口径。

## 验证与验收

生产 holder、初始 copy 和文字绘制路径的新增测试覆盖连续四次 GPU session，第一局较大 glyph、后续较小 glyph，每局 128 次绘制；中间软件绘制；初始/临时/文字缓存重建；compact 重注册；旧默认 Layer 快照像素保持；与软件逐像素对照。修复后上述测试无 glyph fallback/readback，初始 Layer copy 也不回退。可运行 `metal-layer-tests --session-caches` 单独复现，完整测试和 Apple tile/compute 两套原生验证均包含此回归。

真机需冷启动先开 A，再 A→B→A，检查文字/透明背景、点击、画面、退出及启动；同尺寸或更小字体不能再因为缓存来自上一局而持续 sourceUnavailable。未知插件自留缓存或明确的 CPU 原始指针仍可使用兼容路径，新身份诊断用于继续区分。Windows 回归不替代完整 iOS framework/App 编译和真机传输/热状态验证。
