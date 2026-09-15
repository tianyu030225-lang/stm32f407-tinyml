# STM32F407 TinyML 运动状态识别

基于 STM32F407、FreeRTOS 和 MPU6050 的应用源码：以 100 Hz 采集三轴加速度，将 128 点窗口用于数据导出或三类状态识别（STABLE、VIBRATION、IMPACT）。

**本仓库提供自有应用代码、工程配置和接入说明。第三方依赖与模型未随包提供，下载后不能直接构建完整固件。** 请先完成 [依赖获取与接入](DEPENDENCIES.md)。

## 包含内容

| 路径 | 内容 |
| --- | --- |
| `App/` | 采样、命令、AI 三任务，状态、队列、时间与串口输出 |
| `BSP/` | MPU6050、I2C、USART、LED 与蜂鸣器的应用侧驱动 |
| `Components/` | 环形缓冲区、命令解析和双窗口所有权 |
| `AI/ai_adapter.*` | 应用侧 AI 生命周期和输入接口 |
| `AI/vendor/ai_vendor_port.c` | 对第三方分类 API 的自有适配代码 |
| `USER/` | 应用入口、FreeRTOS/外设配置、Keil 工程 |
| `tools/` | 串口 CSV 采集和离线数据评估工具 |

ST 库与模板、CMSIS、FreeRTOS 内核、NanoEdge 生成文件、数据集、模型结果、调试配置、安装工具和构建产物均不在公开包中。具体范围见 [第三方与发布范围](THIRD_PARTY_NOTICES.md)。

## 应用流程

空闲窗口队列 → ImuTask 写入 128 组 XYZ → 就绪窗口队列 → AiTask 处理 → 归还空闲窗口。

CommandTask 解析串口命令，通过消息请求采集或推理状态切换。串口中断只接收字节并通知任务。AiTask 统一管理模型状态、识别结果、报警与窗口归还。

## 固件接入

参考目标为 Windows Keil MDK、ARMCC 5.06 update 6（build 750）、STM32F407ZE / Cortex-M4.fp。打开 `USER/Project.uvprojx` 前，请按依赖说明补齐文件并核对目标芯片、时钟与内存区域。

**中断接入是必做步骤：SysTick 必须驱动蜂鸣器周期更新。遗漏该更新后运行蜂鸣器节奏，PF8 可能持续保持激活；不要在接入未完成时运行板端。** 同时需要配置 USART1 转发、FreeRTOS 异常入口和故障时紧急关闭蜂鸣器，详见 [必需中断接入](DEPENDENCIES.md#必需中断接入)。

公共工程只移除了未被应用调用的旧 SYSTEM/HARDWARE 两组及对应包含路径；应用源码、任务参数和原工程的其余配置保留。没有替换 FreeRTOS 版本或切换编译器。

## 主机采集

需要 Python 3；串口采集额外依赖使用者自行安装的 `pyserial`：

```sh
python -m pip install pyserial
python tools/serial_capture.py --port COM5 --baud 115200 --output-dir data/stable-s1 --start-label STABLE --windows 40
```

上述命令会操作连接的串口设备，应在硬件接入检查完成后使用；串口名按实际环境设置。

主动采集需要匹配的 START 回执，校验数据标签与会话。Ctrl+C 或采满窗口时，仅对确认由本工具启动的采集尝试有界发送 STOP。未指定 `--start-label` 时只监听；每次使用新的输出目录。STOP 写出记录不等于已经收到设备停止确认。

采样协议为：

```text
D,<session_id>,<label>,<window_seq>,<sample_index>,<tick_ms>,<ax_raw>,<ay_raw>,<az_raw>
R,<window_seq>,<class>,<confidence_milli>,<latency_us>,<alarm_flags>
```

原始加速度配置为 ±4 g、8192 LSB/g。时间、轴序和量程应与实际固件一致；`confidence_milli` 是 0～1000 的整数。

离线工具不需要连接模型服务：

```sh
python tools/evaluate_model.py --help
```

训练与测试必须按独立 session 划分。没有匹配的真实数据和模型预测时，不报告模型准确率；随包没有原模型或性能结果。

## 验证范围

采集脚本保留已完成定向回归的修复。此公开包进行了文件归属筛选、源文件一致性、工程引用和文档链接检查；第三方依赖补齐后的完整 Keil 构建、板端运行和模型效果仍需使用者验证。公开包不提供可直接刷写的固件。

## 许可证

自有代码和项目配置采用 [MIT License](LICENSE)。第三方依赖继续受各自的原始许可约束，见 [第三方说明](THIRD_PARTY_NOTICES.md)。
