#include "bsp_led.h"

#include "stm32f4xx_conf.h"

#define BSP_LED_PIN GPIO_Pin_9

static uint8_t s_led_on;

void BSP_LED_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

    gpio.GPIO_Pin = BSP_LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOF, &gpio);

    /* Existing board code and schematic use a high level for LED off. */
    GPIO_SetBits(GPIOF, BSP_LED_PIN);
    s_led_on = 0U;
}

void BSP_LED_Set(uint8_t on)
{
    if (on != 0U)
    {
        GPIO_ResetBits(GPIOF, BSP_LED_PIN);
        s_led_on = 1U;
    }
    else
    {
        GPIO_SetBits(GPIOF, BSP_LED_PIN);
        s_led_on = 0U;
    }
}

void BSP_LED_Toggle(void)
{
    BSP_LED_Set((uint8_t)(s_led_on == 0U));
}

uint8_t BSP_LED_IsOn(void)
{
    return s_led_on;
}
