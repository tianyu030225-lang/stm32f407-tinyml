# 第三方说明

仓库中的应用代码和项目配置采用 [MIT License](LICENSE)。下面列出的外部依赖没有随仓库分发，也不适用本项目的 MIT 许可。

## 仓库里的代码

- `App/`、`BSP/`、`Components/`：任务、驱动和数据管理。
- `AI/`：应用接口和 `AI/vendor/ai_vendor_port.c` 中的 API 适配逻辑，不包含 NanoEdge 生成的模型数据或分类算法库。
- `USER/`：程序入口、FreeRTOS/外设配置和 Keil 工程。工程文件保留了 Keil 的工具生成标识。
- `tools/`：两个 Python 主机工具。

## 需要自行准备的依赖

| 依赖 | 文件范围 | 许可与接入 |
| --- | --- | --- |
| ST 标准外设库、启动模板和 CMSIS | `CORE/`、`FWLIB/`，以及 `USER/` 下的 ST 设备、时钟和中断文件 | 从对应版本的供应包获取，保留原版权与许可，按依赖说明接入 |
| FreeRTOS V9.0.0 | `FreeRTOS/` 内核和端口 | 保留该版本的 GPLv2 + FreeRTOS exception 等适用条款 |
| NanoEdge AI | `NanoEdgeAI.h`、`knowledge.h`、`libneai.a`，以及生成包、CLI、emulator、manifest 和性能资料 | 按 ST 的适用协议获取、生成和使用 |

具体版本、下载入口和文件放置位置见 [DEPENDENCIES.md](DEPENDENCIES.md)。依赖的下载和再分发均以实际版本的授权条款为准。

旧开发板例程 `SYSTEM/`、`HARDWARE/` 未被应用调用，已从工程中移除，无需补回。原项目的测试数据、模型结果、构建和调试产物、编辑器索引及旧 Git 历史也没有随仓库发布。

补齐依赖时请按文档逐项放置文件。直接用完整本地工程覆盖仓库，可能把模型、测试数据、受单独许可约束的源码或本机配置一并带入提交。
