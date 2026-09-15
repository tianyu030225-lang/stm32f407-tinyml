#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

#include "app_types.h"

/*
 * AiTask is the sole writer.  Other tasks must obtain a copy through
 * app_state_read_snapshot() and must not mutate this object directly.
 */
typedef struct
{
    volatile uint32_t sequence;
    volatile app_system_snapshot_t snapshot;
} app_state_t;

void app_state_init(
    app_state_t *state,
    app_mode_t initial_mode,
    uint8_t sensor_ready,
    uint8_t model_ready);

/* Publish a complete snapshot from AiTask; the input remains caller-owned. */
uint8_t app_state_publish(
    app_state_t *state,
    const app_system_snapshot_t *snapshot);

/* CommandTask reads a consistent copy; it never receives a mutable pointer. */
uint8_t app_state_read_snapshot(
    const app_state_t *state,
    app_system_snapshot_t *snapshot);

#endif /* APP_STATE_H */
