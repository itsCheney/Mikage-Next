# 第二批图层性能排查与修复（2026-10-05）

确认用户的新日志来自 `290927f6a70d`，两款均使用 `gpu-metal`，共 **2,915 条记录**。附件仅作为诊断数据读取；游戏消息没有被当作操作指令。可复核的数字、文件 SHA-256 和区间统计在 [汇总 JSON](followup-layer-log-summary.json)，由 `scripts/analyze-layer-diagnostics.py` 生成，并检查累计回读、上传与区间传输之和一致。

## 确认的现象

以下为最后一条心跳的累计值，包含启动及退出确认；MB/GB 为十进制逻辑像素字节。FPS 是心跳样本分布，不能直接当作逐帧分布。退出后的保存错误不作为剧情卡顿证据。

| 游戏 | fallback | 回读 MB | 上传 MB | 最大 CPU step ms | FPS 中位数 / 最低样本 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 天使纷扰 | 26 | 440.547 | 1,215.779 | 1,175.51 | 53.83 / 8.12 |
| 千恋万花 | 12 | 703.536 | 772.172 | 4,893.93 | 56.61 / 0.20 |

普通混合算子的旧问题确实缓解：新日志没有 `AdditiveAlphaBlend[_a]`、`PsOverlayBlend`、`PsHardLightBlend` 回退。剩余 fallback 为天使纷扰的 `BoxBlurAlpha:14,DoGrayScale:12`，千恋万花的 `BoxBlurAlpha:4,DoGrayScale:8`。两份均无 persistent 回读，未看到未结束写租约上传。此前重复上传修复没有在这些日志中再次暴露。

所有心跳仍为 `thermalState=2`，没有正常热状态起点。不能用两次不同操作、不同长度的日志推出冷机发热速度或功耗改善幅度。加缪、9nine 尚无新测试。

### 天使纷扰

- 剧情中可见 `layerExBTOA.write` **19 次、210.125 MB 回读**，随后同量上传，主要是 1920×1440 纹理；集中在启动后约 44–47.5 秒。另有 `layerExBTOA.read` 12.442 MB。原有作用域访问解决了租约重复上传，但像素操作本身仍需 CPU，因而每次 GPU 修改后还会重新回读。
- `BoxBlurAlpha` 来源可见 **119.398 MB / 11 次实际回读**，`DoGrayScale` 为 10.886 MB / 12 次。回退数与实际回读数不同：缓存命中不回读，一次回退也可能读取多个纹理。
- `気配.amv` 约 25.13 秒开始加载；26.30 秒区间记录打开最大 1.120 秒、135 帧解码、**1,128,038,400 字节 RGBA**。此区间不需要新增普通 Layer 回读也能出现约 1.176 秒 step；全程 resident 峰值 1.144 GB。源码确认 `open()` 一次解码并持有全部帧，是独立的启动/内存尖峰。
- Emote CPU capture / CPU readback 为零，后续剧情区间没有持续 Emote 绘制活动，不能将这些回读归因于 Emote。

### 千恋万花

- `bitmap.scanline` 可见 **76 次、652.493 MB 回读**；`bitmap.cpuWrite` 可见 68 次、564.019 MB 上传。主要关联同一张 **纹理 499，1920×1080**，在 14–22 秒区间反复读回再上传；它没有对应大量混合算子回退。
- 流程图/界面附近约 13.08 秒出现 **3.41 秒 step**，对应 126 次 `resourceLoad` 共 3.278 秒；约 20.00 秒出现 **4.894 秒 step**，对应 257 次共 4.791 秒。全程 GPU 同步等待仅 0.804 秒，GC 共约 16 ms，无法解释这些长卡顿。
- **计时口径限制：旧 `resourceLoad` 包含 `execStorage` 加载后执行脚本的时间，也包含缓存命中和图像上传。** 这些区间支持“集中资源调用/脚本初始化很重”，尚不能精确分离纯图片解码、存储读取和界面脚本构建比例。各阶段是包含关系，不能相加。
- 源码确认软件转场 `Process` 可以完整产出目标区域，却通过普通写 scanline 先读回旧目标。日志尚无转场名称标签，因此不能最终断言纹理 499 来自哪个具体 handler。已修复这条明确的无用回读路径，并增加 `transition.source/output/outputOverwrite/outputUnscoped` 标签供复测核对。

命名来源采用每区间最大的八项，其余进入 `other`；命名数字一般是可见下界，汇总字节仍完整。

## 本批实现

1. **GPU BTOA / 灰度。** `copyRightBlueToLeftAlpha`、`copyBottomBlueToTopAlpha`、`fillAlpha` 及无错位自别名的 `clipAlphaRect` 在普通 Metal Layer 上运行。保留 COW、奇数尺寸尾部、区域裁剪、清除外部 alpha，以及 CPU 常驻原始指针兼容路径。错位 alpha 自别名在 COW 前交回原插件，保留软件扫描线顺序；province 像素访问继续软件兼容。也修正完全裁出后清除 alpha 的空指针路径。来源进一步细分到具体 BTOA 方法。
2. **GPU BoxBlur / BoxBlurAlpha。** 当前软件实现实际为 RGBA 各字节的裁剪窗口平均；两个名称共用同一公式。本批采用两次 GPU sliding-sum pass，保持整数除法取整和现有 kernel 语义，输入目标别名也无需 CPU 回读。临时 GPU sums 最大 64 MiB；超出范围或异常 geometry 保留软件路径。没有更换成视觉结果不同的近似模糊。
3. **受控转场输出。** 原 `Process` 契约要求产出全部指定目标矩形。仅完整覆盖、目标非 CPU 常驻且不与任一已知输入别名时，CPU 输出可直接写新缓冲而跳过旧目标回读；局部/未知输入/别名仍保存旧像素。租约在 `Process` 结束或异常时关闭，后续无修改取 GPU 句柄不重复上传；脚本长期原始指针仍有效。GPU handler 不访问 scanline 时不分配 CPU 缓冲。CPU 转场仍需上传新结果，本批没有把所有第三方转场改成 GPU shader。
4. **AMV 按需解码。** `open` 建立压缩帧偏移索引，播放/跳帧时解码所需帧，LRU 目标上限 **32 MiB、16 帧**。单帧大于预算时允许只保留这一帧，单帧解码仍受 256 MiB 限制；因此不能把 32 MiB 叫整个播放器内存硬上限。保留原 Huffman/DCT/RGBA 与 alpha 两种格式、脚本帧索引、canvas 和显示行为。没有新增后台线程；每次未命中仍同步解码，压缩流也可能重新读取。
5. **图像加载与测量。** 删除只打开文件、总返回空的旧 texture loader，避免正常图像重复打开及 mask/province 查询。普通 minimize 只保留最近最多 16 MiB、且不超过配置缓存四分之一的图像；明确 MAX compact、零缓存、清缓存 API 仍清空。移除死分支并确保赋图异常释放临时 bitmap。增加 `imageLoad/imageDecode/imageCacheHit/scriptStorage` 阶段，下一次复测据此判断剩余 UI 冷加载与脚本构建成本。

保持现有 60 FPS、画面效果和 TJS 接口。

## 验证与验收边界

- `Tests/MetalLayer`：实际 RenderManager、GPU 纹理/COW/缓存、生产 shader 整数 helper；新增灰度/alpha 操作与模糊逐像素比较，ROI、单行/列、奇偶/非对称/大窗口、透明度、目标别名及 alpha cache；受控转场验证全覆盖零旧目标回读、局部/别名保存、连续帧、重复句柄零重复上传、原始指针及异常释放。Windows 使用设备替身，Apple CI 才运行真实 Metal shader/绑定/排序。
- `Tests/AlphaMovie`：索引/随机访问/缓存淘汰/重开/异常 payload，加上生产 Huffman/DCT/RGBA 解码的 135 帧合成 AMV，两种 alpha 格式、透明边界、循环重解码和缓存命中。不是实际游戏 AMV 全画面视觉验收。
- `check-krkr-graphics-load.py`：生产加载/compact 函数，验证单次 bitmap 解码调用、cache key/metadata、LRU 保留、MAX 清空、禁用缓存及异常释放；解码存储边界用替身，完整文件编译由 iOS framework 构建验证。
- `layerWorkProfile` 仍关闭默认、不改变渲染选择。阶段与传输只作归因，不能把累计逻辑字节当作物理总线带宽或功耗。

发布后需真机分别复测天使纷扰的设置/鉴赏、`気配.amv` 及上述剧情段，千恋万花的首次/再次打开流程图、设置和转场。检查画面/点击/透明/转场，观察新增来源、GPU 成功/回退、上传回读和内存。首次界面卡顿可能仍受资源读取或脚本同步构建影响；该部分改善幅度以及持续掉帧/发热必须由新日志和正常热状态的 Instruments 对照确认，不能由本地通过测试宣称已消失。
