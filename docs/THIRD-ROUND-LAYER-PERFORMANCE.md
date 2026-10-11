# 第三批日志核对与修复

用户三个附件均来自应用 `fd6f50fc44b0`，共 **4,047 条记录**，使用普通 `gpu-metal` 图层合成。只将附件内容当作诊断数据。统计工具已验证最后心跳累计传输量与所有区间记录之和一致，文件摘要、阶段和来源见 [汇总 JSON](third-round-layer-log-summary.json)。

| 游戏 | 时长 s | fallback | 回读 MB | 上传 MB | 最大 step ms | 热状态 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 9nine | 52.93 | 254 | 100.74 | 616.13 | 809.71 | 全程 2 |
| 千恋万花 | 64.09 | 0 | 177.56 | 1,032.78 | 2,515.38 | 全程 2 |
| 天使纷扰 | 94.12 | 4,294 | 1,886.91 | 3,126.42 | 601.68 | 0 → 1 → 2 |

MB 为十进制逻辑像素字节；累计值包含退出确认，下面的剧情区间截止到退出请求之前。FPS 是心跳样本，GPU submission 时间是最新完成的 command buffer，并非每个显示帧的独立计时。不同阶段时间有嵌套关系，不能相加。

## 天使纷扰：两段不同的瓶颈

### 前期回退和大量传输

**全部 4,294 次 GPU 拒绝都是 `unsupportedStretch`**，集中在最初约 2.7–9.7 秒；不是又缺失了 4,294 个混合方法。`layerUnsupportedMethods` 为空、CPU pinned textures 为零。

源码的普通 rectangle GPU 路径在检查输入是否需要缩放之前，就拒绝所有大于 2 的全局 `StretchType`。软件矩形路径对 1:1 copy、fill 等根本不使用过滤器；对于 cubic/更大的正值，当前 `ResizeRGBA` 实际回到 bilinear。这使 Copy、CopyColor、FillARGB、AlphaBlend 等已支持方法也进入软件路径。

可见传输包含 CopyColor 回读 **489.37 MB / 59 次**，Copy 回读 218.08 MB / 24 次，以及纹理 510（1920×1080）的 `mixed` 上传；最大几个一秒区间约 0.4–0.55 GB 回读。来源 top-eight 之外进入 `other`，因此单独命名来源是可见下界。这条误判解释了本次启动/鉴赏一带大量回读和整图上传。

**修复：** 普通矩形按真实软件行为接纳正值过滤器；保持 affine/triangle 的独立严格过滤器规则。同时修正软件数组下标边界：值恰好等于表长度也必须 clamp，避免索引越界。没有把实际 bicubic 算法偷偷改为 bilinear；当前普通软件实现本来就是 bilinear。新增 1:1、缩放、裁剪、cubic、高位 flag、fill 及 COW/别名的对照。

### 后期低帧率

约 75.7–87.7 秒出现持续低帧率，最低样本 **24.8 FPS**；其中 78–86 秒每区间没有新增普通回读、fallback、AMV 解码，也基本没有上传。脚本总墙钟时间只有每秒约 16–86 ms，不能解释每帧约 21–39 ms 的 step。

同区间最新 GPU command 时间约 **42–102 ms**，抽样 `layerComputeMS` 约 22–50 ms，`nextDrawable` 墙钟等待常见约 16–35 ms。瓶颈转向 GPU 图层合成和展示等待。热状态在约 48.72 秒首次为 1，73.72 秒首次为 2，随后 GPU 时间上升；这是时间关联，日志没有 GPU 频率或功耗，不能直接把降频幅度当作已测量事实。87.7 秒后帧率回到约 57–58，退出请求为 91.19 秒；保存异常不作为这段低帧率原因。

**修复：** 在支持 raster order groups 的 Apple GPU 上，让相同目标的连续普通图层操作使用 programmable blending，保留目标像素在同一 render pass 的 tile memory。与 compute 路径复用同一个整数 `layerPixel` 函数，关闭固定功能混合；源目标别名仍在 GPU 上 snapshot，CPU 原始指针/软件兼容路径不变。Compute、blit、上传、读回、不同目标、提交都会结束当前 pass，避免读写排序错误。平台不支持或 pipeline 创建失败时继续现有 compute 路径。

该实现依据 Apple 的 [programmable blending / tile memory 说明](https://developer.apple.com/documentation/Metal/rendering-a-scene-with-deferred-lighting-in-objective-c)，不是用近似浮点 alpha 替换软件公式。降低全屏连续 compute 的外部读写压力是本批优化方向；手机该场景的实际帧率和热状态仍需新构建复测。旧日志没有每个 operator 的 GPU 耗时，所以尚不能断言具体哪一个图层方法占了这段全部 GPU 时间。

## 千恋万花：旧目标回读已消失，首次解码仍很重

- 本次 **fallback 为零**，没有旧 `bitmap.scanline` 652 MB 目标回读，也没有未结束写租约。
- `transition.outputOverwrite` 上传 **93 次、771.38 MB**；这是 CPU 转场产出的新像素，不能等同于重复上传泄漏。剩余 `transition.source` 回读 **14 次、154.83 MB**，是输入纹理读取，不能在未知第三方转场公式下直接删掉。本批未将所有第三方 CPU 转场改为 GPU。
- 约 8.09 秒界面区间，67 次未命中解码共 **1.608 秒**，56 次缓存命中仅约 5.8 微秒；约 12.67 秒最大卡顿区间，107 次未命中解码共 **2.405 秒**，147 次命中仅约 22.7 微秒。图像加载共 2.414 秒，script 总 2.514 秒，后者包含前者；这是比上一批“resourceLoad 可能混入 execStorage”的更明确证据。
- 因而不能把该 2.5 秒卡顿再归给 GPU 同步等待、GC 或缓存命中。本次已增加 `imageResolve/imageOpen/imageCodec` 及有界的慢 codec 资源日志，区分存储/解码器/尺寸和 mask 工作。现有附件没有 codec、具体慢图片名称和文件内容，不能声称某个 TLG/JPEG 解码器已经被定位并修好。没有忽略游戏明确的 MAX compact，也没有强制异步改掉同步加载接口。

## 9nine：新发现的混合算子

`PsColorDodge5Blend:240,AddBlend:6,PsScreenBlend:2,AdjustGamma:4`。新增前三类的 Metal 支持，覆盖软件 alpha/opacity/HDA 字节公式和错位别名兼容；继续保留低频 gamma 软件路径。PsColorDodge5 对应 Photoshop 5.x 的“先按源 alpha/opacity 缩减 RGB，再查 dodge 表”，不能直接套普通 color-dodge alpha 混合。

缓存命中 2,295 次耗时很小；416 次未命中图像加载共约 3.402 秒，界面尖峰约 0.81 秒。GPU 同步等待全程仅 0.187 秒，其他加载和脚本阶段也需独立判断。AMV 解码仅 8 帧、14 MB；天使纷扰也只解码 38 帧、317.5 MB 累计 payload，均未重新出现一次解码全部 135 帧的行为。

## 验证与新增诊断

- 生产 MSL helper 与 tvpgl 的新增 screen/dodge5/add 逐像素比较，加上 opacity、alpha 边界、HDA、裁剪、缩放和别名；原有 scalar/bitmap/转场/cache/AMV 测试保留。
- Apple CI 同时执行 tile 和强制 compute 两套实际 Metal 测试；支持的 Apple GPU 若 tile shader 未成功初始化，测试失败而不是悄悄把 compute 测试当成 tile 验证。保留 60 FPS 和现有效果。
- Release 对照：1920×1080、每帧清底色并连续 20 层、30 帧，逐次字节舍入校验、零中途 CPU 传输。`wallMS` 包含最终同步完成，是该合成负载结果；它不是实际游戏 FPS、iPhone 功耗或热测试。
- `metal.layerWork` 关联抽样 command ID，记录 rect 调用、tile draw、ROI 像素、scaled/alias/blur 像素及操作 kind 的像素分布；不新增 GPU 命令或同步。Tile 合成的 GPU 阶段时间看 `otherFragmentMS`，原 compute 合成看 `layerComputeMS`，总 GPU command 时间仍不可按 draw 数分摊。
- `image.codecSlow` 只在完整诊断开启且主 codec 墙钟至少 16 ms 时输出，每线程每秒最多八条，只含资源 basename、format、尺寸、时间；`imageCodec` 包含 codec 内部流读取和分配，不能直接称作纯算术解码时间。其余计数仍在区间 stage 中保留。

下一轮重点复测天使纷扰同一后期剧情段和最初页面、千恋万花首次/再次打开流程图、9nine UI 与效果转场；检查画面、点击、透明、GPU 阶段和来源。不同游戏/操作长度的累计字节不能直接当作同条件节能百分比。持续发热改善、千恋万花首次图片加载成本及剩余 CPU 转场上传仍是明确的真机验收边界。
