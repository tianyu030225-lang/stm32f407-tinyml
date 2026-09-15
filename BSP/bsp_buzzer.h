#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BSP_BUZZER_PATTERN_IMPACT = 0,
    BSP_BUZZER_PATTERN_VIBRATION,
    BSP_BUZZER_PATTERN_FAULT
} BSP_BuzzerPattern;

typedef enum
{
    BSP_BUZZER_OK = 0,
    BSP_BUZZER_BUSY,
    BSP_BUZZER_INVALID_PATTERN
} BSP_BuzzerStatus;

/* Initialize PF8 and leave the active-high buzzer silent. */
void BSP_Buzzer_Init(void);

/* Start one finite, predefined rhythm; this function never blocks. */
BSP_BuzzerStatus BSP_Buzzer_Request(BSP_BuzzerPattern pattern,
                                    uint32_t now_ms);

/* Advance the finite rhythm from the RTOS tick context. */
void BSP_Buzzer_Update(uint32_t now_ms);

/* Immediately force the buzzer off and cancel the current rhythm. */
void BSP_Buzzer_Stop(void);

/*
 * Emergency path for assertions and fault handlers.  It does not use the
 * scheduler, interrupts, or any other runtime service.
 */
void BSP_Buzzer_EmergencyStop(void);

uint8_t BSP_Buzzer_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUZZER_H */
