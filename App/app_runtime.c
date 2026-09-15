#include "app_runtime.h"

static void app_runtime_clear_producer(
    app_runtime_producer_counters_t *producer)
{
    producer->sample_total = 0U;
    producer->window_total = 0U;
    producer->window_drop_total = 0U;
    producer->i2c_error_total = 0U;
    producer->sensor_reset_total = 0U;
    producer->rx_overflow_total = 0U;
    producer->invalid_command_total = 0U;
    producer->sensor_fault_pending = 0U;
    producer->resource_fault_pending = 0U;
    producer->tx_fault_pending = 0U;
}

static void app_runtime_delete_resources(app_runtime_t *runtime)
{
    if (runtime->free_buffer_queue != (QueueHandle_t)0)
    {
        vQueueDelete(runtime->free_buffer_queue);
        runtime->free_buffer_queue = (QueueHandle_t)0;
    }
    if (runtime->ready_buffer_queue != (QueueHandle_t)0)
    {
        vQueueDelete(runtime->ready_buffer_queue);
        runtime->ready_buffer_queue = (QueueHandle_t)0;
    }
    if (runtime->control_queue != (QueueHandle_t)0)
    {
        vQueueDelete(runtime->control_queue);
        runtime->control_queue = (QueueHandle_t)0;
    }
    if (runtime->tx_mutex != (SemaphoreHandle_t)0)
    {
        vSemaphoreDelete(runtime->tx_mutex);
        runtime->tx_mutex = (SemaphoreHandle_t)0;
    }
}

uint8_t app_runtime_init(app_runtime_t *runtime, uint8_t sensor_ready)
{
    app_window_queue_item_t free_item;
    component_window_id_t buffer_id;

    if (runtime == (app_runtime_t *)0)
    {
        return 0U;
    }

    component_ring_buffer_init(&runtime->rx_ring);
    component_window_pool_init(&runtime->window_pool);
    runtime->free_buffer_queue = xQueueCreate(
        APP_WINDOW_QUEUE_LENGTH, sizeof(app_window_queue_item_t));
    runtime->ready_buffer_queue = xQueueCreate(
        APP_WINDOW_QUEUE_LENGTH, sizeof(app_window_queue_item_t));
    runtime->control_queue = xQueueCreate(
        APP_CONTROL_QUEUE_LENGTH, sizeof(app_control_message_t));
    runtime->tx_mutex = xSemaphoreCreateMutex();
    if ((runtime->free_buffer_queue == (QueueHandle_t)0) ||
        (runtime->ready_buffer_queue == (QueueHandle_t)0) ||
        (runtime->control_queue == (QueueHandle_t)0) ||
        (runtime->tx_mutex == (SemaphoreHandle_t)0))
    {
        app_runtime_delete_resources(runtime);
        return 0U;
    }

    free_item.metadata.sequence = 0U;
    free_item.metadata.timestamp_tick = 0U;
    free_item.metadata.session_id = 0U;
    free_item.metadata.sample_count = 0U;
    free_item.metadata.label = APP_LABEL_NONE;
    free_item.metadata.reserved = 0U;
    for (buffer_id = 0U;
         buffer_id < COMPONENT_WINDOW_POOL_COUNT;
         buffer_id++)
    {
        free_item.buffer_id = buffer_id;
        if (xQueueSend(runtime->free_buffer_queue,
                       &free_item,
                       (TickType_t)0) != pdPASS)
        {
            app_runtime_delete_resources(runtime);
            return 0U;
        }
    }

    runtime->imu_task = (TaskHandle_t)0;
    runtime->command_task = (TaskHandle_t)0;
    runtime->ai_task = (TaskHandle_t)0;
    runtime->sensor_ready = sensor_ready;
    runtime->imu_enabled = 0U;
    runtime->sampling_generation = 1U;
    runtime->imu_active_id = COMPONENT_WINDOW_ID_INVALID;
    runtime->imu_active_generation = 0U;
    runtime->next_window_sequence = 1U;
    runtime->capture_label = APP_LABEL_NONE;
    runtime->session_id = 0U;
    app_runtime_clear_producer(&runtime->producer);
    ai_adapter_init(&runtime->ai_adapter);
    app_state_init(&runtime->state,
                   (sensor_ready != 0U) ? APP_MODE_IDLE : APP_MODE_FAULT,
                   sensor_ready,
                   0U);

    runtime->ai_snapshot.mode = (sensor_ready != 0U) ? APP_MODE_IDLE :
                                APP_MODE_FAULT;
    runtime->ai_snapshot.sensor_ready = sensor_ready;
    runtime->ai_snapshot.model_ready = 0U;
    runtime->ai_snapshot.reserved0 = 0U;
    runtime->ai_snapshot.capture_label = APP_LABEL_NONE;
    runtime->ai_snapshot.last_class = APP_CLASS_UNKNOWN;
    runtime->ai_snapshot.threshold_milli = 800U;
    runtime->ai_snapshot.session_id = 0U;
    runtime->ai_snapshot.last_window_sequence = 0U;
    runtime->ai_snapshot.last_confidence_milli = 0U;
    runtime->ai_snapshot.reserved1 = 0U;
    runtime->ai_snapshot.last_latency_us = 0U;
    runtime->ai_snapshot.alarm_flags = (sensor_ready != 0U) ?
                                        APP_ALARM_FLAG_NONE :
                                        APP_ALARM_FLAG_SENSOR_FAULT;
    runtime->ai_snapshot.stats.sample_count = 0U;
    runtime->ai_snapshot.stats.window_count = 0U;
    runtime->ai_snapshot.stats.window_drop_count = 0U;
    runtime->ai_snapshot.stats.i2c_error_count = 0U;
    runtime->ai_snapshot.stats.sensor_reset_count = 0U;
    runtime->ai_snapshot.stats.model_error_count = 0U;
    runtime->ai_snapshot.stats.unknown_count = 0U;
    runtime->ai_snapshot.stats.rx_overflow_count = 0U;
    runtime->ai_snapshot.stats.invalid_command_count = 0U;
    runtime->ai_snapshot.stats.inference_count = 0U;
    runtime->ai_snapshot.stats.inference_total_us = 0U;
    runtime->ai_snapshot.stats.inference_average_us = 0U;
    runtime->ai_snapshot.stats.inference_p95_us = 0U;
    runtime->ai_snapshot.stats.inference_max_us = 0U;
    runtime->ai_snapshot.stats.imu_task_min_stack_words = 0U;
    runtime->ai_snapshot.stats.command_task_min_stack_words = 0U;
    runtime->ai_snapshot.stats.ai_task_min_stack_words = 0U;
    runtime->inference_history_count = 0U;
    runtime->inference_history_next = 0U;
    runtime->inference_total_us = 0U;
    runtime->inference_count = 0U;
    runtime->vibration_streak = 0U;
    runtime->stable_streak = 0U;
    runtime->baseline_sample_total = 0U;
    runtime->baseline_window_total = 0U;
    runtime->baseline_window_drop_total = 0U;
    runtime->baseline_i2c_error_total = 0U;
    runtime->baseline_sensor_reset_total = 0U;
    runtime->baseline_rx_overflow_total = 0U;
    runtime->baseline_invalid_command_total = 0U;
    runtime->buzzer_fault_announced = 0U;
    runtime->pending_sampling_enable = 0U;

    return 1U;
}

void app_runtime_signal_ai(app_runtime_t *runtime)
{
    if ((runtime != (app_runtime_t *)0) &&
        (runtime->ai_task != (TaskHandle_t)0))
    {
        (void)xTaskNotifyGive(runtime->ai_task);
    }
}

BaseType_t app_runtime_send_free(
    app_runtime_t *runtime,
    component_window_id_t buffer_id,
    const component_window_metadata_t *metadata)
{
    app_window_queue_item_t item;

    if ((runtime == (app_runtime_t *)0) ||
        (runtime->free_buffer_queue == (QueueHandle_t)0))
    {
        return errQUEUE_FULL;
    }
    item.buffer_id = buffer_id;
    if (metadata != (const component_window_metadata_t *)0)
    {
        item.metadata = *metadata;
    }
    else
    {
        item.metadata.sequence = 0U;
        item.metadata.timestamp_tick = 0U;
        item.metadata.session_id = 0U;
        item.metadata.sample_count = 0U;
        item.metadata.label = APP_LABEL_NONE;
        item.metadata.reserved = 0U;
    }
    return xQueueSend(runtime->free_buffer_queue, &item, (TickType_t)0);
}

void app_runtime_mark_resource_fault(app_runtime_t *runtime)
{
    if (runtime != (app_runtime_t *)0)
    {
        runtime->producer.resource_fault_pending = 1U;
        app_runtime_signal_ai(runtime);
    }
}
