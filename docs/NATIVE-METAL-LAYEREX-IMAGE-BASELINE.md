# C2：LayerExImage 归因与精确 light LUT

日期：2026-10-10。归属 P2C C2 / G2。本轮解决方法/参数/阶段不可见及 GPU 判断之前取得 CPU 像素的问题，先提供可精确验证的 `light` GPU 子域。未宣称 DRACU 整条链路已零回读或性能改善；其余颜色方法等待新日志确定实际触发参数。

## 修改前证据

三层 HEAD：主仓库 `80812c43803796a0e65d9fc3de5780de9f362543`，Runtime `35b178858e4f1d8d07d333f046a316f24d10bb0a`，Core `ffef77a2e1d29df9062872df9585d44ca4e8ef5c`。三层工作树干净。

原始日志 `perf_log/After_P2C_C3/Mikage-diagnostics-F63BEEDA-5516-40C3-9AE8-532D793877BF.jsonl`，SHA256 `2ca718cbb9e27c5d9fb99cb5778280eff6445dffb40e2b3d84f6122671e88448`。62 个完整窗口中 `layerExImage.write` 回读/上传各 125 次、各 1,421,725,380 逻辑像素 bytes；`layerExBase` 回读 190 次、778,240 bytes。两项 read wait 合计 2,749.160326 ms。详情/调用栈分别省略 23/132 条，完整聚合未溢出。

用户提供的 `BMPBaseAffineSourceLayer.tjs` 与本地 DRACU XP3 解包内容相同；已捕获栈定位到 `_redrawImage` 的动态 Function 重放。脚本可产生 light/modulate/noise 等列表，但旧日志没有具体 native 方法或参数，不能提前把全部 125 次分配给 light。游戏原文不进入 tracked fixtures。独立核算保存在 `build/c3-f63-cpu-analysis/`。

## 实现与保留边界

- 全部五个 Image 方法使用真实 NCBind invocation policy；构造仅读取 native metadata。少参数仍由原绑定在 native instance 获取之前拒绝，尾随参数仍不消费。CPU 路径在 getter 中获取租约后才进行原参数转换，保留复杂转换的旧顺序；仅原生 receiver 的有限 primitive 数值 light 参数可推迟 acquire。
- 首批 GPU 支持域为 brightness [-255,255]、contrast [-100,100]、canonical native metadata、同会话 RGBA8 且无活动 CPU 租约/持久指针的目标。参数仍经原 NCBind 转换，原 CPU float 公式生成 256 项表，复制为不可变自有 B/G/R LUT。通过既有 COW 入口取目标，按原 Layer Clip 执行并保留原 Update/ImageModified 行为。
- 复用既有 Gamma LUT kernel，增加 `TVP_LAYER_RGB_LUT_ALL_PIXELS=16` 区分 light 与普通 Gamma：light 处理 alpha=0 下的 RGB；普通 AdjustGamma 保留透明像素跳过规则。alpha 不变、ROI 外不写。不新增注册名或 kind 编号，不使用普通 Gamma 参数 setter。
- backend 资源/租约/几何拒绝在目标写入前返回，可唯一 CPU 续跑；backend 异常保守地传播并失效 CPU 缓存，禁止 CPU 重放。GPU 完成后的 Update 异常也不重放。必要 dirty 输入上传沿原逻辑进行；未添加 submit/wait/query/readback。
- colorize/modulate/noise/generateWhiteNoise 继续原 CPU 算法及 rand 消费顺序，具名 `unsupportedMethod`。modulate 不以普通 float 近似替换原 double 算法，不把零参数自动视为像素恒等。
- 非 light CPU 路径仍需要 acquire。去除构造的提前 acquire 后，首次实际 CPU 获取归于 `layerExImage.write`；不能把 `layerExBase` 标签减少或移动当作等待消失。

## 诊断与分析器

新增原生 `metal.layerImage` v1，沿用现有 profile Take 的 generation/windowID 与 logger，C struct、Swift importer、采样周期和 HUD 不变。每个窗口包含至多 64 个方法/阶段/路由/原因/实际参数组，另有 overflow 与窗口总账；空窗口也有总账。重复同参数跨纹理调用完整累计。容量、超长标签、计数饱和、迟到调用/读取显式报告；没有无界逐调用日志或资源强引用。

阶段包含 construct/invoke/arity/conversion；构造是 metadata 记录，与实际方法 calls 分开。路由包含 gpu/cpu/noop/error。正常方法入口记录原转换后的整型或 17 位精度 double 参数；尚未进入方法的转换失败明确显示 unconverted。成功 read 的 calls/bytes/wallNS/waitNS 复用 C0 测量，缓存命中和失败读取不造样本。`parameterBytes` 是不可变 LUT 有效载荷，实际 LUT 上传另看既有 backend gammaLUT 计数，不能作为像素上传或二次计入 C0。

64 个参数组满后，route 总调用量和四项 read 指标仍完整进入总账/overflow，但具体方法/原因/参数分布是下界；分析器 `breakdownComplete=false`，不得由 overflow 推测缺失标签。迟到/换代不混入错误窗口；迟到计数导致完整性失败。分析器验证行数、重复、代际、四项 read 与参数/record 总量、调用/构造/路由恒等式，旧日志明确没有 Image profile，不补零。

## 本地验证

平台 Windows，GCC C++17 Release，Python 使用 Codex bundled runtime。Windows backend 是 device double；绑定测试提取完整生产 Image 算法、policy、getter 和生产方法清单，只替换 native Layer/backend 依赖。不能作为 Apple kernel 证据。

- 100 组 light 精确 CPU 公式对照：开关诊断、GPU/CPU 两种路径、亮度/对比度边界、Clip 外像素、alpha=0/1/2/3/255；GPU 支持域 CPU acquire=0。原 Gamma 全域回归继续通过。
- COW 保留共享源、尾随参数、少参数不构造、数字字符串的原 CPU 转换、分配/后端拒绝唯一 CPU 续跑、后端写后异常与 Update 异常不重放。其余四个方法的诊断开关像素及 rand 序列一致。
- 真实 facade/backend 标量提取：按字节验证 LUT 通道顺序、透明 RGB、alpha cache、ROI 外 cache、活动读租约拒绝、RGBA 检查、空/非法 ROI、必要 1×1 dirty 上传仅 4 bytes。
- 独立测试覆盖 64 参数组/容量和超长 overflow、饱和、迟到/换代；生产 header emitter→parser 复现 71 CPU +1 GPU，并与 C0/read 聚合对账及空窗/缺行检查。
- MetalLayer CTest 8 项，Backend CTest 2 项，TJS shutdown 1 项；解析 60 项；frame-timing、point trace debug/release、生产 LayerExImage/MetalLayerRenderManager syntax 与既有 C importer/wiring。完整输出见 `build/native-metal-layerex-image-baseline/`，首次验证暴露的透明 RGB 差异已修正，最终证据以 `.after.log` 为准。

可复放命令：

```text
cmake --build build/metal-layer-tests --parallel 4
ctest --test-dir build/metal-layer-tests --output-on-failure --timeout 120
cmake --build build/metal-render-tests --parallel 4
ctest --test-dir build/metal-render-tests --output-on-failure
cmake --build build/tjs-shutdown-tests --parallel 4
ctest --test-dir build/tjs-shutdown-tests --output-on-failure
python scripts/check-krkr-frame-times.py
python scripts/check-krkr-point-read-trace.py
python Tests/MetalLayer/test-layer-diagnostics.py
python Tests/MetalLayer/test-image-window-emitter.py
```

修改前输出为 `layer.before.log/backend.before.log/tjs.before.log`；本轮命令、源码 diff、前后三层 HEAD、证据哈希保存在同目录。未监控 CI。

## 待验与后续选择

- [ ] Apple Objective-C++/MSL 联合构建，默认 tile/强制 compute 的原生精确像素、submit/wait 和 Alpha cache 检查；device/simulator、完整 App 与原插件调用。
- [ ] 重采 DRACU 同场景的新日志，以 layerImage v1 确认实际方法/参数/构造阶段。light GPU 支持域应无该调用目标 CPU acquire/read/wait/整图 output upload；其余 CPU 路径应带具体方法/参数与拒绝原因，核对全部 C0 总量。
- [ ] 如果主路径是 modulate/colorize/noise，继续按原数值/随机语义设计专用支持域；本轮不把它们计为 GPU 已完成。
- [ ] 同设备/OS/场景/步骤/分辨率/设置/热状态/时长每组三次配对，再评价性能；原 GPU 排队工作可能移到呈现边界，不把全部历史 read wait 直接当成节省的帧时间。
