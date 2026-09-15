#include "app_tx.h"

#include <stdarg.h>
#include <stdio.h>

#include "bsp_usart.h"

#define APP_TX_FORMAT_BUFFER_SIZE (512U)
#define APP_TX_MUTEX_WAIT_TICKS   (pdMS_TO_TICKS(50U))

uint8_t App_TxSend(app_runtime_t *runtime,
                   const uint8_t *data,
                   uint16_t length)
{
    uint8_t result;

    if ((runtime == (app_runtime_t *)0) ||
        (data == (const uint8_t *)0) ||
        (length == 0U) ||
        (runtime->tx_mutex == (SemaphoreHandle_t)0))
    {
        return 0U;
    }

    if (xSemaphoreTake(runtime->tx_mutex, APP_TX_MUTEX_WAIT_TICKS) != pdTRUE)
    {
        runtime->producer.tx_fault_pending = 1U;
        app_runtime_signal_ai(runtime);
        return 0U;
    }

    result = (uint8_t)(BSP_USART_Send(data,
                                      length,
                                      BSP_USART_TX_TIMEOUT_DEFAULT) ==
                       BSP_USART_OK);
    (void)xSemaphoreGive(runtime->tx_mutex);
    if (result == 0U)
    {
        runtime->producer.tx_fault_pending = 1U;
        app_runtime_signal_ai(runtime);
    }
    return result;
}

uint8_t App_TxSendf(app_runtime_t *runtime, const char *format, ...)
{
    char buffer[APP_TX_FORMAT_BUFFER_SIZE];
    va_list arguments;
    int length;

    if ((runtime == (app_runtime_t *)0) ||
        (format == (const char *)0))
    {
        return 0U;
    }

    va_start(arguments, format);
    length = vsnprintf(buffer,
                       sizeof(buffer),
                       format,
                       arguments);
    va_end(arguments);
    if ((length <= 0) ||
        ((uint32_t)length >= (uint32_t)sizeof(buffer)))
    {
        runtime->producer.tx_fault_pending = 1U;
        app_runtime_signal_ai(runtime);
        return 0U;
    }
    return App_TxSend(runtime,
                      (const uint8_t *)buffer,
                      (uint16_t)length);
}
