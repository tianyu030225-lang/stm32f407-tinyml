#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdint.h>

/* Initializes DWT for short inference intervals; tick timing remains usable. */
void App_TimeInit(void);
/*
 * Returns a raw 32-bit DWT CYCCNT token when DWT is available.  The fallback
 * token is a tick-derived microsecond clock.  The measured interval must be
 * shorter than one 32-bit CYCCNT wrap so unsigned subtraction is unambiguous.
 */
uint32_t App_TimeCycleToken(void);
/* Unsigned subtraction of two cycle tokens, valid across one counter wrap. */
uint32_t App_TimeCycleDelta(uint32_t start_token, uint32_t end_token);
/* Converts a raw cycle delta to microseconds; the fallback token is already us. */
uint32_t App_TimeCycleDeltaToUs(uint32_t cycle_delta);
/* FreeRTOS-tick millisecond clock with natural unsigned wrap. */
uint32_t App_TimeNowMs(void);
/* ISR-safe counterpart used by the tick-owned actuator service. */
uint32_t App_TimeNowMsFromISR(void);

#endif /* APP_TIME_H */
