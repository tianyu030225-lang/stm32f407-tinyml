#include "app_mode.h"

uint8_t app_mode_is_valid(app_mode_t mode)
{
    /* ARMCC represents this non-negative enum as unsigned; the lower-bound
     * comparison against APP_MODE_IDLE (zero) is therefore always true. */
    return (uint8_t)(mode <= APP_MODE_FAULT);
}

uint8_t app_mode_can_transition(app_mode_t current, app_mode_t next)
{
    if (!app_mode_is_valid(current) || !app_mode_is_valid(next))
    {
        return 0U;
    }
    if (current == next)
    {
        return 1U;
    }

    switch (current)
    {
        case APP_MODE_IDLE:
            return (uint8_t)((next == APP_MODE_CAPTURE) ||
                             (next == APP_MODE_INFERENCE) ||
                             (next == APP_MODE_FAULT));

        case APP_MODE_CAPTURE:
            return (uint8_t)((next == APP_MODE_IDLE) ||
                             (next == APP_MODE_FAULT));

        case APP_MODE_INFERENCE:
            return (uint8_t)((next == APP_MODE_IDLE) ||
                             (next == APP_MODE_FAULT));

        case APP_MODE_FAULT:
        default:
            return 0U;
    }
}

app_mode_transition_status_t app_mode_transition(
    app_mode_t *current,
    app_mode_t next)
{
    if (current == (app_mode_t *)0)
    {
        return APP_MODE_TRANSITION_INVALID_ARGUMENT;
    }
    if (!app_mode_can_transition(*current, next))
    {
        return APP_MODE_TRANSITION_INVALID;
    }
    *current = next;
    return APP_MODE_TRANSITION_OK;
}

const char *app_mode_name(app_mode_t mode)
{
    switch (mode)
    {
        case APP_MODE_IDLE:
            return "IDLE";
        case APP_MODE_CAPTURE:
            return "CAPTURE";
        case APP_MODE_INFERENCE:
            return "INFERENCE";
        case APP_MODE_FAULT:
            return "FAULT";
        case APP_MODE_INVALID:
        default:
            return "INVALID";
    }
}
