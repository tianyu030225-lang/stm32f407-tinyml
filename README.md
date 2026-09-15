# STM32F407 TinyML 运动状态识别

用 STM32F407 和 MPU6050 采集三轴加速度，在 FreeRTOS 下完成数据导出和运动状态识别。采样频率为 100 Hz，每个窗口包含 128 组 XYZ 数据，识别类别为静止（STABLE）、振动（VIBRATION）和冲击（IMPACT）。

仓库包含应用源码、Keil 工程和 Python 采集工具。**ST 标准外设库、CMSIS、FreeRTOS 内核和 NanoEdge 模型需要自行准备，克隆后还不能直接编译固件。** 文件放置位置和接入步骤见 [DEPENDENCIES.md](DEPENDENCIES.md)。

## 硬件与开发环境

- 目标芯片：STM32F407ZE，外部晶振 8 MHz，系统时钟配置为 168 MHz。
- 传感器：MPU6050，通过 I2C1 的 PB8/PB9 连接。
- 串口：USART1，PA9/PA10，默认 115200 波特率。
- 指示灯：PF9；蜂鸣器：PF8，高电平有效。
- 工程入口：`USER/Project.uvprojx`，原工具链为 Windows Keil MDK、ARMCC 5.06 update 6（build 750）。

引脚、时钟和内存配置需要与实际板卡核对。**蜂鸣器依赖 SysTick 中的周期更新来结束短鸣，漏接后 PF8 可能持续激活。** 补齐库文件后还要完成 [中断接入](DEPENDENCIES.md#必需中断接入)，再进行板端测试。

## 代码结构

| 目录 | 内容 |
| --- | --- |
| `App/` | 采样、命令和 AI 任务，以及状态、队列、时间和串口输出 |
| `BSP/` | MPU6050、I2C、USART、LED 和蜂鸣器驱动 |
| `Components/` | 环形缓冲区、命令解析和双窗口管理 |
| `AI/` | AI 状态和输入接口，NanoEdge 分类 API 的适配层 |
| `USER/` | 程序入口、FreeRTOS/外设配置和 Keil 工程 |
| `tools/` | 串口 CSV 采集和离线数据评估脚本 |

ImuTask 从空闲队列取出窗口，填满 128 组数据后交给 AiTask。AiTask 完成导出或推理后归还窗口，同时管理模型状态、识别结果和报警。CommandTask 处理串口命令，请求切换采集或推理状态；串口中断只接收字节并通知任务。

## 开始使用

1. 按 [依赖说明](DEPENDENCIES.md) 补齐 ST/CMSIS、FreeRTOS 和中断实现，核对工程参数。
2. 准备与适配层匹配的 NanoEdge 库和模型。如果只需要采样和导出，可按 [暂不接入模型时](DEPENDENCIES.md#暂不接入模型时) 关闭推理。
3. 在 Keil 中打开 `USER/Project.uvprojx`，完成编译、链接和硬件接线检查，再进行板端验证。

### 串口采集

主机需要 Python 3 和 `pyserial`。下面的命令会打开串口并向设备发送采集命令，请先完成固件接入和硬件检查，把 `COM5` 换成实际串口：

```sh
python -m pip install pyserial
python tools/serial_capture.py --port COM5 --baud 115200 --output-dir data/stable-s1 --start-label STABLE --windows 40
```

每次采集使用新的输出目录。指定 `--start-label` 后，脚本等待匹配的 START 回执，再校验数据标签和 session；Ctrl+C 或采满窗口时，只对本次确认启动的采集尝试发送 STOP，并限制等待时间。STOP 已写出不代表设备已确认停止。不指定 `--start-label` 时，脚本只监听，不发送 START 或 STOP。

固件输出两种数据行：

```text
D,<session_id>,<label>,<window_seq>,<sample_index>,<tick_ms>,<ax_raw>,<ay_raw>,<az_raw>
R,<window_seq>,<class>,<confidence_milli>,<latency_us>,<alarm_flags>
```

`D` 行是采样数据，原始加速度量程为 ±4 g、8192 LSB/g；`R` 行是识别结果，`confidence_milli` 为 0～1000 的整数。解析数据或接入模型时，时间、轴序和量程都要与固件保持一致。

### 离线评估

查看本地评估脚本的参数：

```sh
python tools/evaluate_model.py --help
```

脚本不需要连接模型服务。训练和测试数据应来自独立的 session；模型准确率需要用真实测试数据和对应预测结果计算。仓库没有附带原模型、数据集或性能结果。

## 测试状态

串口采集脚本的定向回归测试已通过。补齐第三方依赖后的完整 Keil 构建、板端运行和模型效果尚未验证，仓库也没有提供可直接刷写的固件。

## 许可证

应用代码和项目配置采用 [MIT License](LICENSE)。外部依赖按各自的许可使用，范围见 [第三方说明](THIRD_PARTY_NOTICES.md)。
