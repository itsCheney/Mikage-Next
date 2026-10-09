# P2C C2 剩余消费链：实施与验证

2026-10-09。初始三仓干净：main `3bc9c1ddf541875bceb0299ffeca212b81105d88`、Runtime `b6f31a2f892a0f4e20641471a6c361d2b899ac37`、Core `5e54d4678119c79848f6aab9d759550c8ff0a663`。本轮按 vectorSource、record、drawRectangle、clear 分批提交推送。范围承接 [CPU 消费链专项](NATIVE-METAL-P2C-C2-CPU-CONSUMERS.md)。

## 第一批：vectorSource 与成功 read 聚合

原生 vector GdipImage 的 drawImageStretch 通过原 drawImageAffine/draw/fill 生成有序 span，继续使用原矩阵、重复项、paint 顺序、背景和返回值。预检仅读取原生数据；脚本参数持有源直到同步采样完成，GPU 仅消费复制后的 packet，无新增 GPU 生命周期资源。借用 texture paint、非法几何及预算外输入继续具名 CPU 路由。捕获销毁同时恢复 calcTransform，拒绝后唯一 CPU 续跑。

成功 whole-texture read 在详情预算前进入 64 槽聚合，按 method/nativeEntry/access/origin 分组；八个已知消费者保护槽，其他槽供动态来源使用。容量与超长分别保留四指标 overflow；聚合/overflow/footer 使用独立 metal.cpuConsumerAggregate v2 和原 work windowID。旧 C struct、采样周期、HUD、详情字段不变。分析器按完整选定窗口解析，检查缺行/重复/代际/总账/C0，详情不重复加总；point-read 不在此覆盖域。

本地 MetalLayer 6/6、Backend 1/1、TJS shutdown 1/1、frame、point trace 与 C importer 通过。新增 24 组真实生产 vector 重放/拒绝/后续状态对照；71 read 跨纹理超过详情预算仍完整聚合，动态容量、超长和保护槽通过。解析新增完整聚合及缺行/重复/代际/旧日志测试。原精确像素容差不变。

原测试输出、命令、完整三层 HEAD、diff 哈希和源码快照位置见 [第一批 manifest](native-metal-c2-remainder-baseline/batch1/manifest.json)。初次 before 测试因系统 TEMP 沙箱权限失败；改用工作区 TEMP 后原 MetalLayer 6/6 通过，失败输出也保留，不能记作产品失败或原生验证。

## 待验与范围

Windows portable 使用原 plutovg、生产 NCBind/TJS、生产 shader 数学抽取及 GPU device double。Apple Objective-C++/MSL、Swift/App、default/forced compute、device/simulator、原生 submit/wait 及同设备/场景/热状态三次配对未验。历史日志保持原样，不宣称性能改善。record、drawRectangle、clear 尚待后续批次；必要 CPU 保存/alphaToProvince、saveDataPack 独立兼容、C3、P2D、Nekopara 不因本批签收。
