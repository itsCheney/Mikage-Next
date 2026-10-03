# Emote integration 首轮实现记录

日期：2026-10-03。三个仓库均使用 `emote_dev`；参考仓库位于忽略的
`third_party/AetherKrkr`，固定为 `fa0f8af9614865aebcb839abd32bc181643d1e4a`。

当前子模块提交：core `c9a5938`，build fork `7dc68c9`。三个仓库的 `emote_dev`
作为同步发布分支，按 core → build → app 的顺序推送，保证递归 checkout 能取得
固定的子模块版本。当前版本另加入 App 设置开关及下次启动生效的宿主接口。

## 已实现

- 新增独立 C++17 动画核心，定义和可变状态分离；支持独立 timeline、起始段、
  越界保留、非循环结束、变量时长/easing、替换/排队、加权差分及淡入淡出。
- ordinary Emote/Motion.Player 和 D3D 包装层接入可选参数，新增 `queuing` 属性。
  原有两参数 `setVariable` 调用仍有效，D3D 保留帧数入口。
- 集成模式默认按 60 Hz authored frames 推进，仍以 20 为 speed divisor 的默认值。
  legacy 模式保留原有 50 tick/s 换算。集成模式 transition/tickCount 使用 authored
  frames；这些单位差异需要用游戏脚本和官方参考确认，尚未认定是通用官方契约。
- 变量、selector 派生输出和 blink 状态属于 player；当前 motion 参数读取优先使用
  实例值，保留原始资源默认值作为回退。作用域标签缓存避免每帧扫描整个资源树。
- 主 motion 的结束仍能解除 `.playing/.animating` 等待，控制 timeline 可继续推进。
  显式暂停与自动结束分开处理，draw 和命中查询不推进控制时钟。
- 增加版本化状态快照、资源定义指纹、长度/数值/队列验证、UTF-8/NUL 标签编码，
  保存 blink 相位。旧的仅含变换的状态仍可读取。D3D clone 复制未完成动画，并
  避免恢复后逐变量赋值清空队列。
- 诊断模式下，每个 player 保留最近 128 次 progress 的固定容量缓冲：输入时间、
  控制时钟、主 motion 状态、progress/draw 版本，以及最多八个变量和 timeline
  时间样本。每秒最多输出一条摘要；不增加 GPU readback 或提交。
- CI 覆盖 `emote_dev`，加入生产核心与真实 TJS/ncbind 测试。框架构建配置加入来源
  通知和 GPL 文本，包装行为待 Apple 构建核验。

## 如何选择候选模式

App 中使用 **设置 → 动画 → Emote 候选动画模式**。默认关闭；开启后保存偏好，
下次启动游戏使用候选模式。关闭后下次启动恢复 legacy，正在运行的游戏不切换。
宿主每次启动显式覆盖 SDL hint，因此不会残留上一局模式，也不受调试环境变量
覆盖。启动诊断记录实际选择的模式。

不经过 App 宿主的独立 runtime 可在创建 player 前使用 SDL hint 或环境变量：

```text
MIKAGE_EMOTE_ANIMATION_MODE=integrated
```

环境/hint 在 player 构造时读取；既有实例不会随环境值改变而切换。
App 中做 A/B 对比时通过开关选择后重新启动游戏会话。
诊断沿用应用的诊断开关（宿主设置 `MIKAGE_METAL_DIAGNOSTICS`）。

该开关控制动画行为；新代码仍编译入实验 runtime，不能通过关闭开关免除其许可
条件。GPL 来源、完整文本和分发核验要求见
[来源通知](../Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/AETHER-NOTICE.md)。

## 验证证据

变更前的生产函数抽取夹具在不经过 GPU 的情况下复现 5/5 问题：非循环起播越界、
启动另一条 timeline 重置、停止另一条 timeline 重置、短循环重置长循环、淡出立即
停止。这证明存在动画缺陷，不能证明实际游戏卡顿的全部原因。

新增测试说明见 [EmoteAnimation](../Tests/EmoteAnimation/README.md)。Release 下通过
59 项生产核心检查、32 项生产适配器/TJS 检查，以及 60/42 Hz 合成轨迹在 61 个
共同时间点的对照。测试不等同于整个 Windows/iOS runtime 构建。
新增开关另通过 14 项生产宿主接口/模式选择检查，覆盖默认、开启、关闭、下一局
生效、现有实例隔离及 hint 设置失败；App 偏好持久化、旧配置迁移、启动快照的
XCTest 已加入模拟器 CI，当前 Windows 环境不能执行 UIKit 测试。
既有 PSB 解析/加载、资源缓存、帧统计、会话退出、TJS shutdown、原生工厂、脚本栈
和 LayerInput 回归也作为此次接入检查。

## 尚待验收

1. 缺少受测真实 PSB 和官方 PC 轨迹：节点 frame type、`ccc/zcc/mesh.cc` 等曲线保持
   当前实现，尚未开展 M3 的语义替换；完整 PSB 元数据导出与逐资源对照未完成。
2. 环形缓冲是有限前缀样本与调用版本，不是全部变量、真实显示帧或自动根因分类。
   `--trajectory` 仅导出合成控制案例，不能冒充真实角色轨迹。
3. 未实现 AetherInternal 私有物理/native SDK，也未替换 Metal 渲染。
4. 当前 Windows 环境未完成 Apple framework、原生 Metal 或 iPhone 验收，尚不能
   承诺卡顿消失或启用候选为默认。条件具备后按原计划进行同场景测试。
5. 已保留 GPL 来源及通知；combined runtime/App 的分发兼容性仍需核验。

实验中发现回归时，保持 legacy 默认并记录资源/调用序列；不在同一会话中途自动
回退，以免把两个状态模型拼接成新的动画跳变。
