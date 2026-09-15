#include "app_time.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

extern uint32_t SystemCoreClock;

/* DWT is deliberately opt-in: App_TimeInit is the boundary for cycle timing. */
static uint8_t s_dwt_available;

void App_TimeInit(void)
{
    s_dwt_available = 0U;

#if defined(DWT) && defined(CoreDebug)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_dwt_available = 1U;
#endif
}

uint32_t App_TimeCycleToken(void)
{
    if (s_dwt_available != 0U)
    {
        return DWT->CYCCNT;
    }

    /* The fallback token is already expressed in microseconds. */
    return (uint32_t)(xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS *      
                      1000U);
}

uint32_t App_TimeCycleDelta(uint32_t start_token, uint32_t end_token)
{
    /* Unsigned arithmetic intentionally handles one raw CYCCNT wrap. */
    return end_token - start_token;
}

uint32_t App_TimeCycleDeltaToUs(uint32_t cycle_delta)
{
    if (s_dwt_available == 0U)
    {
        return cycle_delta;
    }

    /* A zero clock cannot be used as a conversion divisor. */
    if (SystemCoreClock == 0U)
    {
        return 0U;
    }

    return (uint32_t)(((uint64_t)cycle_delta * (uint64_t)1000000U) /
                      (uint64_t)SystemCoreClock);
}

uint32_t App_TimeNowMs(void)
{
    /*
     * Milliseconds are an absolute application clock.  Keep the FreeRTOS
     * tick's natural unsigned wrap instead of deriving a 25.6-second clock
     * from the 32-bit DWT counter.
     */
    return (uint32_t)(xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS);      
}

uint32_t App_TimeNowMsFromISR(void)
{
    return (uint32_t)(xTaskGetTickCountFromISR() *
                      (uint32_t)portTICK_PERIOD_MS);
}
