#ifndef APP_MODE_H
#define APP_MODE_H

#include <stdint.h>

typedef enum
{
    APP_MODE_IDLE = 0,
    APP_MODE_CAPTURE,
    APP_MODE_INFERENCE,
    APP_MODE_FAULT,
    APP_MODE_INVALID
} app_mode_t;

typedef enum
{
    APP_MODE_TRANSITION_OK = 0,
    APP_MODE_TRANSITION_INVALID,
    APP_MODE_TRANSITION_INVALID_ARGUMENT
} app_mode_transition_status_t;

uint8_t app_mode_is_valid(app_mode_t mode);
uint8_t app_mode_can_transition(app_mode_t current, app_mode_t next);
app_mode_transition_status_t app_mode_transition(
    app_mode_t *current,
    app_mode_t next);
const char *app_mode_name(app_mode_t mode);

#endif /* APP_MODE_H */
