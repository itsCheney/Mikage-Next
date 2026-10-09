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

## 待验与范围

Windows portable 使用原 plutovg、生产 NCBind/TJS、生产 shader 数学抽取及 GPU device double。Apple Objective-C++/MSL、Swift/App、default/forced compute、device/simulator、原生 submit/wait 及同设备/场景/热状态三次配对未验。历史日志保持原样，不宣称性能改善。record、drawRectangle、clear 尚待后续批次；必要 CPU 保存/alphaToProvince、saveDataPack 独立兼容、C3、P2D、Nekopara 不因本批签收。
