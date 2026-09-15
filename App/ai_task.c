#include "ai_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_buzzer.h"
#include "bsp_mpu6050.h"
#include "bsp_led.h"
#include "app_mode.h"
#include "app_runtime.h"
#include "app_time.h"
#include "app_tx.h"

#define APP_AI_WAIT_TICKS             (pdMS_TO_TICKS(10U))
#define APP_AI_FAULT_ALARM_FLAGS      (APP_ALARM_FLAG_SENSOR_FAULT | \
                                       APP_ALARM_FLAG_MODEL_FAULT | \
                                       APP_ALARM_FLAG_WINDOW_DROP)

/* AiTask-only scratch; keep the 512-entry sort off the task stack. */
static uint32_t s_app_ai_latency_sorted[APP_INFERENCE_HISTORY_LENGTH];
/* The generated model may use this window for the duration of one inference;
 * keep it out of the 4 KiB AiTask stack. */
static float s_app_ai_model_input[COMPONENT_WINDOW_SAMPLE_COUNT]
                                 [COMPONENT_WINDOW_AXIS_COUNT];

static uint16_t app_ai_saturate_u16(uint32_t value)
{
    if (value > 1000U)
    {
        return 1000U;
    }
    return (uint16_t)value;
}

static void app_ai_reset_decision(app_runtime_t *runtime)
{
    runtime->vibration_streak = 0U;
    runtime->stable_streak = 0U;
    runtime->ai_snapshot.last_class = APP_CLASS_UNKNOWN;
    runtime->ai_snapshot.last_confidence_milli = 0U;
    runtime->ai_snapshot.last_latency_us = 0U;
    runtime->ai_snapshot.alarm_flags = APP_ALARM_FLAG_NONE;
}

static void app_ai_send_free(
    app_runtime_t *runtime,
    component_window_id_t buffer_id,
    const component_window_metadata_t *metadata)
{
    component_window_metadata_t free_metadata;

    if (metadata != (const component_window_metadata_t *)0)
    {
        free_metadata = *metadata;
    }
    else
    {
        free_metadata.sequence = 0U;
        free_metadata.timestamp_tick = 0U;
        free_metadata.session_id = 0U;
        free_metadata.sample_count = 0U;
        free_metadata.label = APP_LABEL_NONE;
        free_metadata.reserved = 0U;
    }
    if (component_window_release_free(&runtime->window_pool,
                                      buffer_id) != COMPONENT_WINDOW_OK)
    {
        app_runtime_mark_resource_fault(runtime);
        return;
    }
    if (app_runtime_send_free(runtime, buffer_id, &free_metadata) != pdPASS)
    {
        app_runtime_mark_resource_fault(runtime);
    }
}

static void app_ai_flush_ready(app_runtime_t *runtime)
{
    app_window_queue_item_t item;
    const component_window_metadata_t *metadata;

    while (xQueueReceive(runtime->ready_buffer_queue,
                         &item,
                         (TickType_t)0) == pdPASS)
    {
        if (component_window_begin_processing(&runtime->window_pool,
                                              item.buffer_id) !=
            COMPONENT_WINDOW_OK)
        {
            if (component_window_abort_ready(&runtime->window_pool,
                                             item.buffer_id) ==
                COMPONENT_WINDOW_OK)
            {
                if (app_runtime_send_free(runtime,
                                          item.buffer_id,
                                          &item.metadata) != pdPASS)
                {
                    app_runtime_mark_resource_fault(runtime);
                }
            }
            app_runtime_mark_resource_fault(runtime);
            continue;
        }
        metadata = component_window_read_metadata(&runtime->window_pool,
                                                  item.buffer_id);
        app_ai_send_free(runtime, item.buffer_id, metadata);
    }
}

static void app_ai_discard_ready_item(
    app_runtime_t *runtime,
    const app_window_queue_item_t *item)
{
    const component_window_metadata_t *metadata;

    if (component_window_begin_processing(&runtime->window_pool,
                                          item->buffer_id) !=
        COMPONENT_WINDOW_OK)
    {
        if (component_window_abort_ready(&runtime->window_pool,
                                         item->buffer_id) ==
            COMPONENT_WINDOW_OK)
        {
            if (app_runtime_send_free(runtime,
                                      item->buffer_id,
                                      &item->metadata) != pdPASS)
            {
                app_runtime_mark_resource_fault(runtime);
            }
        }
        app_runtime_mark_resource_fault(runtime);
        return;
    }
    metadata = component_window_read_metadata(&runtime->window_pool,
                                              item->buffer_id);
    app_ai_send_free(runtime, item->buffer_id, metadata);
}

static void app_ai_stop_sampling(app_runtime_t *runtime)
{
    runtime->imu_enabled = 0U;
    runtime->sampling_generation++;
    runtime->pending_sampling_enable = 0U;
    BSP_Buzzer_Stop();
    app_ai_flush_ready(runtime);
}

static uint8_t app_ai_change_mode(app_runtime_t *runtime,
                                  app_mode_t next_mode)
{
    app_mode_transition_status_t status;

    if (runtime->ai_snapshot.mode == next_mode)
    {
        return 1U;
    }

    status = app_mode_transition(&runtime->ai_snapshot.mode, next_mode);
    if (status != APP_MODE_TRANSITION_OK)
    {
        return 0U;
    }

    app_ai_stop_sampling(runtime);
    app_ai_reset_decision(runtime);
    if ((next_mode == APP_MODE_CAPTURE) ||
        (next_mode == APP_MODE_INFERENCE))
    {
        runtime->pending_sampling_enable = 1U;
    }
    runtime->ai_snapshot.capture_label = runtime->capture_label;
    runtime->ai_snapshot.session_id = runtime->session_id;
    return 1U;
}

static void app_ai_enter_fault(app_runtime_t *runtime,
                               app_alarm_flags_t fault_flag)
{
    if (runtime->ai_snapshot.mode != APP_MODE_FAULT)
    {
        (void)app_ai_change_mode(runtime, APP_MODE_FAULT);
    }
    runtime->pending_sampling_enable = 0U;
    runtime->imu_enabled = 0U;
    ai_adapter_stop(&runtime->ai_adapter);
    runtime->ai_snapshot.model_ready = 0U;
    if ((fault_flag & APP_ALARM_FLAG_SENSOR_FAULT) != 0U)
    {
        runtime->ai_snapshot.sensor_ready = 0U;
    }
    runtime->ai_snapshot.alarm_flags |= fault_flag;
    BSP_LED_Set(1U);
    if (runtime->buzzer_fault_announced == 0U)
    {
        (void)BSP_Buzzer_Request(BSP_BUZZER_PATTERN_FAULT,
                                  App_TimeNowMs());
        runtime->buzzer_fault_announced = 1U;
    }
}

static const char *app_ai_fault_response(const app_runtime_t *runtime)
{
    app_alarm_flags_t alarm_flags;

    alarm_flags = runtime->ai_snapshot.alarm_flags;
    /* A sensor fault takes precedence over model and resource faults. */
    if ((alarm_flags & APP_ALARM_FLAG_SENSOR_FAULT) != 0U)
    {
        return "ERR SENSOR";
    }
    if ((alarm_flags & APP_ALARM_FLAG_MODEL_FAULT) != 0U)
    {
        return "ERR MODEL";
    }
    /* A dropped window is an unavailable/busy processing resource. */
    if ((alarm_flags & APP_ALARM_FLAG_WINDOW_DROP) != 0U)
    {
        return "ERR BUSY";
    }
    return "ERR BUSY";
}

static void app_ai_send_fault_response(app_runtime_t *runtime)
{
    (void)App_TxSendf(runtime,
                      "%s\r\n",
                      app_ai_fault_response(runtime));
}

static uint16_t app_ai_stack_words(TaskHandle_t task)
{
    UBaseType_t high_water;

    if (task == (TaskHandle_t)0)
    {
        return 0U;
    }
    high_water = uxTaskGetStackHighWaterMark(task);
    return app_ai_saturate_u16((uint32_t)high_water);
}

static void app_ai_refresh_stats(app_runtime_t *runtime)
{
    uint32_t sample_total;
    uint32_t window_total;
    uint32_t window_drop_total;
    uint32_t i2c_error_total;
    uint32_t sensor_reset_total;
    uint32_t rx_overflow_total;
    uint32_t invalid_command_total;

    sample_total = runtime->producer.sample_total;
    window_total = runtime->producer.window_total;
    window_drop_total = runtime->producer.window_drop_total;
    i2c_error_total = runtime->producer.i2c_error_total;
    sensor_reset_total = runtime->producer.sensor_reset_total;
    rx_overflow_total = runtime->producer.rx_overflow_total;
    invalid_command_total = runtime->producer.invalid_command_total;

    runtime->ai_snapshot.stats.sample_count =
        sample_total - runtime->baseline_sample_total;
    runtime->ai_snapshot.stats.window_count =
        window_total - runtime->baseline_window_total;
    runtime->ai_snapshot.stats.window_drop_count =
        window_drop_total - runtime->baseline_window_drop_total;
    runtime->ai_snapshot.stats.i2c_error_count =
        i2c_error_total - runtime->baseline_i2c_error_total;
    runtime->ai_snapshot.stats.sensor_reset_count =
        sensor_reset_total - runtime->baseline_sensor_reset_total;
    runtime->ai_snapshot.stats.rx_overflow_count =
        rx_overflow_total - runtime->baseline_rx_overflow_total;
    runtime->ai_snapshot.stats.invalid_command_count =
        invalid_command_total - runtime->baseline_invalid_command_total;
    runtime->ai_snapshot.stats.inference_count = runtime->inference_count;
    runtime->ai_snapshot.stats.inference_total_us = runtime->inference_total_us;
    if (runtime->inference_count != 0U)
    {
        runtime->ai_snapshot.stats.inference_average_us =
            runtime->inference_total_us / runtime->inference_count;
    }
    else
    {
        runtime->ai_snapshot.stats.inference_average_us = 0U;
    }
    runtime->ai_snapshot.stats.imu_task_min_stack_words =
        app_ai_stack_words(runtime->imu_task);
    runtime->ai_snapshot.stats.command_task_min_stack_words =
        app_ai_stack_words(runtime->command_task);
    runtime->ai_snapshot.stats.ai_task_min_stack_words =
        app_ai_stack_words(runtime->ai_task);
}

static void app_ai_record_latency(app_runtime_t *runtime,
                                  uint32_t latency_us)
{
    uint32_t dropped_latency_us;
    uint16_t index;
    uint16_t insert_index;
    uint16_t p95_index;
    uint32_t value;

    /* Count, total, average, P95 and maximum all describe the same bounded
     * window: the most recent up to 512 completed inferences. */
    if (runtime->inference_history_count >= APP_INFERENCE_HISTORY_LENGTH)
    {
        dropped_latency_us =
            runtime->inference_latency_history[runtime->inference_history_next];
        if (runtime->inference_total_us >= dropped_latency_us)
        {
            runtime->inference_total_us -= dropped_latency_us;
        }
        else
        {
            runtime->inference_total_us = 0U;
        }
    }
    else
    {
        runtime->inference_history_count++;
    }

    /* The published field is 32-bit; saturation is explicit, never invented. */
    if (UINT32_MAX - runtime->inference_total_us < latency_us)
    {
        runtime->inference_total_us = UINT32_MAX;
    }
    else
    {
        runtime->inference_total_us += latency_us;
    }

    runtime->inference_latency_history[runtime->inference_history_next] =       
        latency_us;
    runtime->inference_history_next++;
    if (runtime->inference_history_next >= APP_INFERENCE_HISTORY_LENGTH)
    {
        runtime->inference_history_next = 0U;
    }

    for (index = 0U;
         index < runtime->inference_history_count;
         index++)
    {
        s_app_ai_latency_sorted[index] =
            runtime->inference_latency_history[index];
    }
    for (index = 1U;
         index < runtime->inference_history_count;
         index++)
    {
        value = s_app_ai_latency_sorted[index];
        insert_index = index;
        while ((insert_index > 0U) &&
               (s_app_ai_latency_sorted[insert_index - 1U] > value))
        {
            s_app_ai_latency_sorted[insert_index] =
                s_app_ai_latency_sorted[insert_index - 1U];
            insert_index--;
        }
        s_app_ai_latency_sorted[insert_index] = value;
    }
    runtime->inference_count = runtime->inference_history_count;
    p95_index = (uint16_t)(((uint32_t)runtime->inference_history_count *        
                            95U + 99U) / 100U);
    if (p95_index != 0U)
    {
        p95_index--;
    }
    runtime->ai_snapshot.stats.inference_p95_us =
        s_app_ai_latency_sorted[p95_index];
    runtime->ai_snapshot.stats.inference_max_us =
        s_app_ai_latency_sorted[runtime->inference_history_count - 1U];
}

static void app_ai_reset_stats(app_runtime_t *runtime)
{
    uint16_t index;

    runtime->baseline_sample_total = runtime->producer.sample_total;
    runtime->baseline_window_total = runtime->producer.window_total;
    runtime->baseline_window_drop_total = runtime->producer.window_drop_total;
    runtime->baseline_i2c_error_total = runtime->producer.i2c_error_total;
    runtime->baseline_sensor_reset_total = runtime->producer.sensor_reset_total;
    runtime->baseline_rx_overflow_total = runtime->producer.rx_overflow_total;
    runtime->baseline_invalid_command_total =
        runtime->producer.invalid_command_total;
    runtime->inference_history_count = 0U;
    runtime->inference_history_next = 0U;
    runtime->inference_total_us = 0U;
    runtime->inference_count = 0U;
    for (index = 0U; index < APP_INFERENCE_HISTORY_LENGTH; index++)
    {
        runtime->inference_latency_history[index] = 0U;
    }
    runtime->ai_snapshot.stats.model_error_count = 0U;
    runtime->ai_snapshot.stats.unknown_count = 0U;
    runtime->ai_snapshot.stats.inference_p95_us = 0U;
    runtime->ai_snapshot.stats.inference_max_us = 0U;
    app_ai_refresh_stats(runtime);
}

static void app_ai_set_alarm_for_result(app_runtime_t *runtime,
                                        app_class_t *class_id,
                                        uint16_t confidence_milli,
                                        app_alarm_flags_t *alarm_flags)
{
    *alarm_flags = runtime->ai_snapshot.alarm_flags;
    if (confidence_milli < runtime->ai_snapshot.threshold_milli)
    {
        *class_id = APP_CLASS_UNKNOWN;
    }

    /* A fault owns the alarm output.  A late result must never overwrite it
     * with a normal classification or clear the fault indication. */
    if ((runtime->ai_snapshot.mode == APP_MODE_FAULT) ||
        ((runtime->ai_snapshot.alarm_flags & APP_AI_FAULT_ALARM_FLAGS) != 0U))
    {
        return;
    }

    switch (*class_id)
    {
        case APP_CLASS_IMPACT:
            runtime->vibration_streak = 0U;
            runtime->stable_streak = 0U;
            runtime->ai_snapshot.alarm_flags |= APP_ALARM_FLAG_IMPACT;
            (void)BSP_Buzzer_Request(BSP_BUZZER_PATTERN_IMPACT,
                                      App_TimeNowMs());
            break;

        case APP_CLASS_VIBRATION:
            runtime->stable_streak = 0U;
            if (runtime->vibration_streak != UINT16_MAX)
            {
                runtime->vibration_streak++;
            }
            if (runtime->vibration_streak >= 2U)
            {
                runtime->ai_snapshot.alarm_flags |= APP_ALARM_FLAG_VIBRATION;
                (void)BSP_Buzzer_Request(BSP_BUZZER_PATTERN_VIBRATION,
                                          App_TimeNowMs());
            }
            break;

        case APP_CLASS_STABLE:
            runtime->vibration_streak = 0U;
            if (runtime->stable_streak != UINT16_MAX)
            {
                runtime->stable_streak++;
            }
            if (runtime->stable_streak >= 3U)
            {
                runtime->ai_snapshot.alarm_flags = APP_ALARM_FLAG_NONE;
                BSP_Buzzer_Stop();
            }
            /* The first two stable windows are evidence only; they do not
             * clear a previously latched ordinary alarm. */
            break;

        case APP_CLASS_UNKNOWN:
        default:
            runtime->vibration_streak = 0U;
            runtime->stable_streak = 0U;
            /* UNKNOWN breaks streaks but does not clear a latched alarm. */
            runtime->ai_snapshot.alarm_flags |= APP_ALARM_FLAG_UNKNOWN;
            runtime->ai_snapshot.stats.unknown_count++;
            break;
    }

    *alarm_flags = runtime->ai_snapshot.alarm_flags;
}

static uint8_t app_ai_export_window(app_runtime_t *runtime,
                                    component_window_id_t buffer_id,
                                    const component_window_metadata_t *metadata)
{
    const int16_t *data;
    const uint32_t *ticks;
    uint16_t sample_index;

    data = component_window_read_data(&runtime->window_pool, buffer_id);
    ticks = component_window_read_ticks(&runtime->window_pool, buffer_id);
    if ((data == (const int16_t *)0) ||
        (ticks == (const uint32_t *)0) ||
        (metadata == (const component_window_metadata_t *)0))
    {
        return 0U;
    }

    /* Capture is an intentional serial blocking path; force silence first. */
    BSP_Buzzer_Stop();
    if (App_TxSendf(runtime,
                    "OK DATASET %lu %s %lu\r\n",
                    (unsigned long)metadata->session_id,
                    app_label_name(metadata->label),
                    (unsigned long)metadata->sequence) == 0U)
    {
        return 0U;
    }
    for (sample_index = 0U;
         sample_index < COMPONENT_WINDOW_SAMPLE_COUNT;
         sample_index++)
    {
        if (App_TxSendf(runtime,
                        "D,%lu,%s,%lu,%u,%lu,%d,%d,%d\r\n",
                        (unsigned long)metadata->session_id,
                        app_label_name(metadata->label),
                        (unsigned long)metadata->sequence,
                        (unsigned int)sample_index,
                        (unsigned long)ticks[sample_index],
                        (int)data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 0U],
                        (int)data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 1U],
                        (int)data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 2U]) == 0U)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t app_ai_infer_window(app_runtime_t *runtime,
                                   component_window_id_t buffer_id,
                                   const component_window_metadata_t *metadata) 
{
    const int16_t *data;
    app_class_t class_id;
    uint16_t confidence_milli;
    uint32_t start_cycle;
    uint32_t end_cycle;
    uint32_t cycle_delta;
    uint32_t latency_us;
    app_alarm_flags_t alarm_flags;
    ai_adapter_status_t status;
    uint16_t sample_index;
    uint8_t axis_index;

    data = component_window_read_data(&runtime->window_pool, buffer_id);
    if ((data == (const int16_t *)0) ||
        (metadata == (const component_window_metadata_t *)0))
    {
        return 0U;
    }

    /* Keep the model boundary in physical g units without guessing its ABI. */
    for (sample_index = 0U;
         sample_index < COMPONENT_WINDOW_SAMPLE_COUNT;
         sample_index++)
    {
        for (axis_index = 0U;
             axis_index < COMPONENT_WINDOW_AXIS_COUNT;
             axis_index++)
        {
            s_app_ai_model_input[sample_index][axis_index] =
                BSP_MPU6050_RawToG(
                    data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) +
                         axis_index]);
        }
    }

    class_id = APP_CLASS_UNKNOWN;
    confidence_milli = 0U;
    start_cycle = App_TimeCycleToken();
    status = ai_adapter_infer(&runtime->ai_adapter,
                              &s_app_ai_model_input[0][0],
                              COMPONENT_WINDOW_SAMPLE_COUNT,
                              COMPONENT_WINDOW_AXIS_COUNT,
                              &class_id,
                              &confidence_milli);
    end_cycle = App_TimeCycleToken();
    cycle_delta = App_TimeCycleDelta(start_cycle, end_cycle);
    latency_us = App_TimeCycleDeltaToUs(cycle_delta);
    if (status != AI_ADAPTER_STATUS_READY)
    {
        runtime->ai_snapshot.stats.model_error_count++;
        app_ai_enter_fault(runtime, APP_ALARM_FLAG_MODEL_FAULT);
        (void)App_TxSendf(runtime, "ERR MODEL\r\n");
        return 0U;
    }
    if ((confidence_milli > 1000U) ||
        ((class_id != APP_CLASS_STABLE) &&
         (class_id != APP_CLASS_VIBRATION) &&
         (class_id != APP_CLASS_IMPACT)))
    {
        runtime->ai_snapshot.stats.model_error_count++;
        app_ai_enter_fault(runtime, APP_ALARM_FLAG_MODEL_FAULT);
        (void)App_TxSendf(runtime, "ERR MODEL\r\n");
        return 0U;
    }

    app_ai_set_alarm_for_result(runtime,
                                &class_id,
                                confidence_milli,
                                &alarm_flags);
    runtime->ai_snapshot.last_class = class_id;
    runtime->ai_snapshot.last_confidence_milli = confidence_milli;
    runtime->ai_snapshot.last_latency_us = latency_us;
    runtime->ai_snapshot.last_window_sequence = metadata->sequence;
    runtime->ai_snapshot.alarm_flags = alarm_flags;
    app_ai_record_latency(runtime, latency_us);
    BSP_LED_Set((uint8_t)(alarm_flags != APP_ALARM_FLAG_NONE));
    if (App_TxSendf(runtime,
                    "R,%lu,%s,%u,%lu,%lu\r\n",
                    (unsigned long)metadata->sequence,
                    app_class_name(class_id),
                    (unsigned int)confidence_milli,
                    (unsigned long)latency_us,
                    (unsigned long)alarm_flags) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static void app_ai_process_window(app_runtime_t *runtime,
                                  const app_window_queue_item_t *item)
{
    const component_window_metadata_t *metadata;
    uint8_t result;

    if (component_window_begin_processing(&runtime->window_pool,
                                          item->buffer_id) != COMPONENT_WINDOW_OK)
    {
        app_runtime_mark_resource_fault(runtime);
        return;
    }
    metadata = component_window_read_metadata(&runtime->window_pool,
                                              item->buffer_id);
    result = 1U;
    if (runtime->ai_snapshot.mode == APP_MODE_CAPTURE)
    {
        result = app_ai_export_window(runtime, item->buffer_id, metadata);
    }
    else if (runtime->ai_snapshot.mode == APP_MODE_INFERENCE)
    {
        result = app_ai_infer_window(runtime, item->buffer_id, metadata);
    }
    if (result == 0U)
    {
        BSP_Buzzer_Stop();
    }
    app_ai_send_free(runtime, item->buffer_id, metadata);
}

static void app_ai_handle_status(app_runtime_t *runtime)
{
    app_system_snapshot_t *snapshot;

    app_ai_refresh_stats(runtime);
    snapshot = &runtime->ai_snapshot;
    (void)App_TxSendf(runtime,
                      "OK STATUS mode=%s sensor=%u model=%u class=%s conf=%u alarm=%lu samples=%lu windows=%lu drops=%lu i2c=%lu sensor_reset=%lu model_err=%lu unknown=%lu rxov=%lu invalid=%lu infer_count=%lu infer_total_us=%lu avg_us=%lu p95_us=%lu max_us=%lu stack_imu=%u stack_cmd=%u stack_ai=%u\r\n",
                      app_mode_name(snapshot->mode),
                      (unsigned int)snapshot->sensor_ready,
                      (unsigned int)snapshot->model_ready,
                      app_class_name(snapshot->last_class),
                      (unsigned int)snapshot->last_confidence_milli,
                      (unsigned long)snapshot->alarm_flags,
                      (unsigned long)snapshot->stats.sample_count,
                      (unsigned long)snapshot->stats.window_count,
                      (unsigned long)snapshot->stats.window_drop_count,
                      (unsigned long)snapshot->stats.i2c_error_count,
                      (unsigned long)snapshot->stats.sensor_reset_count,
                      (unsigned long)snapshot->stats.model_error_count,
                      (unsigned long)snapshot->stats.unknown_count,
                      (unsigned long)snapshot->stats.rx_overflow_count,
                      (unsigned long)snapshot->stats.invalid_command_count,
                      (unsigned long)snapshot->stats.inference_count,
                      (unsigned long)snapshot->stats.inference_total_us,
                      (unsigned long)snapshot->stats.inference_average_us,
                      (unsigned long)snapshot->stats.inference_p95_us,
                      (unsigned long)snapshot->stats.inference_max_us,
                      (unsigned int)snapshot->stats.imu_task_min_stack_words,
                      (unsigned int)snapshot->stats.command_task_min_stack_words,
                      (unsigned int)snapshot->stats.ai_task_min_stack_words);
}

static void app_ai_handle_command(app_runtime_t *runtime,
                                  const app_control_message_t *message)
{
    ai_adapter_status_t adapter_status;
    uint8_t mode_changed;

    switch (message->command)
    {
        case APP_CONTROL_STATUS:
            app_ai_handle_status(runtime);
            break;

        case APP_CONTROL_AI_INFO:
            (void)App_TxSendf(runtime,
                              "OK AI INFO state=%s\r\n",
                              ai_adapter_status_name(&runtime->ai_adapter));
            break;

        case APP_CONTROL_AI_START:
            if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
            {
                app_ai_send_fault_response(runtime);
                break;
            }
            if (runtime->sensor_ready == 0U)
            {
                app_ai_enter_fault(runtime, APP_ALARM_FLAG_SENSOR_FAULT);
                (void)App_TxSendf(runtime, "ERR SENSOR\r\n");
                break;
            }
            if (runtime->ai_snapshot.mode != APP_MODE_IDLE)
            {
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
                break;
            }
            adapter_status = ai_adapter_start(&runtime->ai_adapter);
            if (adapter_status != AI_ADAPTER_STATUS_READY)
            {
                runtime->ai_snapshot.stats.model_error_count++;
                app_ai_enter_fault(runtime, APP_ALARM_FLAG_MODEL_FAULT);
                (void)App_TxSendf(runtime, "ERR MODEL\r\n");
                break;
            }
            runtime->ai_snapshot.model_ready = 1U;
            mode_changed = app_ai_change_mode(runtime, APP_MODE_INFERENCE);     
            if (mode_changed == 0U)
            {
                ai_adapter_stop(&runtime->ai_adapter);
                runtime->ai_snapshot.model_ready = 0U;
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
            }
            else
            {
                (void)App_TxSendf(runtime, "OK AI START INFERENCE\r\n");
            }
            break;

        case APP_CONTROL_AI_STOP:
            /*
             * Close the inference gate before changing modes or draining
             * ready windows. This makes STOP safe even if an older window was
             * committed just before the command was handled.
             */
            ai_adapter_stop(&runtime->ai_adapter);
            runtime->ai_snapshot.model_ready = 0U;
            if ((runtime->ai_snapshot.mode == APP_MODE_INFERENCE) ||
                (runtime->ai_snapshot.mode == APP_MODE_CAPTURE))
            {
                if (app_ai_change_mode(runtime, APP_MODE_IDLE) == 0U)
                {
                    app_ai_stop_sampling(runtime);
                }
            }
            else
            {
                /*
                 * Idempotent STOP also closes sampling and discards a ready
                 * item that raced with a prior transition while idle or faulted.
                 */
                app_ai_stop_sampling(runtime);
            }
            if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
            {
                app_ai_send_fault_response(runtime);
            }
            else
            {
                (void)App_TxSendf(runtime, "OK AI STOP IDLE\r\n");
            }
            break;

        case APP_CONTROL_AI_THRESHOLD:
            if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
            {
                app_ai_send_fault_response(runtime);
                break;
            }
            runtime->ai_snapshot.threshold_milli =
                app_ai_saturate_u16(message->threshold_milli);
            (void)App_TxSendf(runtime,
                              "OK AI THRESHOLD %u\r\n",
                              (unsigned int)runtime->ai_snapshot.threshold_milli);
            break;

        case APP_CONTROL_DATASET_START:
            if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
            {
                app_ai_send_fault_response(runtime);
                break;
            }
            if (runtime->sensor_ready == 0U)
            {
                app_ai_enter_fault(runtime, APP_ALARM_FLAG_SENSOR_FAULT);
                app_ai_send_fault_response(runtime);
                break;
            }
            if (runtime->ai_snapshot.mode != APP_MODE_IDLE)
            {
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
                break;
            }
            runtime->capture_label = message->label;
            runtime->session_id++;
            if (app_ai_change_mode(runtime, APP_MODE_CAPTURE) == 0U)
            {
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
            }
            else
            {
                (void)App_TxSendf(runtime,
                                  "OK DATASET START %s\r\n",
                                  app_label_name(message->label));
            }
            break;

        case APP_CONTROL_DATASET_STOP:
            if (runtime->ai_snapshot.mode == APP_MODE_CAPTURE)
            {
                (void)app_ai_change_mode(runtime, APP_MODE_IDLE);
                runtime->capture_label = APP_LABEL_NONE;
                (void)App_TxSendf(runtime, "OK DATASET STOP\r\n");
            }
            else if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
            {
                app_ai_send_fault_response(runtime);
            }
            else
            {
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
            }
            break;

        case APP_CONTROL_STATS_RESET:
            app_ai_reset_stats(runtime);
            (void)App_TxSendf(runtime, "OK STATS RESET\r\n");
            break;

        case APP_CONTROL_HELP:
            (void)App_TxSendf(runtime,
                              "OK HELP STATUS|AI INFO|AI START|AI STOP|AI THRESHOLD <0.00-1.00>|DATASET START <STABLE|VIBRATION|IMPACT>|DATASET STOP|STATS RESET|HELP\r\n");
            break;

        case APP_CONTROL_NONE:
        default:
            (void)App_TxSendf(runtime, "ERR ARG\r\n");
            break;
    }
}

static void app_ai_publish(app_runtime_t *runtime)
{
    runtime->ai_snapshot.capture_label = runtime->capture_label;
    runtime->ai_snapshot.session_id = runtime->session_id;
    app_ai_refresh_stats(runtime);
    (void)app_state_publish(&runtime->state, &runtime->ai_snapshot);
    if ((runtime->ai_snapshot.mode == APP_MODE_FAULT) ||
        (runtime->ai_snapshot.alarm_flags != APP_ALARM_FLAG_NONE))
    {
        BSP_LED_Set(1U);
    }
    else
    {
        BSP_LED_Set(0U);
    }
}

static void app_ai_try_enable_sampling(app_runtime_t *runtime)
{
    if ((runtime->pending_sampling_enable != 0U) &&
        (runtime->imu_active_id == COMPONENT_WINDOW_ID_INVALID))
    {
        app_ai_flush_ready(runtime);
        if (runtime->imu_active_id == COMPONENT_WINDOW_ID_INVALID)
        {
            runtime->imu_enabled = 1U;
            runtime->pending_sampling_enable = 0U;
        }
    }
}

void App_AiTask(void *argument)
{
    app_runtime_t *runtime;
    app_control_message_t message;
    app_window_queue_item_t item;

    runtime = (app_runtime_t *)argument;
    /* Model initialization is explicit: AI START owns the only start call. */
    runtime->ai_snapshot.model_ready = 0U;
    if (runtime->sensor_ready == 0U)
    {
        app_ai_enter_fault(runtime, APP_ALARM_FLAG_SENSOR_FAULT);
    }
    app_ai_publish(runtime);
    /* READY is emitted by a live task, not merely by main before the
     * scheduler starts. */
    (void)App_TxSendf(runtime, "READY\r\n");

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, APP_AI_WAIT_TICKS);

        if (runtime->producer.sensor_fault_pending != 0U)
        {
            runtime->producer.sensor_fault_pending = 0U;
            app_ai_enter_fault(runtime, APP_ALARM_FLAG_SENSOR_FAULT);
        }
        if (runtime->producer.resource_fault_pending != 0U)
        {
            runtime->producer.resource_fault_pending = 0U;
            app_ai_enter_fault(runtime, APP_ALARM_FLAG_WINDOW_DROP);
        }
        if (runtime->producer.tx_fault_pending != 0U)
        {
            runtime->producer.tx_fault_pending = 0U;
            /* Any I/O failure takes the output actuator to the safe state. */
            BSP_Buzzer_Stop();
        }

        while (xQueueReceive(runtime->control_queue,
                             &message,
                             (TickType_t)0) == pdPASS)
        {
            app_ai_handle_command(runtime, &message);
        }

        app_ai_try_enable_sampling(runtime);
        while (xQueueReceive(runtime->ready_buffer_queue,
                             &item,
                             (TickType_t)0) == pdPASS)
        {
            if ((runtime->producer.sensor_fault_pending != 0U) ||
                (runtime->producer.resource_fault_pending != 0U))
            {
                /* FAULT owns the item only long enough to release it safely. */
                app_ai_discard_ready_item(runtime, &item);
                break;
            }
            if (runtime->ai_snapshot.mode == APP_MODE_CAPTURE ||
                runtime->ai_snapshot.mode == APP_MODE_INFERENCE)
            {
                app_ai_process_window(runtime, &item);
            }
            else
            {
                app_ai_discard_ready_item(runtime, &item);
            }
        }

        if (runtime->ai_snapshot.mode == APP_MODE_FAULT)
        {
            app_ai_flush_ready(runtime);
        }
        app_ai_publish(runtime);
    }
}
