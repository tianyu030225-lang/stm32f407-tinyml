#include "app_state.h"

#define APP_STATE_READ_ATTEMPTS (3U)

static void app_state_clear_snapshot(app_system_snapshot_t *snapshot)
{
    snapshot->mode = APP_MODE_IDLE;
    snapshot->sensor_ready = 0U;
    snapshot->model_ready = 0U;
    snapshot->reserved0 = 0U;
    snapshot->capture_label = APP_LABEL_NONE;
    snapshot->last_class = APP_CLASS_UNKNOWN;
    snapshot->threshold_milli = 800U;
    snapshot->session_id = 0U;
    snapshot->last_window_sequence = 0U;
    snapshot->last_confidence_milli = 0U;
    snapshot->reserved1 = 0U;
    snapshot->last_latency_us = 0U;
    snapshot->alarm_flags = APP_ALARM_FLAG_NONE;

    snapshot->stats.sample_count = 0U;
    snapshot->stats.window_count = 0U;
    snapshot->stats.window_drop_count = 0U;
    snapshot->stats.i2c_error_count = 0U;
    snapshot->stats.sensor_reset_count = 0U;
    snapshot->stats.model_error_count = 0U;
    snapshot->stats.unknown_count = 0U;
    snapshot->stats.rx_overflow_count = 0U;
    snapshot->stats.invalid_command_count = 0U;
    snapshot->stats.inference_count = 0U;
    snapshot->stats.inference_total_us = 0U;
    snapshot->stats.inference_average_us = 0U;
    snapshot->stats.inference_p95_us = 0U;
    snapshot->stats.inference_max_us = 0U;
    snapshot->stats.imu_task_min_stack_words = 0U;
    snapshot->stats.command_task_min_stack_words = 0U;
    snapshot->stats.ai_task_min_stack_words = 0U;
}

void app_state_init(
    app_state_t *state,
    app_mode_t initial_mode,
    uint8_t sensor_ready,
    uint8_t model_ready)
{
    app_system_snapshot_t snapshot;

    if (state == (app_state_t *)0)
    {
        return;
    }

    app_state_clear_snapshot(&snapshot);
    if (app_mode_is_valid(initial_mode))
    {
        snapshot.mode = initial_mode;
    }
    snapshot.sensor_ready = sensor_ready;
    snapshot.model_ready = model_ready;
    state->sequence = 0U;
    state->snapshot = snapshot;
}

uint8_t app_state_publish(
    app_state_t *state,
    const app_system_snapshot_t *snapshot)
{
    if ((state == (app_state_t *)0) ||
        (snapshot == (const app_system_snapshot_t *)0))
    {
        return 0U;
    }

    state->sequence++;
    state->snapshot = *snapshot;
    state->sequence++;
    return 1U;
}

uint8_t app_state_read_snapshot(
    const app_state_t *state,
    app_system_snapshot_t *snapshot)
{
    uint32_t before;
    uint32_t after;
    uint8_t attempt;

    if ((state == (const app_state_t *)0) ||
        (snapshot == (app_system_snapshot_t *)0))
    {
        return 0U;
    }

    for (attempt = 0U; attempt < APP_STATE_READ_ATTEMPTS; attempt++)
    {
        before = state->sequence;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        *snapshot = state->snapshot;
        after = state->sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            return 1U;
        }
    }
    return 0U;
}
