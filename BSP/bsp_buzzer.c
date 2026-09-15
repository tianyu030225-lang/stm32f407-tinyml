#include "bsp_buzzer.h"

#include "stm32f4xx_conf.h"

#define BSP_BUZZER_PIN GPIO_Pin_8

typedef struct
{
    uint16_t duration_ms;
    uint8_t active;
} BSP_BuzzerSegment;

static const BSP_BuzzerSegment s_impact_pattern[] =
{
    {80U, 1U}, {80U, 0U}, {80U, 1U}, {80U, 0U}, {80U, 1U}
};

static const BSP_BuzzerSegment s_vibration_pattern[] =
{
    {120U, 1U}, {120U, 0U}, {120U, 1U}, {120U, 0U}
};

static const BSP_BuzzerSegment s_fault_pattern[] =
{
    {120U, 1U}, {120U, 0U}
};

static const BSP_BuzzerSegment *s_pattern;
static uint8_t s_pattern_length;
static uint8_t s_segment_index;
static uint8_t s_active;
static uint32_t s_segment_deadline;
static uint32_t s_pattern_deadline;

static uint32_t bsp_buzzer_enter_critical(void)
{
    uint32_t saved_primask;

    saved_primask = __get_PRIMASK();
    __disable_irq();
    return saved_primask;
}

static void bsp_buzzer_exit_critical(uint32_t saved_primask)
{
    __set_PRIMASK(saved_primask);
}

static uint8_t bsp_buzzer_deadline_reached(uint32_t now_ms,
                                           uint32_t deadline_ms)
{
    return (uint8_t)(((int32_t)(now_ms - deadline_ms)) >= 0);
}

static void bsp_buzzer_drive(uint8_t active)
{
    if (active != 0U)
    {
        /* PF8 drives the S8050 base; high is the active level. */
        GPIO_SetBits(GPIOF, BSP_BUZZER_PIN);
    }
    else
    {
        GPIO_ResetBits(GPIOF, BSP_BUZZER_PIN);
    }
}

void BSP_Buzzer_EmergencyStop(void)
{
    /* BSRRH is a direct write-only reset path for PF8; no library/runtime call. */
    GPIOF->BSRRH = (uint16_t)BSP_BUZZER_PIN;
    s_active = 0U;
    s_pattern = (const BSP_BuzzerSegment *)0;
    s_pattern_length = 0U;
    s_segment_index = 0U;
    s_segment_deadline = 0U;
    s_pattern_deadline = 0U;
}

static uint8_t bsp_buzzer_select_pattern(BSP_BuzzerPattern pattern)
{
    switch (pattern)
    {
    case BSP_BUZZER_PATTERN_IMPACT:
        s_pattern = s_impact_pattern;
        s_pattern_length = (uint8_t)(sizeof(s_impact_pattern) /
                                     sizeof(s_impact_pattern[0]));
        return 1U;

    case BSP_BUZZER_PATTERN_VIBRATION:
        s_pattern = s_vibration_pattern;
        s_pattern_length = (uint8_t)(sizeof(s_vibration_pattern) /
                                     sizeof(s_vibration_pattern[0]));
        return 1U;

    case BSP_BUZZER_PATTERN_FAULT:
        s_pattern = s_fault_pattern;
        s_pattern_length = (uint8_t)(sizeof(s_fault_pattern) /
                                     sizeof(s_fault_pattern[0]));
        return 1U;

    default:
        s_pattern = (const BSP_BuzzerSegment *)0;
        s_pattern_length = 0U;
        return 0U;
    }
}

void BSP_Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

    gpio.GPIO_Pin = BSP_BUZZER_PIN;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
    GPIO_Init(GPIOF, &gpio);

    s_active = 0U;
    s_pattern = (const BSP_BuzzerSegment *)0;
    s_pattern_length = 0U;
    s_segment_index = 0U;
    s_segment_deadline = 0U;
    s_pattern_deadline = 0U;
    /* Initialization is explicitly silent, including before any task starts. */
    BSP_Buzzer_EmergencyStop();
}

BSP_BuzzerStatus BSP_Buzzer_Request(BSP_BuzzerPattern pattern,
                                    uint32_t now_ms)
{
    uint32_t saved_primask;
    uint32_t total_ms;
    uint8_t index;

    saved_primask = bsp_buzzer_enter_critical();
    if (s_active != 0U)
    {
        bsp_buzzer_exit_critical(saved_primask);
        return BSP_BUZZER_BUSY;
    }
    if (bsp_buzzer_select_pattern(pattern) == 0U)
    {
        bsp_buzzer_exit_critical(saved_primask);
        return BSP_BUZZER_INVALID_PATTERN;
    }

    total_ms = 0U;
    for (index = 0U; index < s_pattern_length; ++index)
    {
        total_ms += s_pattern[index].duration_ms;
    }

    s_segment_index = 0U;
    s_segment_deadline = now_ms + s_pattern[0].duration_ms;
    s_pattern_deadline = now_ms + total_ms;
    s_active = 1U;
    bsp_buzzer_drive(s_pattern[0].active);
    bsp_buzzer_exit_critical(saved_primask);
    return BSP_BUZZER_OK;
}

void BSP_Buzzer_Update(uint32_t now_ms)
{
    uint8_t next_index;

    if (s_active == 0U)
    {
        return;
    }

    /* The total deadline is a hard upper bound for every predefined rhythm. */
    if (bsp_buzzer_deadline_reached(now_ms, s_pattern_deadline) != 0U)
    {
        BSP_Buzzer_Stop();
        return;
    }

    while (s_active != 0U &&
           bsp_buzzer_deadline_reached(now_ms, s_segment_deadline) != 0U)
    {
        next_index = (uint8_t)(s_segment_index + 1U);
        if (next_index >= s_pattern_length)
        {
            BSP_Buzzer_Stop();
            return;
        }

        s_segment_index = next_index;
        s_segment_deadline += s_pattern[s_segment_index].duration_ms;
        bsp_buzzer_drive(s_pattern[s_segment_index].active);
    }
}

void BSP_Buzzer_Stop(void)
{
    uint32_t saved_primask;

    saved_primask = bsp_buzzer_enter_critical();
    s_active = 0U;
    s_pattern = (const BSP_BuzzerSegment *)0;
    s_pattern_length = 0U;
    s_segment_index = 0U;
    s_segment_deadline = 0U;
    s_pattern_deadline = 0U;
    /* Normal stop is also a hard low-level cutoff. */
    BSP_Buzzer_EmergencyStop();
    bsp_buzzer_exit_critical(saved_primask);
}

uint8_t BSP_Buzzer_IsActive(void)
{
    return s_active;
}
