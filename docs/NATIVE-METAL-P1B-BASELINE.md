# 原生 Metal P1B 实施与基线

日期：2026-10-06（Asia/Shanghai）。P1B 的 11 项 Photoshop 矩形方法已实现并完成本地 portable 验证；本轮原生 Metal、iOS 与真机回归待验。

## 开始前的远端检查

三层仓库先执行 `git fetch --no-recurse-submodules origin`，均成功。对 HEAD 与最新 origin/metal_dev 比较，结果都是 **ahead/behind=0/0，文件 diff=0**：

| 仓库 | 本地 HEAD = origin/metal_dev |
| --- | --- |
| main | `5e9c575c037fc701bcc3de6e1f08b93a9947e688` |
| runtime/host | `add9561548c834535bbd131b182879769342257e` |
| core | `1ee1e6de3a033e316e90882a5654a9278901a2d5` |

父仓库记录的两层子模块指针也与 checkout 一致。tracked 工作区干净，唯一已有未跟踪文件 `docs/KRKRSDL3-IMPLEMENTATION-GAP-AUDIT-2026-10-06.md` 保留，SHA-256 为 `ea0acae403f93732ee388b634394f7aeb4427624f76bf5837415dd8471fce54c`。本轮不编辑该文件，也不将其算作 P1B 新增文件。

没有 reset、merge 或同步覆盖操作。以下 P1B 改动发生在核对之后，当前尚未提交。

## 实现与支持域

新增 PsAlpha、PsAdd、PsSub、PsSoftLight、PsColorDodge、PsColorBurn、PsLighten、PsDarken、PsDiff、PsDiff5、PsExclusion，ID 37–47，Count=48。原 0–36 编号、方法对象及六对共享别名保持。能力审计为 **85 项清单 / 70 注册 / 70 描述符 / 0 软件描述符缺口 / 15 未注册**。

所有新方法使用实际软件 HDA 函数绑定为参照，保持源 alpha、opacity=255 分支、先后次序、饱和与位移取整；共用 `layerPsP1BPixel`。Dodge 先查表再按有效 alpha 插值，旧 Dodge5 先衰减源 RGB 再查表；Diff 后插值，Diff5 先衰减源再 difference。PsAlpha 的满源 alpha 仍用 denominator=256，不替换为复制。旧 PsScreen/PsColorDodge5 像素 helper 不变。

SoftLight、ColorDodge、ColorBurn 从 `TVPGL_C_Init()` 实际使用的静态表只读导出，不重算 pow 或换用另一套同名 `_c` 表。每张表按 source*256+destination 排列，分别位于 0、65536、131072，总长 196608 bytes。backend 持有不可变 MTLBuffer；相同内容复用，替换时旧已编码资源由命令保留。

ordinary tile、in-place compute、snapshot compute 均绑定 buffer(3)，非表方法使用已有 alpha buffer 占位。缺表或上传分配失败在编码/写目标前拒绝，并保留正确软件回退。上传次数/字节 `layerPsTableUploads/Bytes`、独立 `psTables` reject 经 session/C bridge/Swift 诊断贯通，图像与 Gamma 上传计数含义保持。

GPU 域为一个 RGBA 源、RGBA 目标、正向矩形及既有取样/裁剪范围。same-pixel 自别名可 GPU 执行；错位目标别名保留软件扫描线顺序。镜像与非法 32-bit 格式在写前明确拒绝；CPU pin/lease、backend 及资源失败保留安全软件执行。这里不扩展 affine/perspective/general triangles，不注册历史 tint/AlphaTest 功能。

## 实际本地验证

| 检查 | 结果 |
| --- | --- |
| MetalLayer Release build / CTest | 构建成功，**2/2 pass，0 skip**；5,491 次主 surface 比较、3 个 session |
| P1B 生产 MSL helper 提取为 C++ | **46,137,346 精确像素**，对照初始化后的实际 PS HDA 指针与表 |
| 原 P1A / UnivTrans / extended scalar | **21,757,036 / 3,145,728 / 1,835,008** 精确像素继续通过，容差未放宽 |
| MetalRenderBackend build / CTest | 构建成功，**1/1 pass，0 skip**；Windows 仅 diagnostics/software transfer |
| 能力与扩展 fixture | 70/70/0/15 与独立 TestKind48/Count49 通过 |

P1B scalar 覆盖完整通道对×alpha/opacity 边界、全部 alpha×opacity 的彩色样本、独立表端点/轴向、零强度/HDA和两组旧变体反例。manager/device double 验证 GPU 计数、驻留、ROI、统一源缩放/裁剪、别名、CPU 指针、一次表上传/重复供给，以及失败保留原资源/目标并准确记录 PsTables。

统一源取样用例用于分离路由与整数混合，不替代任意图像线性取样的 native 容差裁决。MetalLayer 在 Windows 使用同步 device double，nativeCompiled=false；这些结果不证明实际 Objective-C++/MSL 编译、真实纹理绑定、吞吐、功耗或发热改善。

## 证据与命令

- [修改前 Layer](native-metal-p1b-baseline/before-metal-layer.log) / [修改前 backend](native-metal-p1b-baseline/before-metal-render.log)：保存既有输出，原日志没有绑定本次三层 HEAD。
- [修改后 Layer](native-metal-p1b-baseline/after-metal-layer.log) / [修改后 backend](native-metal-p1b-baseline/after-metal-render.log)：本轮实际执行输出，保留原始字节。
- [能力 JSON](native-metal-p1b-baseline/capabilities.json) / [机器记录](native-metal-p1b-baseline/metadata.json)：版本、命令、参数资源、SHA-256、远端核对与未跟踪文件保留记录。

完整三层原始字节 patch 在本地 `build/native-metal-p1b-baseline`，包括 P1B 新文件，排除已有用户审计文档和 metadata 自身；`git apply --reverse --check` 只验证、不修改工作区。

```text
git fetch --no-recurse-submodules origin  # 分别在三层仓库执行
git rev-list --left-right --count HEAD...origin/metal_dev
git diff --name-status HEAD origin/metal_dev
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel 2
ctest --test-dir build/metal-layer-tests --output-on-failure
build/metal-layer-tests/metal-layer-tests.exe --audit-capabilities
cmake -S Tests/MetalRenderBackend -B build/metal-render-tests
cmake --build build/metal-render-tests --parallel 2
ctest --test-dir build/metal-render-tests --output-on-failure --verbose
```

## 待验项

macOS 默认 tile、强制 compute/snapshot compute 的生产 MSL 与真实像素/绑定/资源寿命；iOS device/simulator、framework/App/IPA 与 Swift/C 桥完整重建；新真机 PS 效果、透明/输入、资源切换、内存及稳定帧时间。C stats 尾部追加字段，宿主与 App 需一起重建。

全部 70 个名称有描述符不代表所有格式、参数、几何都能 GPU 执行，也不等于旧 GL 77 名称或 Kirikiroid 全兼容。
