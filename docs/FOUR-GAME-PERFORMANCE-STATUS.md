# 四款游戏的图层性能修复

基线来自用户提供的四份 JSONL，共 4,803 条记录，应用提交 `a27de2445daf`。
记录混合了启动、设置、鉴赏、剧情与退出操作；以下数字取最后一条心跳的累计值，GB 使用十进制。

| 游戏 | CPU fallback | 回读 GB | 上传 GB | 最大 step ms |
| --- | ---: | ---: | ---: | ---: |
| 加缪 | 44 | 0.257 | 1.252 | 112 |
| 9nine | 66 | 0.396 | 1.313 | 1,104 |
| 千恋万花 | 21,418 | 1.469 | 1.549 | 4,415 |
| 天使纷扰 | 2,384 | 9.430 | 9.503 | 1,166 |

四次记录全程为 serious 热状态；这些数据不构成冷机功耗或降温对照。

## 第一批改动

- 普通 Layer Metal 支持 additive alpha、Photoshop multiply/overlay/hard-light 和 alpha 转预乘，包含 opacity、HDA 别名、引用图像及 COW。Overlay/hard-light 按软件实际使用的 `/255` 查表公式计算。错位的源目标自混合保留软件扫描线顺序。
- LayerExImage、LayerExDraw、LayerExRaster、LayerExAreaAverage、LayerExBTOA、shrinkCopy 及旧 LayerExBase 使用作用域像素访问，持有原纹理直到完成，提交实际已知写入区域。受控访问不永久 pin 纹理、不撤销脚本原始指针租约，也不把临时引用误当成 bitmap 共享。
- 新增 `layerWorkProfile` 区间记录，按纹理/调用来源聚合上传和同步回读，记录资源加载、AMV 解码、VM 执行、GC、compact、软件回退以及 AMV 解码帧数和 RGBA 字节。字段口径见 [诊断说明](DIAGNOSTICS.md)。
- 保持 60 FPS、现有 TJS 属性及模糊/灰度/gamma 的软件兼容路径。AMV 仍全帧解码；本批只补齐归因，没有实现按需解码。

第二批新日志确认普通混合回退已消失，但仍有 BTOA、灰度/模糊、scanline 和 AMV 加载问题。本批已实现针对修复，当前实现与证据见 [第二批排查与修复](FOLLOWUP-LAYER-PERFORMANCE.md)；上述软件模糊/灰度和全帧 AMV 描述仅代表第一批发布时的状态。

## 验证与剩余验收

便携测试直接编译生产 MSL 整数 helper，新增 1,048,576 组与 tvpgl 的逐像素对照；并验证裁剪/缩放/别名、alpha cache、COW、局部上传、原始指针共存、异常释放和有界诊断/C 桥接。便携设备替身不能替代实际 Metal shader、纹理排序或功耗测试。

在 Apple 环境运行现有 `Tests/MetalLayer` 原生测试及完整 iOS framework/App 构建，再分别复测四款游戏的启动、设置、鉴赏与剧情。重点比较：

1. 天使纷扰的 additive-alpha 回退及设置/鉴赏页面的目标回读；区间位置以操作对照为准。
2. 千恋万花的 Overlay 回退及独立的 scanline/native 像素访问来源。
3. 天使纷扰上传高峰的 `lease`、纹理 ID、来源和字节；插件结束后未修改图像不应再次上传。
4. AMV 打开期间的 decode/resource 时长、解码 payload 与 resident memory，以及剩余 VM/GC/compact 尖峰。

从相同正常热状态、亮度和充电条件开始，分别记录各场景；退出确认和保存异常单独分析。实际发热改善需通过 Instruments 和持续真机对照确认。

引擎改动跨 build 子模块和嵌套 cpp 子模块。发布干净检出的构建时，需先提交发布 cpp，再更新 build 子模块指针，最后更新应用指针；只提交根仓库不会包含未提交的引擎修改。
