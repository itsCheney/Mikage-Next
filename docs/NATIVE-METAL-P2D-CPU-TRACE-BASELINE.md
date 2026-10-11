# P2D CPU 续查：场景 PSB 路径与慢调用保留

2026-10-11。从 Root `8e3b744`、Runtime `f58097d`、Core `e191359` 续接，保留已有 After_P2D_2 分析文档改动。本包只补归因与回归，不提交、不推送，不宣称 CPU 长帧已解决。

## 找到的原生入口及证据边界

After_P2D_2 已确认 `KAGEnvPlayer.tjs` 的 `clearStorage/initStorage/loadScene` 和 `StorageData` 构造耗时数百毫秒，整step最高约0.9–1.1s。这些是游戏脚本函数，不能直接归因到三套原生 KAG parser。

在 ignored `build/c3-f63-cpu-analysis/game-scripts/KAGEnvPlayer.tjs` 找到旧 DRACU 提取副本：StorageData 检查 `.scn`，创建 `PSBFile`，遍历 `data.root.scenes` 建sceneMap；clearStorage/finalize会invalidate并释放旧对象。该副本SHA256与旧提取证据一致，但本次日志把构造函数列在独立StorageData.tjs，文件组织已不同，且当前资源包不可核验。因此这是源码机制线索，尚不是本次PSB耗时占比证明。

当前原生 `plugins/psbfile/PSBFile.cpp` 确实执行存储open、完整文件读取/MDF解压、表解析，以及 `readAllObjs` 递归构建完整TJS对象树。旧CPU的script.storage只覆盖TJS脚本存储执行，不覆盖这些PSB操作。显式GC指标也不覆盖所有引用计数同步释放。两者为0不能排除PSB工作。

PSB Invalidate释放stream而保留_root。普通owner最终删除时，native Destruct的delete this会触发_root成员析构，再递归释放TJS字典/数组/字符串。只有把计时放在delete this外，才能覆盖成员析构；析构函数体内的临时计时器会过早结束。全局强制TJS teardown可能直接delete native实例而绕过Destruct，本包不将其归入已覆盖的普通owner释放路径。

第二轮排除已验证：若游戏仍持有root或子节点，树的最后释放可以发生在PSB实例删除之后，psb.destroy小不能排除一般对象释放。字体/音频/影片初始化、同步脚本扫描与诊断扰动也有独立证据，详见 [其它原因复核](NATIVE-METAL-P2D-CPU-OTHER-CAUSES.md)。retained-root生产回归已加入PSB测试。

## 生产改动

PSB增加九类CPU scope：`psb.load/open/read/decompress/copy/tables/materialize/invalidate/destroy`。

- load覆盖loadFromStream，open单独覆盖已有TVPCreateStream；不重复套同kind的外层load。
- read包含已有archive/filter/stream工作；不能称纯磁盘IO。MDF decompress只包已有zlib调用，copy包解压后内存stream分配和拷贝。
- tables覆盖现有header/name/string/chunk表解析。materialize只包最外一次_root构建，destroy只包普通native实例delete；不在每个对象节点、PropSet或Release热路径安装计时。
- 调用、异常、失败返回和所有权顺序保持；只标记已有失败，不重试，不新增读/submit/wait或资源引用。字节数是各scope已有工作量，彼此可能嵌套，不能求和当作物理IO流量。

当前重点为字符串路径场景加载；octet构造在进入loadFromStream之前的前置内存拷贝未单独计时，不能将本包当作所有PSB入口的完整分解。

CPU日志升级为v2，Swift/C profile ABI不变。固定保留窗口中最慢两步，每步最多四角色：step、最慢外层VM、最慢PSB阶段、其它最慢子调用。先在当前step用四个固定候选位置保存，再在step完成时选择窗口候选，避免迟到重step的原生阶段先被窗口淘汰。嵌套host step各有本地候选，未启用诊断时不构造候选缓冲；无堆分配或TJS强引用。

栈在原调用返回/异常展开前复制，窗口输出时不查VM。新增scopeID/parentScopeID、startWindow、childWallNS/residualWallNS、bytes及scope内vmCalls；callID仍为所属VM调用，原生阶段用独立scopeID区分。residual仅扣掉已仪器化的直接子scope，剩余仍可包含未测原生调用和等待，不能叫纯VM CPU。vmCalls复用已有序号，只表示调用数量而非指令数或CPU时间。

保持每窗口8条、每step4条、四层/512-byte栈、详情滚动8KiB/s和960-byte消息上限。替换/淘汰、字节、格式及回调drop分别对账；窗口中途重入会先归档已完成子scope，跨窗调用显式标记。转义显示截断、缺祖先、缺行和未知关联保持不完整。退出未flush尾窗仍不补算。诊断成本累计，尚须设备开关配对测实际扰动。

解析器兼容v1/v2；v1明确`nativePSBAvailable=false`、`residualWallAvailable=false`，不能将旧日志缺失PSB字段读作零。v2核对21种聚合、scope身份/父序、角色上限、残余wall和drop原因，缺失祖先单列而不猜映射。

## 已完成验证

- Backend portable **4/4**：CPU真实header、晚出现重step保留其先完成原生阶段、外层保留、嵌套/异常/代际/线程、跨窗、回调重入、UTF-8转义/字节上限，以及v1/v2 emitter→parser。
- Release TJSStackTrace **1/1**：实际VM与生产host捕获回调；在正常返回和异常后销毁VM/tracer，再输出候选，确认step、外层VM与原生阶段栈仍有效。
- 生产PSBFile **1/1**：完整真实PSB/TJS TU，存储边界替身、精确提取的生产memory stream，合成普通PSB/MDF；诊断on/off逐项内容、读次数、所有权/finalizer顺序、失败/异常及成员树销毁覆盖一致。
- TJS shutdown/reentrant **1/1**、native factory **1/1**。CPU emitter→parser含8个用例；After_P2D_2三文件v1解析回归通过，均未伪造PSB成本。
- MetalLayer portable **9/9**通过。首次运行的临时目录权限错误通过把TEMP/TMP指向工作区修复，未修改生产行为或跳过用例。

PSB测试已接入iOS workflow前置阶段；Windows结果不代表Apple重跑通过。设备IO、当前游戏脚本版本、PSB阶段实际占比和CPU性能保持待验。本包没有修改游戏脚本、缓存执行结果、扩大画质/绘制裁剪或强制yield。

## 下一次重放的裁决

2026-10-11已收到 `After_P2D_3` / Root `302c7bc21718`，五游戏CPU v2/PSB/vmCalls设备命中，两个解析器均通过。PSB materialize及同步destroy已确认重成本；千恋冷UI窗口还确认67次imageOpen累计约1.42s，天使有独立host事件长帧，9-nine重窗口有12次PSB加载，NEKOPARA有Emote慢资源加载。原生完整矩阵和配对性能保持待验；下述文字保留原重放方法，最新裁决与下一包优先级见 [After_P2D_3设备分析](NATIVE-METAL-P2D-3-DEVICE-ANALYSIS.md)。

待本包进入通过CI的新包后，用同一存档和输入重放天使skipToStop及千恋/DRACU场景跳转。提供完整日志即可，优先等待nominal，保留各热阶段。

若重step的PSB materialize或destroy占比高，下一包针对已经测到的对象树构建/同步释放开销；若open/read/decompress高，沿archive/filter/MDF继续分解；若这些阶段都很小，则依据保留下来的外层VM、子调用与残余wall追其它原生入口或脚本循环。不会依据旧脚本或“缓存已有命中”提前宣布某个优化能解决长帧。
