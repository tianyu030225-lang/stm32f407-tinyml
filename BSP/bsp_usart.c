#include "bsp_usart.h"

#include "stm32f4xx_conf.h"

static BSP_USART_RxByteHook s_rx_byte_hook;
static uint8_t s_usart_initialized;

void BSP_USART_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    gpio.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = BSP_USART1_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);
    USART_ClearFlag(USART1, USART_FLAG_TC);

    /* FreeRTOS uses all four priority bits for pre-emption priorities. */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 5U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART1, ENABLE);
    s_usart_initialized = 1U;
}

void BSP_USART_SetRxByteHook(BSP_USART_RxByteHook hook)
{
    s_rx_byte_hook = hook;
}

void BSP_USART1_IRQHandler(void)
{
    uint32_t status;
    uint8_t byte;

    /* Read SR before DR so ORE/FE/NE/PE are cleared by the same bounded
     * register access as RXNE.  A byte with a framing/parity error is dropped;
     * an overrun still delivers the newest valid byte. */
    status = USART1->SR;
    if ((status & (USART_FLAG_RXNE |
                   USART_FLAG_ORE |
                   USART_FLAG_NE |
                   USART_FLAG_FE |
                   USART_FLAG_PE)) != 0U)
    {
        byte = (uint8_t)USART1->DR;
        if (((status & USART_FLAG_RXNE) != 0U) &&
            ((status & (USART_FLAG_NE | USART_FLAG_FE | USART_FLAG_PE)) == 0U) &&
            (s_rx_byte_hook != (BSP_USART_RxByteHook)0))
        {
            s_rx_byte_hook(byte);
        }
    }
}

BSP_USART_Status BSP_USART_Send(const uint8_t *data,
                                uint16_t length,
                                uint32_t timeout_budget)
{
    uint16_t index;
    uint32_t remaining;

    if (data == (const uint8_t *)0 || length == 0U || timeout_budget == 0U)
    {
        return BSP_USART_ERR_ARGUMENT;
    }
    if (s_usart_initialized == 0U)
    {
        return BSP_USART_ERR_NOT_INITIALIZED;
    }

    for (index = 0U; index < length; ++index)
    {
        remaining = timeout_budget;
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        {
            if (remaining == 0U)
            {
                return BSP_USART_ERR_TIMEOUT;
            }
            --remaining;
        }
        USART_SendData(USART1, data[index]);
    }

    remaining = timeout_budget;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    {
        if (remaining == 0U)
        {
            return BSP_USART_ERR_TIMEOUT;
        }
        --remaining;
    }
    return BSP_USART_OK;
}
