# C2B：最小参数数量契约补修

2026-10-09。三个绘制入口的 GPU 预检与生产 NCBind 最小参数契约已对齐；另通过用户提供的字节码定位并补修已知版本设置页的 `direct` 错误。两项分别验收，设备设置闭环及新GPU命中仍待重采。Nekopara 保持延后。

发布补记：用户随后授权提交推送。Core `5e54d4678119c79848f6aab9d759550c8ff0a663` 与Runtime `b6f31a2f892a0f4e20641471a6c361d2b899ac37` 已按顺序发布并核对远端，主仓库测试/证据与指针随包含本记录的提交发布。manifest保留修改前/本地验收快照并附发布信息；不跟踪CI构建，Apple与设备待验项保持。

## 基线与来源

| 仓库 | 修改前 HEAD |
| --- | --- |
| 主仓库 | `5987acb274a7c4d581253393e60a1f8a1794e5c9` |
| Runtime | `1f8c04839d2bf1eec5805ec8c80190799d22f681` |
| Core | `e3302bed3d4f6f65d1acba10bdce383dea48c41d` |

三仓开始时干净。原计划、三个空diff、原LastTest输出和After_P2C_C2B-2八份输入哈希先保存于 `build/native-metal-c2b-arity-baseline`；生产/测试原版另从修改前HEAD保存，历史输出不是本次重建结果。

After_P2C_C2B-2均为主仓库短revision `5987acb274a7`：千恋11调用全部GPU；两份天使各71调用（GPU4、CPU67），CPU为drawPath/arguments33、record33及drawImageStretch/vectorSource1。八份626个完整work窗口的v2聚合、代表和总账对账通过，没有容量丢失、溢出或缺行；历史v1省略39条保持未知。本批日志已证明部分实际GPU命中与统计完整性，不证明完整Apple矩阵或性能改善。详细原始行号与复算保留于 `build/c2b-2-log-analysis`，原日志未改。

## 修复契约

NCBind `doInvoke` 仅在 `_numparams < SelectorT::ArgsCount` 返回 `TJS_E_BADPARAMCOUNT`，`paramsFunctor` 只转换签名对应的前N项，多余实参不在native调用阶段消费。旧GPU预检却严格要求drawLine=5、drawPath=2、drawImageStretch=9，导致合法额外参数误走CPU/arguments。

生产只改三处守卫为 `<5 / <2 / <9`，并注明NCBind契约；前N项的类型、数值、paint/path、source、record、lease、alias、Clip、预算和backend检查不变。少参数仍走原错误路径，不越界访问。未增加getter、转换、GPU查询、readback、submit、wait或新开关；shader数学和参数/渲染编号保持。

新测试通过真实生产绑定分别调用签名参数＋1项和＋4项：含会在调用时抛错的未使用script对象、非数值字符串、infinity及void。旧预检在六例均为gpu0/cpu1/arguments1、attempts0/cpuWrite1；修复后六例均为gpu1/cpu0/arguments0、attempts1/cpuWrite0。精确像素、typed RectF、一次结果装箱一致，未使用对象的native lookup/getter/conversion均为0。这里验证native阶段不读额外参数，不改变TJS调用前的实参表达式求值。

少一项时CPU/GPU入口均保留原 `TJS_E_BADPARAMCOUNT`、结果不变、零backend attempt/像素获取/更新；backend提交前拒绝时只有一次CPU续跑、一次更新和一次装箱。原提交后异常、record、lease、别名与资源边界回归继续通过。

## 验证与限制

Windows 11 / MinGW GCC 13.2；MetalLayer Release/Ninja；GPU为device-double，CPU oracle为真实plutovg，MSL为生产整数helper提取，精确容差未改变。

- MetalLayer CTest 6/6，MetalRenderBackend与TJS shutdown各1/1。
- 新增TJS兼容依赖的所有独立目标均构建并通过：TJSStackTrace 1/1、TJSNativeFactories 1/1、EmoteAnimation 5/5、EmoteMetadata 2/2。复用项目现有public-domain 7zip SHA-256源码，主App原已链接；ONLYCONSOLE及独立测试补齐C源，不新增hash算法或构建期生成步骤。
- 51项解析、frame-timing、point trace debug/release通过。
- 完整native Layer、LayerExBase、LayerExDraw生产TU syntax通过。
- 旧策略复现失败与修复后完整binary输出均保存，预期失败不作为最终回归失败。
- 一次单独解析运行因默认sandbox TEMP不可写失败，改用已有workspace TEMP/TMP后51项通过；未改fixture或测试断言。

命令/日志/三层HEAD/diff/源快照/哈希见 [manifest](native-metal-c2b-arity-baseline/manifest.json)。Apple原生kernel、default/forced compute、App与设备回归，以及天使33次arguments实际命中变化需要重建重采；日志没有旧调用的实际参数数量，不能仅凭本地复现断言那33次全部由额外参数造成。重采继续要求窗口完整对账、支持域目标无CPU像素获取/往返，保留具体其他拒绝原因。record/vectorSource继续原CPU域。

## 设置 direct 取证

用户已提供 `perf_log/Game/天使纷扰/data.xp3` 与直接提取的 `sysscn/hsvcpick.tjs`，授权使用本地GARbro。只静态提取四个相关文件到忽略的build目录，无游戏初始化执行，游戏全文/字节码不进入tracked测试或文档。用户直接文件与GARbro提取的TJS2 100字节码完全同源：35064 bytes，SHA256 `17f0154ed6f2640e40d5219f2757c2ab51c27dc042099bd3fc4fc1d8e9c69528`。

生产loader/disassembler确认 `OptionHSVPickerModule.updateColorSelect` 的ip206/218读取未限定的 `direct`，由回调绑定的选项对象this/global proxy查找，不是Layer/HSV原生API。四个目标脚本全集context/常量扫描没有direct声明或初始化；未选择颜色时getColor明确返回-1，进入这一分支。真实既有 `hsvDirectSysButton` getter会查询可选UI按钮，失败返回void；此前updateColorDirectSysButton已按该状态处理按钮隐藏。旧日志的后续Utils寄存器不属于故障接收对象。未发现通用VM缺失语义错误证据。

新兼容helper接入 `tTJSScriptCache::LoadByteCode`、生产loader之前：basename、完整SHA256、长度、父类/函数、334 code words/27 local constants、两处GPD的IP/receiver/结果/constant签名全部匹配才复制。受检解析并证明本地旧字符串槽仅被这两处读取使用；只将该槽的globalStringIndex映射到已存在的hsvDirectSysButton。不会新增direct属性或改全局StringArray/missing规则，不改opcode、跳转、源位置、文件长度或原输入，不新增GPU工作。软件SHA256使用实例内软件函数，无CPU能力探测或全局算法选择；成功才经原console通道输出 `compat.bytecode hsvcpick optionalDirectButton applied`，不输出源码或寄存器。失败/不匹配原字节码继续原路径，临时输出不残留。

原创CompileScript→真实VM回归验证：原缺失字段仍报错；局部映射后按钮隐藏、按钮缺失返回void、非负颜色分支保持，实例/global仍无direct，其他未知字段仍抛原异常。错误basename/长度/hash、类/函数/指令/receiver/local类型/字符串/额外constant使用都拒绝；结构漂移例重新计算fixture摘要，以独立验证结构守卫。固定生产规则不能接受synthetic指纹。三轮原始→修正→原始加载无污染，软件SHA向量通过。

真实用户文件仅静态核对：生产固定规则命中，副本只有offset32114的一个字节228→179变化；副本SHA256 `2bed2a724398f70e15f9a4daafca4332c871b4f65628207e1630e8a07646707b`，原文件仍为原摘要。生产反汇编仅两处名称注释改变，其余指令/操作数/跳转保持。`tjs-shutdown-tests --bytecode-static-compat <用户文件路径>` 在任何VM创建前完成同样核查，无TopLevel执行。所有真实游戏原文及私有反汇编仍只在ignored build。

Apple/App待验：匹配版本启动时确认上述applied标识；进入设置、无颜色/普通颜色选择、控件缺省、修改/退出及再次进入，验证按钮状态与保存行为；不再出现direct缺失。完整游戏初始化/交互没有在本机执行，不把静态补丁与原创VM回归当作设备闭环。App原VM dump过滤保持。其它发行版或已修改脚本摘要不匹配不会补丁，需另取证。
