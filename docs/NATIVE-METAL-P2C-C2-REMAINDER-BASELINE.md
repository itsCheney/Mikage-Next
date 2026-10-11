# P2C C2 剩余消费链：实施与验证

2026-10-09。初始三仓干净：main `3bc9c1ddf541875bceb0299ffeca212b81105d88`、Runtime `b6f31a2f892a0f4e20641471a6c361d2b899ac37`、Core `5e54d4678119c79848f6aab9d759550c8ff0a663`。本轮按 vectorSource、record、drawRectangle、clear 分批提交推送。范围承接 [CPU 消费链专项](NATIVE-METAL-P2C-C2-CPU-CONSUMERS.md)。

## 第一批：vectorSource 与成功 read 聚合

原生 vector GdipImage 的 drawImageStretch 通过原 drawImageAffine/draw/fill 生成有序 span，继续使用原矩阵、重复项、paint 顺序、背景和返回值。预检仅读取原生数据；脚本参数持有源直到同步采样完成，GPU 仅消费复制后的 packet，无新增 GPU 生命周期资源。借用 texture paint、非法几何及预算外输入继续具名 CPU 路由。捕获销毁同时恢复 calcTransform，拒绝后唯一 CPU 续跑。

成功 whole-texture read 在详情预算前进入 64 槽聚合，按 method/nativeEntry/access/origin 分组；八个已知消费者保护槽，其他槽供动态来源使用。容量与超长分别保留四指标 overflow；聚合/overflow/footer 使用独立 metal.cpuConsumerAggregate v2 和原 work windowID。旧 C struct、采样周期、HUD、详情字段不变。分析器按完整选定窗口解析，检查缺行/重复/代际/总账/C0，详情不重复加总；point-read 不在此覆盖域。

本地 MetalLayer 6/6、Backend 1/1、TJS shutdown 1/1、frame、point trace 与 C importer 通过。新增 24 组真实生产 vector 重放/拒绝/后续状态对照；71 read 跨纹理超过详情预算仍完整聚合，动态容量、超长和保护槽通过。解析新增完整聚合及缺行/重复/代际/旧日志测试。原精确像素容差不变。

原测试输出、命令、完整三层 HEAD、diff 哈希和源码快照位置见 [第一批 manifest](native-metal-c2-remainder-baseline/batch1/manifest.json)。初次 before 测试因系统 TEMP 沙箱权限失败；改用工作区 TEMP 后原 MetalLayer 6/6 通过，失败输出也保留，不能记作产品失败或原生验证。

## 第二批：record 事务

原 drawLine/drawPath/drawImageStretch 的 record 域通过原预检后允许 capture。_drawPath 每个有效 drawInfo 仍追加完整 Appearance 和 path，保留重复录制及 transform；新增项暂存，Prepare 完成预算与目标 vector 容量准备，成功后无分配发布。提交前拒绝销毁暂存再唯一 CPU 续跑。span facade 增加可选 committed 输出，提交后异常也发布录制状态再传播，不重复写目标或录制。纯普通 transform 在 record 模式只更新矩阵；view transform、导出、重绘、保存仍保留原同步像素副作用。

Appearance/brush/pen 克隆增加异常清理；plutovg overlay 增加独立 checked path clone，精确分配元素数组，失败返回 null。记录克隆、暂存和目标扩容与请求的 span/scratch 一起受64 MiB检查；原backend实际staging预算继续生效。原栅格化算法不变。

真实绑定覆盖成功、后端拒绝、checked clone 失败、提交后异常、装箱异常；精确像素和导出命令/重复项/变换一致，导出后再次 vector 重放与原 CPU 相同。原测试中普通 record transform 的“必须 acquire”断言改为本批明确要求的纯元数据零 acquire，更新副作用保持。证据见 [第二批 manifest](native-metal-c2-remainder-baseline/batch2/manifest.json)。

## 第三批：drawRectangle 与五入口路由

drawRectangle 使用原 path_add_rect/_drawPath，复用原 paint、历史 Clip、录制事务、最小参数契约及一次 RectF 装箱。真实生产绑定加入该方法；原诊断开关/额外参数/拒绝续跑/像素对照同时覆盖四个绘制入口，另有24组变换/裁剪/record精确矩形对照。

metal.layerSpan 输出v3：五个固定方法、15个方法/路由保护位＋17个原因位，共32代表；clear 在第四批接入。所有方法/路由/原因聚合仍完整、有界，旧v1/v2分析兼容；v3按本版本保护位验证，混合版本或代际缺口显式报错。证据见 [第三批 manifest](native-metal-c2-remainder-baseline/batch3/manifest.json)。

## 第四批：clear 完整覆盖与最终验证

独立只读 facade 检查原生同会话 RGBA 与 CPU 租约，不获取GPU句柄。原clear在capture canvas上仍执行原SRC颜色转换/栅格化；准备后的每行span必须连续覆盖整个image、无缺口/重叠、SolidSource且coverage=255，才获取CPU overwrite租约并写入原span的premultiplied像素。部分/历史Clip、逃逸alias、活动lease和不能证明的域保留原CPU。record替换预先暂存，成功后发布；原无条件Update、ImageModified和void结果清空顺序保持。已有对象结果可能析构重入，具名resultObject保守回退。

新增45组颜色/alpha/Clip/COW/record/lease/分配与更新故障对照；另验逃逸image同步可见性、对象结果释放、整链诊断开关。74次生产绑定序列（原71调用＋rectangle＋clear＋vector）得到73 GPU＋1 CPU fullOverwrite，无目标旧像素acquire；mock验证不等同原生总线性能。真实facade/device double单独验证overwrite无readback/提前upload、alpha cache失效及一次180bytes必要延迟输入上传；探针无backend调用由源码确认，原生submit/wait断言在Apple分支待运行。

最终 MetalLayer **7/7**、Backend **1/1**、TJS shutdown **1/1**；解析 **56项**、frame、point debug/release、C11 importer/App接线及四份生产TU syntax通过，零本地skip。新CTest编译生产emitter，以真实v3五方法75条路由、71条成功read/32条详情、空窗口和C0精确对账喂给生产分析器。源数据详情/总量不重复计数；聚合完整、名字容量和真实unknown归因分别报告。证据见 [第四批 manifest](native-metal-c2-remainder-baseline/batch4/manifest.json)。

支持域的参数/span/scratch仅为原GPU合成输入，CPU clear overwrite单列为初始化；不计成GPU命中，也不新增GPU clear随后强制回读。getRecordImage/redrawRecord/saveRecord保留原同步CPU副作用，原内部重绘/保存仍可成为后续具名消费者，不能据选定绘制链测试宣称整款游戏零回读；设备重采须覆盖这些接口，核对是否转移了等待。

## 必要 CPU 边界源码核对

`plugins/LayerExBTOA.cpp::copyAlphaToProvince` 读取完整 imageWidth/imageHeight，不使用Layer ClipRect；逐RGBA像素取第4字节写省图。threshold<0复制原alpha，0..255写比较结果0/1，>=256写0。即使后一分支不消费alpha，原入口仍先校验/获取源和省图，错误顺序本轮保留。仅增加准确的copyAlphaToProvince诊断scope及保护槽，算法不变；同源版本的重复读复用原LayerTexture valid cache，GPU/CPU写入沿用既有失效。

`SaveLayerImage → TVPSaveImage → PNG handler` 同步传递完整MainImage及元数据；PNG32逐行读取RGBA，PNG24去除alpha，不改用Clip ROI。源未变化时后续scanline复用原CPU cache；保存完成或异常原样返回，本轮不改变格式、路径、编码或跨会话资源释放。现有真实facade缓存/失效/诊断回归覆盖底层，但不充当新的设备保存文件或重启恢复验收。

## 待验与范围

Windows portable 使用原 plutovg、生产 NCBind/TJS、生产 shader 数学抽取及 GPU device double。本轮四批选定支持域本地实现/验证完成。Apple Objective-C++/MSL、Swift/App、default/forced compute、device/simulator、原生 submit/wait 及同设备/场景/热状态三次配对未验。历史日志保持原样，不宣称性能改善。必要 CPU 保存/alphaToProvince、保守record重绘等更广消费域、saveDataPack 独立兼容、C3、P2D、Nekopara 不因本批签收。
