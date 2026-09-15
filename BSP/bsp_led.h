#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PF9 is the board status LED and is active low. */
void BSP_LED_Init(void);
void BSP_LED_Set(uint8_t on);
void BSP_LED_Toggle(void);
uint8_t BSP_LED_IsOn(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
