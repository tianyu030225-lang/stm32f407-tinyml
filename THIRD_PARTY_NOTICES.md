# 第三方与发布范围

根目录 [MIT License](LICENSE) 适用于本公开包中项目作者有权授权的自有应用代码和项目配置，不改变外部依赖的许可。

## 随包提供

- App、BSP、Components 的应用实现。
- AI 应用接口及 `AI/vendor/ai_vendor_port.c` 适配逻辑；该文件调用公开 vendor API，不包含生成模型数据或分类算法库。
- USER 应用入口、项目配置及 Keil 工程配置。
- 两个 Python 主机工具和本包说明文档。

Keil 工程保留其工具生成标识。使用某个编译器、RTOS 或库的 API，并不表示这些第三方产品由本项目编写或重新授权。

## 不随包分发

| 类别 | 排除范围 | 使用方式 |
| --- | --- | --- |
| ST 库和模板、CMSIS | CORE、FWLIB，以及 USER 下 ST 设备/时钟/中断文件 | 自行从对应供应包获取，保留原版权与适用许可，并完成应用接入 |
| FreeRTOS 内核与端口 | FreeRTOS 整个目录 | 自行获取 V9.0.0，保留该版本 GPLv2 + FreeRTOS exception 等适用条款 |
| NanoEdge 生成及工具资产 | NanoEdgeAI.h、knowledge.h、libneai.a、原 ZIP、CLI、emulator、manifest 和性能资料 | 自行获取并按适用协议生成和使用；不继承为 MIT |
| 旧开发板例程 | SYSTEM、HARDWARE | 公共工程已移除无应用调用的旧组，不需要为公开版补回 |
| 本地材料 | 测试资产、数据集、构建/调试产物、编辑器索引、Git 历史 | 不作为本次源码发布内容 |

本包没有对被排除文件作“禁止任何分发”或“均为开源”的统一法律判断；采用外部依赖方式以明确发布范围。下载或自行再分发依赖时，应遵守其实际版本和授权条款。

获取入口及准确的目标目录见 [DEPENDENCIES.md](DEPENDENCIES.md)。不要把完整私有工程直接覆盖到公开仓库；否则可能重新带入模型、测试数据、受单独许可约束的源码或本机配置。
