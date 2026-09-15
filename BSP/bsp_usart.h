#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_USART1_BAUDRATE          ((uint32_t)115200U)
#define BSP_USART_TX_TIMEOUT_DEFAULT ((uint32_t)100000U)

typedef enum
{
    BSP_USART_OK = 0,
    BSP_USART_ERR_ARGUMENT,
    BSP_USART_ERR_TIMEOUT,
    BSP_USART_ERR_NOT_INITIALIZED
} BSP_USART_Status;

/* This hook is called from USART1 IRQ context and must be ISR-safe. */
typedef void (*BSP_USART_RxByteHook)(uint8_t byte);

/* Configure USART1 on PA9/PA10 for 115200 8N1 and enable RXNE IRQ. */
void BSP_USART_Init(void);

/* Register the upper-layer byte sink; no parsing or formatting is done here. */
void BSP_USART_SetRxByteHook(BSP_USART_RxByteHook hook);

/* Call this from the vector's USART1_IRQHandler implementation. */
void BSP_USART1_IRQHandler(void);

/* Task-context polling send with a finite TX wait budget per byte. */
BSP_USART_Status BSP_USART_Send(const uint8_t *data,
                                uint16_t length,
                                uint32_t timeout_budget);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USART_H */
