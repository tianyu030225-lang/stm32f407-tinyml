# 依赖获取与接入

本公开包没有随附第三方实现、工具链、模型或可刷写固件。下列目录表示使用者本地补齐后的布局，已列入忽略规则，不应不加区分地重新提交。

## 工具链与工程参数

- 工程入口：`USER/Project.uvprojx`。
- 原工程记录：ARMCC 5.06 update 6（build 750），`uAC6=0`，STM32F407ZE，Cortex-M4.fp，Keil.STM32F4xx_DFP.1.0.8。
- 预定义包括 `STM32F40_41xxx`、`USE_STDPERIPH_DRIVER`、`__FPU_USED=1`、`HSE_VALUE=8000000`、`AI_ADAPTER_ENABLE_NANOEDGE=1`。
- 原 F407 时钟实现使用 HSE 8 MHz，`PLL_M=8`、`PLL_N=336`、`PLL_P=2`、`PLL_Q=7`，配置目标为 168 MHz。补入 `system_stm32f4xx.c` 时必须核对对应分支的实际宏值和 `SystemCoreClock`，不能只改 `HSE_VALUE` 后沿用厂商模板的默认 PLL 参数；这些配置值不替代实际硬件时钟核验。
- FreeRTOS Tick 为 1000 Hz，堆大小为 75 KiB，使用 `heap_4.c` 和 RVDS/ARM_CM4F 端口。不要混用多个内存分配实现。
- 工程声明的 IROM 为 `0x08000000 + 0x80000`；IRAM 为 `0x20000000 + 0x20000`，IRAM2 为 `0x10000000 + 0x10000`。按实际芯片和链接设置核对，不根据目录名称改变容量。
- 这是原配置的记录，不保证任意新版 MDK、Device Pack 或编译器可直接替换。

## ST 标准外设库与 CMSIS

官方获取入口：[STSW-STM32065](https://www.st.com/en/embedded-software/stsw-stm32065.html)。

项目使用标准外设库接口，不是 STM32 HAL。原文件头记录了 ST 标准库 V1.4.0 和 CMSIS Core V3.20；应核对下载包的具体版本和许可，不把更新版本当成已经验证的等价替换。

| 本地目标位置 | 需补齐的内容 |
| --- | --- |
| `FWLIB/STM32F4xx_StdPeriph_Driver/inc/` | 标准库头文件，含 misc、GPIO、RCC、SYSCFG、USART、I2C |
| `FWLIB/STM32F4xx_StdPeriph_Driver/src/` | `misc.c`、`stm32f4xx_gpio.c`、`stm32f4xx_rcc.c`、`stm32f4xx_syscfg.c`、`stm32f4xx_usart.c`、`stm32f4xx_i2c.c` |
| `CORE/` | `core_cm4.h`、`core_cm4_simd.h`、`core_cmFunc.h`、`core_cmInstr.h`，以及 ARMCC 启动文件 `startup_stm32f40_41xxx.s` |
| `USER/` | `stm32f4xx.h`、`system_stm32f4xx.h`、`system_stm32f4xx.c`，以及按下节接入的 `stm32f4xx_it.c/h` |

各版供应包内部目录可能不同，以文件名、版本和目标 CPU 匹配为准。`SystemInit`、时钟树、`SystemCoreClock` 和向量表由对应 ST/CMSIS 启动实现提供；公开包不替代这些实现。

## FreeRTOS V9.0.0

官方源码：[FreeRTOS V9.0.0 Source](https://github.com/FreeRTOS/FreeRTOS/tree/V9.0.0/FreeRTOS/Source)。保留该版本的 [原始许可和例外条款](https://github.com/FreeRTOS/FreeRTOS/blob/V9.0.0/FreeRTOS/License/license.txt)，不要替换为新版 FreeRTOS 的 MIT 许可。

| 上游位置（相对于 FreeRTOS/Source） | 本地目标位置 |
| --- | --- |
| `include/` | `FreeRTOS/include/` |
| `croutine.c`、`event_groups.c`、`list.c`、`queue.c`、`tasks.c`、`timers.c` | `FreeRTOS/src/` |
| `portable/RVDS/ARM_CM4F/port.c`、`portmacro.h` | `FreeRTOS/port/RVDS/ARM_CM4F/` |
| `portable/MemMang/heap_4.c` | `FreeRTOS/port/MemMang/` |

使用公开包的 `USER/FreeRTOSConfig.h`。USART1 的抢占优先级配置为 5，优先级分组使用 Group 4，与 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5` 对齐。

## 必需中断接入

`USER/stm32f4xx_it.c/h` 是需要使用者自行提供并接入的文件。**只补一个空的厂商模板不能恢复完整行为。**

| 入口 | 必须完成的接入 |
| --- | --- |
| `SysTick_Handler` | 先调用 `FreeRTOS_SysTick_Handler()`，随后调用 `BSP_Buzzer_Update(App_TimeNowMsFromISR())` |
| `USART1_IRQHandler` | 转发到 `BSP_USART1_IRQHandler()` |
| `HardFault_Handler`、`MemManage_Handler`、`BusFault_Handler`、`UsageFault_Handler` | 进入故障停机前调用 `BSP_Buzzer_EmergencyStop()` |
| `SVC_Handler`、`PendSV_Handler` | 由 FreeRTOS 端口及 `FreeRTOSConfig.h` 的映射提供，不要再保留模板中的重复实现 |

编写接入文件时包含声明自有函数的 `bsp_buzzer.h`、`bsp_usart.h`、`app_time.h`，并为 FreeRTOS tick 入口提供匹配的函数声明。禁止把 SysTick 同时重复交给两个实现。

蜂鸣器位于 PF8，高电平有效；周期更新负责结束短鸣。**缺少 SysTick 中的更新调用可能使 PF8 持续激活。接入完成并核对实际电路前不要运行会触发蜂鸣器的板端程序。**

应用主函数已经绑定串口字节回调并创建任务；不应在 ISR 内额外解析命令、执行推理或打印阻塞日志。栈溢出、分配失败和断言的静音处理保留在公开的应用入口和配置中。

## NanoEdge AI

使用者从 [ST NanoEdge AI Studio](https://www.st.com/en/development-tools/nanoedgeaistudio.html) 自行获取工具，并按自己的数据、许可和目标生成库；生成流程见 [官方文档](https://wiki.st.com/stm32mcu/wiki/AI:NanoEdge_AI_Studio)。本仓库不提供原生成包或模型成绩。

完整推理模式要求本地 `AI/vendor/` 中具有相互匹配的：

- `NanoEdgeAI.h`
- `knowledge.h`
- `libneai.a`

自有适配器有明确约束，不能直接换成任意三分类库：

- 应用输入为 128×3 的 XYZ 窗口，单位 g。
- 当前 vendor 接口要求 `DATA_INPUT_USER=1`、`AXIS_NUMBER=3`、`CLASS_NUMBER=3`。
- 每一行乘以 8192 恢复 raw-count float，调用分类 API；不做轴序调换或额外归一化。
- 当前类别 ID 为 1=VIBRATION、2=STABLE、3=IMPACT。
- 128 次概率按类别求均值，再取最大值；`knowledge.h` 只由 `ai_vendor_port.c` 一个编译单元包含。
- 原接入记录使用 soft-float 库。需要核实新库与 ARMCC/Cortex-M4.fp 工程的实际链接兼容性；不要通过随意切换编译器、浮点选项或改头文件来绕过不匹配。
- 数据维度、量程、类别顺序或生成接口不同，都需要重新审查适配层并验证结果。

### 暂不接入模型时

若只准备采样、命令和数据导出，可在自己的本地工程中将 `AI_ADAPTER_ENABLE_NANOEDGE=1` 改为 `0`，同时移除 Keil AI 组中的 `libneai.a` 引用。此时 vendor 生成文件不参与编译，适配层返回 `UNAVAILABLE`，不会伪造推理结果。

这只是既有编译开关的用法；ST/CMSIS/FreeRTOS 和上面的中断接入仍然必需。公开工程默认保留完整推理模式的配置，未自动切换。

## 补齐后的检查顺序

1. 在工程视图中确认所有声明的源文件和头文件能够定位。
2. 核对工具链、芯片、时钟、内存、浮点 ABI 和唯一的中断入口。
3. 先完成本地编译与链接，检查错误、警告和 MAP；构建成功不代表硬件行为已经验证。
4. 核对实际 I2C1 PB8/PB9、USART1 PA9/PA10、LED PF9、蜂鸣器 PF8 及电路，再按自己的硬件验证流程操作。
5. 如需推理，另行确认实际模型的数据约定和独立测试数据；不要将生成库视为自研算法或用未测数值描述效果。
