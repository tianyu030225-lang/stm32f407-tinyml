#include "FreeRTOS.h"
#include "task.h"

#include "bsp_buzzer.h"
#include "bsp_i2c.h"
#include "bsp_led.h"
#include "bsp_mpu6050.h"
#include "bsp_usart.h"
#include "ai_task.h"
#include "command_task.h"
#include "imu_task.h"
#include "app_runtime.h"
#include "app_time.h"

static app_runtime_t s_runtime;

static void app_boot_send(const char *message, uint16_t length)
{
    (void)BSP_USART_Send((const uint8_t *)message,
                          length,
                          BSP_USART_TX_TIMEOUT_DEFAULT);
}

static void app_safe_silent_failure(void)
{
    BSP_Buzzer_Stop();
    BSP_LED_Set(0U);
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

int main(void)
{
    BSP_MPU6050_Status sensor_status;
    uint8_t sensor_ready;

    App_TimeInit();
    BSP_LED_Init();
    BSP_Buzzer_Init();

    /* Bring up the diagnostic channel before touching the external sensor. */
    BSP_USART_Init();
    app_boot_send("BOOT\r\n", (uint16_t)(sizeof("BOOT\r\n") - 1U));

    sensor_status = BSP_MPU6050_Init(BSP_I2C1_TIMEOUT_DEFAULT);
    sensor_ready = (uint8_t)(sensor_status == BSP_MPU6050_OK);
    if (sensor_ready != 0U)
    {
        app_boot_send("SENSOR OK\r\n",
                      (uint16_t)(sizeof("SENSOR OK\r\n") - 1U));
    }
    else
    {
        app_boot_send("SENSOR ERR\r\n",
                      (uint16_t)(sizeof("SENSOR ERR\r\n") - 1U));
    }

    if (app_runtime_init(&s_runtime, sensor_ready) == 0U)
    {
        app_boot_send("RUNTIME ERR\r\n",
                      (uint16_t)(sizeof("RUNTIME ERR\r\n") - 1U));
        app_safe_silent_failure();
    }

    App_CommandBindRuntime(&s_runtime);
    BSP_USART_SetRxByteHook(App_CommandRxByteFromISR);

    if (xTaskCreate(App_ImuTask,
                    "ImuTask",
                    512U,
                    &s_runtime,
                    3U,
                    &s_runtime.imu_task) != pdPASS)
    {
        app_boot_send("TASK ERR IMU\r\n",
                      (uint16_t)(sizeof("TASK ERR IMU\r\n") - 1U));
        app_safe_silent_failure();
    }
    if (xTaskCreate(App_CommandTask,
                    "CommandTask",
                    512U,
                    &s_runtime,
                    2U,
                    &s_runtime.command_task) != pdPASS)
    {
        app_boot_send("TASK ERR CMD\r\n",
                      (uint16_t)(sizeof("TASK ERR CMD\r\n") - 1U));
        app_safe_silent_failure();
    }
    if (xTaskCreate(App_AiTask,
                    "AiTask",
                    1024U,
                    &s_runtime,
                    1U,
                    &s_runtime.ai_task) != pdPASS)
    {
        app_boot_send("TASK ERR AI\r\n",
                      (uint16_t)(sizeof("TASK ERR AI\r\n") - 1U));
        app_safe_silent_failure();
    }

    vTaskStartScheduler();
    app_safe_silent_failure();
    return 0;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    app_safe_silent_failure();
}

void vApplicationMallocFailedHook(void)
{
    app_safe_silent_failure();
}
