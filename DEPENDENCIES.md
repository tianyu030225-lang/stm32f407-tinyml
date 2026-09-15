# 依赖获取与接入

先准备下面的库和工具，再打开 `USER/Project.uvprojx`。第三方源码、模型和工具链没有放在仓库中；对应的本地目录已加入 `.gitignore`。

## 工具链与工程参数

工程保留以下配置。更换 MDK、Device Pack 或编译器时，需要重新确认兼容性。

| 项目 | 配置 |
| --- | --- |
| 工程入口 | `USER/Project.uvprojx` |
| 编译器 | ARMCC 5.06 update 6（build 750），`uAC6=0` |
| 目标 | STM32F407ZE，Cortex-M4.fp，Keil.STM32F4xx_DFP.1.0.8 |
| 预定义 | `STM32F40_41xxx`、`USE_STDPERIPH_DRIVER`、`__FPU_USED=1`、`HSE_VALUE=8000000`、`AI_ADAPTER_ENABLE_NANOEDGE=1` |
| FreeRTOS | Tick 1000 Hz，堆 75 KiB，`heap_4.c`，RVDS/ARM_CM4F 端口 |
| IROM | `0x08000000 + 0x80000` |
| IRAM | `0x20000000 + 0x20000` |
| IRAM2 | `0x10000000 + 0x10000` |

时钟使用 HSE 8 MHz，`PLL_M=8`、`PLL_N=336`、`PLL_P=2`、`PLL_Q=7`，目标频率为 168 MHz。补入 `system_stm32f4xx.c` 时，要检查对应分支的 PLL 参数和 `SystemCoreClock`；只修改 `HSE_VALUE` 不足以完成时钟配置。还需核对实际晶振、芯片容量和链接设置。

FreeRTOS 只保留一份内存分配实现，不要同时编译 `heap_4.c` 和其他 `heap_*.c`。

## ST 标准外设库与 CMSIS

从 [STSW-STM32065](https://www.st.com/en/embedded-software/stsw-stm32065.html) 获取库文件。项目使用标准外设库接口，不是 STM32 HAL；原库文件的版本为 ST 标准库 V1.4.0、CMSIS Core V3.20。下载时核对版本和许可，使用其他版本需要重新验证。

| 本地目标位置 | 需补齐的内容 |
| --- | --- |
| `FWLIB/STM32F4xx_StdPeriph_Driver/inc/` | 标准库头文件，包括 misc、GPIO、RCC、SYSCFG、USART、I2C |
| `FWLIB/STM32F4xx_StdPeriph_Driver/src/` | `misc.c`、`stm32f4xx_gpio.c`、`stm32f4xx_rcc.c`、`stm32f4xx_syscfg.c`、`stm32f4xx_usart.c`、`stm32f4xx_i2c.c` |
| `CORE/` | `core_cm4.h`、`core_cm4_simd.h`、`core_cmFunc.h`、`core_cmInstr.h`，以及 ARMCC 启动文件 `startup_stm32f40_41xxx.s` |
| `USER/` | `stm32f4xx.h`、`system_stm32f4xx.h`、`system_stm32f4xx.c`，以及按下节接入的 `stm32f4xx_it.c/h` |

供应包内部目录可能随版本变化，以文件名、版本和目标 CPU 为准。启动文件和 ST/CMSIS 实现负责向量表、`SystemInit`、时钟树和 `SystemCoreClock`。

## FreeRTOS V9.0.0

使用 [FreeRTOS V9.0.0 源码](https://github.com/FreeRTOS/FreeRTOS/tree/V9.0.0/FreeRTOS/Source)，保留该版本的 [许可和例外条款](https://github.com/FreeRTOS/FreeRTOS/blob/V9.0.0/FreeRTOS/License/license.txt)。V9.0.0 的许可不能用新版 FreeRTOS 的 MIT 许可替代。

| 上游位置（相对于 FreeRTOS/Source） | 本地目标位置 |
| --- | --- |
| `include/` | `FreeRTOS/include/` |
| `croutine.c`、`event_groups.c`、`list.c`、`queue.c`、`tasks.c`、`timers.c` | `FreeRTOS/src/` |
| `portable/RVDS/ARM_CM4F/port.c`、`portmacro.h` | `FreeRTOS/port/RVDS/ARM_CM4F/` |
| `portable/MemMang/heap_4.c` | `FreeRTOS/port/MemMang/` |

使用仓库中的 `USER/FreeRTOSConfig.h`。USART1 的抢占优先级为 5，优先级分组为 Group 4，与 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5` 对齐。

## 必需中断接入

在本地的 `USER/stm32f4xx_it.c/h` 中完成以下接入，不能直接使用空的厂商模板。

| 入口 | 接入方式 |
| --- | --- |
| `SysTick_Handler` | 先调用 `FreeRTOS_SysTick_Handler()`，随后调用 `BSP_Buzzer_Update(App_TimeNowMsFromISR())` |
| `USART1_IRQHandler` | 转发到 `BSP_USART1_IRQHandler()` |
| `HardFault_Handler`、`MemManage_Handler`、`BusFault_Handler`、`UsageFault_Handler` | 进入故障停机前调用 `BSP_Buzzer_EmergencyStop()` |
| `SVC_Handler`、`PendSV_Handler` | 使用 FreeRTOS 端口及 `FreeRTOSConfig.h` 提供的映射，删除模板中的重复实现 |

接入文件需要包含 `bsp_buzzer.h`、`bsp_usart.h`、`app_time.h`，并为 FreeRTOS tick 入口提供匹配的函数声明。SysTick 只能有一个入口实现。

**蜂鸣器在 PF8，高电平有效。`BSP_Buzzer_Update` 负责结束短鸣，漏掉 SysTick 中的调用可能使 PF8 持续激活。接好中断并核对电路后，才能运行会触发蜂鸣器的板端程序。**

主函数已经绑定串口字节回调并创建任务。ISR 只处理接收和任务通知，不在其中解析命令、执行推理或打印阻塞日志。栈溢出、分配失败和断言时的静音处理已经写在应用入口和配置中。

## NanoEdge AI

从 [ST NanoEdge AI Studio](https://www.st.com/en/development-tools/nanoedgeaistudio.html) 获取工具，按自己的数据、目标芯片和适用许可生成库，步骤见 [官方文档](https://wiki.st.com/stm32mcu/wiki/AI:NanoEdge_AI_Studio)。仓库没有附带生成包或模型。

推理模式需要在 `AI/vendor/` 放入同一次生成、相互匹配的三个文件：

- `NanoEdgeAI.h`
- `knowledge.h`
- `libneai.a`

适配层按以下数据约定工作：

- 应用输入为 128×3 的 XYZ 窗口，单位为 g。
- vendor 接口要求 `DATA_INPUT_USER=1`、`AXIS_NUMBER=3`、`CLASS_NUMBER=3`。
- 每行数据乘以 8192，转换为 raw-count float 后调用分类 API，不调换轴序、不额外归一化。
- 类别 ID 为 1=VIBRATION、2=STABLE、3=IMPACT。
- 对 128 次输出的各类概率分别求均值，再取最大值。`knowledge.h` 只在 `ai_vendor_port.c` 中包含一次。
- 原接入使用 soft-float 库。新库需要确认与 ARMCC/Cortex-M4.fp 工程的链接兼容性；编译器、浮点选项和生成头文件必须匹配。

数据维度、量程、类别顺序或生成接口有变化时，应一起修改适配层并验证结果，不能直接换入任意三分类库。

### 暂不接入模型时

如果只需要采样、命令和数据导出，在本地工程中将 `AI_ADAPTER_ENABLE_NANOEDGE=1` 改为 `0`，并移除 Keil AI 组里的 `libneai.a` 引用。

关闭后，NanoEdge 生成文件不参与编译，适配层返回 `UNAVAILABLE`。ST/CMSIS、FreeRTOS 和上述中断接入仍然必需。仓库的工程默认开启推理。

## 接入检查

1. 确认工程中的源文件、头文件和库文件都能定位。
2. 核对编译器、芯片、时钟、内存、浮点 ABI 和各中断入口，避免重复定义。
3. 完成编译和链接，检查错误、警告与 MAP 文件。当前尚未验证补齐依赖后的完整构建。
4. 核对 I2C1 PB8/PB9、USART1 PA9/PA10、LED PF9、蜂鸣器 PF8 及实际电路，再开展板端测试。
5. 推理测试使用与模型约定一致的数据，并按独立 session 划分训练集和测试集。模型效果需要单独测量。
