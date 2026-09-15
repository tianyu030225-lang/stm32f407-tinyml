#include "imu_task.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_i2c.h"
#include "bsp_mpu6050.h"
#include "app_runtime.h"

#define APP_IMU_PERIOD_TICKS          (pdMS_TO_TICKS(10U))
#define APP_IMU_MAX_CONSECUTIVE_I2C  (3U)

static void app_imu_clear_metadata(component_window_metadata_t *metadata)
{
    metadata->sequence = 0U;
    metadata->timestamp_tick = 0U;
    metadata->session_id = 0U;
    metadata->sample_count = 0U;
    metadata->label = APP_LABEL_NONE;
    metadata->reserved = 0U;
}

static void app_imu_return_free(
    app_runtime_t *runtime,
    component_window_id_t buffer_id)
{
    component_window_metadata_t metadata;

    app_imu_clear_metadata(&metadata);
    if (app_runtime_send_free(runtime, buffer_id, &metadata) != pdPASS)
    {
        app_runtime_mark_resource_fault(runtime);
    }
}

static void app_imu_abort_active(app_runtime_t *runtime)
{
    component_window_id_t buffer_id;
    component_window_status_t status;

    buffer_id = runtime->imu_active_id;
    if (buffer_id == COMPONENT_WINDOW_ID_INVALID)
    {
        return;
    }

    status = component_window_abort_fill(&runtime->window_pool, buffer_id);
    if (status == COMPONENT_WINDOW_OK)
    {
        app_imu_return_free(runtime, buffer_id);
    }
    else
    {
        /* A failed abort means another state/owner already owns this ID.  Do
         * not enqueue it again: that would create a duplicate free ID. */
        app_runtime_mark_resource_fault(runtime);
    }
    runtime->imu_active_id = COMPONENT_WINDOW_ID_INVALID;
    runtime->imu_active_generation = 0U;
}

static uint8_t app_imu_begin_window(app_runtime_t *runtime,
                                    uint8_t *drop_reported)
{
    app_window_queue_item_t item;
    component_window_metadata_t *metadata;
    component_window_id_t buffer_id;
    uint32_t generation;

    if (xQueueReceive(runtime->free_buffer_queue,
                      &item,
                      (TickType_t)0) != pdPASS)
    {
        if (*drop_reported == 0U)
        {
            runtime->producer.window_drop_total++;
            *drop_reported = 1U;
        }
        return 0U;
    }

    buffer_id = item.buffer_id;
    if (component_window_begin_fill(&runtime->window_pool, buffer_id) !=
        COMPONENT_WINDOW_OK)
    {
        /* The queue removed this ID, so return it only when the pool still
         * confirms that it is free.  A non-free state belongs to another
         * owner and must not be returned a second time. */
        if (component_window_state(&runtime->window_pool, buffer_id) ==
            COMPONENT_WINDOW_STATE_FREE)
        {
            app_imu_return_free(runtime, buffer_id);
        }
        app_runtime_mark_resource_fault(runtime);
        return 0U;
    }

    generation = runtime->sampling_generation;
    runtime->imu_active_id = buffer_id;
    runtime->imu_active_generation = generation;
    metadata = component_window_write_metadata(&runtime->window_pool,
                                               buffer_id);
    if (metadata == (component_window_metadata_t *)0)
    {
        app_imu_abort_active(runtime);
        app_runtime_mark_resource_fault(runtime);
        return 0U;
    }

    metadata->sequence = runtime->next_window_sequence++;
    metadata->timestamp_tick = 0U;
    metadata->session_id = runtime->session_id;
    metadata->sample_count = 0U;
    metadata->label = runtime->capture_label;
    metadata->reserved = 0U;
    *drop_reported = 0U;
    return 1U;
}

static void app_imu_commit_window(
    app_runtime_t *runtime,
    uint16_t sample_index)
{
    component_window_id_t buffer_id;
    component_window_metadata_t metadata;
    app_window_queue_item_t item;
    const component_window_metadata_t *stored_metadata;

    buffer_id = runtime->imu_active_id;
    stored_metadata = component_window_read_metadata(&runtime->window_pool,
                                                     buffer_id);
    if (stored_metadata == (const component_window_metadata_t *)0)
    {
        app_imu_abort_active(runtime);
        app_runtime_mark_resource_fault(runtime);
        return;
    }

    metadata = *stored_metadata;
    metadata.sample_count = (uint16_t)(sample_index + 1U);
    if (runtime->sampling_generation != runtime->imu_active_generation ||
        runtime->imu_enabled == 0U)
    {
        app_imu_abort_active(runtime);
        return;
    }

    if (component_window_commit_ready(&runtime->window_pool,
                                      buffer_id,
                                      &metadata) != COMPONENT_WINDOW_OK)
    {
        app_imu_abort_active(runtime);
        app_runtime_mark_resource_fault(runtime);
        return;
    }

    item.buffer_id = buffer_id;
    item.metadata = metadata;
    if (xQueueSend(runtime->ready_buffer_queue,
                   &item,
                   (TickType_t)0) != pdPASS)
    {
        if (component_window_abort_ready(&runtime->window_pool, buffer_id) ==
            COMPONENT_WINDOW_OK)
        {
            app_imu_return_free(runtime, buffer_id);
        }
        runtime->producer.window_drop_total++;
        app_runtime_mark_resource_fault(runtime);
        runtime->imu_active_id = COMPONENT_WINDOW_ID_INVALID;
        runtime->imu_active_generation = 0U;
        return;
    }

    runtime->producer.window_total++;
    runtime->imu_active_id = COMPONENT_WINDOW_ID_INVALID;
    runtime->imu_active_generation = 0U;
    app_runtime_signal_ai(runtime);
}

void App_ImuTask(void *argument)
{
    app_runtime_t *runtime;
    TickType_t last_wake;
    uint16_t sample_index;
    uint8_t consecutive_i2c_errors;
    uint8_t free_drop_reported;
    BSP_MPU6050_AccelRaw sample;
    int16_t *data;
    uint32_t *sample_ticks;
    component_window_metadata_t *metadata;
    uint32_t now_tick;
    BSP_MPU6050_Status read_status;

    runtime = (app_runtime_t *)argument;
    last_wake = xTaskGetTickCount();
    sample_index = 0U;
    consecutive_i2c_errors = 0U;
    free_drop_reported = 0U;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, APP_IMU_PERIOD_TICKS);

        if ((runtime == (app_runtime_t *)0) ||
            (runtime->sensor_ready == 0U) ||
            (runtime->imu_enabled == 0U) ||
            (runtime->producer.sensor_fault_pending != 0U))
        {
            if (runtime != (app_runtime_t *)0)
            {
                app_imu_abort_active(runtime);
            }
            continue;
        }

        if (runtime->imu_active_id == COMPONENT_WINDOW_ID_INVALID)
        {
            if (app_imu_begin_window(runtime, &free_drop_reported) == 0U)
            {
                continue;
            }
            sample_index = 0U;
        }

        if (runtime->sampling_generation != runtime->imu_active_generation)
        {
            app_imu_abort_active(runtime);
            continue;
        }

        read_status = BSP_MPU6050_ReadAccelRaw(&sample,
                                               BSP_I2C1_TIMEOUT_DEFAULT);
        if (read_status != BSP_MPU6050_OK)
        {
            runtime->producer.i2c_error_total++;
            /* BSP_MPU6050_ReadAccelRaw already performs bounded retries.  A
             * failed read starts one additional bounded bus recovery here;
             * this counter records recovery attempts, not claimed successes. */
            (void)BSP_I2C_Recover();
            if (runtime->producer.sensor_reset_total != UINT32_MAX)
            {
                runtime->producer.sensor_reset_total++;
            }
            consecutive_i2c_errors++;
            app_imu_abort_active(runtime);
            if (consecutive_i2c_errors >= APP_IMU_MAX_CONSECUTIVE_I2C)
            {
                runtime->producer.sensor_fault_pending = 1U;
                app_runtime_signal_ai(runtime);
            }
            continue;
        }
        consecutive_i2c_errors = 0U;

        data = component_window_write_data(&runtime->window_pool,
                                           runtime->imu_active_id);
        sample_ticks = component_window_write_ticks(&runtime->window_pool,
                                                     runtime->imu_active_id);
        metadata = component_window_write_metadata(&runtime->window_pool,
                                                   runtime->imu_active_id);
        if ((data == (int16_t *)0) ||
            (sample_ticks == (uint32_t *)0) ||
            (metadata == (component_window_metadata_t *)0))
        {
            app_imu_abort_active(runtime);
            app_runtime_mark_resource_fault(runtime);
            continue;
        }

        data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 0U] = sample.x;
        data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 1U] = sample.y;
        data[(sample_index * COMPONENT_WINDOW_AXIS_COUNT) + 2U] = sample.z;
        now_tick = (uint32_t)xTaskGetTickCount();
        sample_ticks[sample_index] = now_tick;
        if (sample_index == 0U)
        {
            metadata->timestamp_tick = now_tick;
        }
        metadata->sample_count = (uint16_t)(sample_index + 1U);
        runtime->producer.sample_total++;

        if (sample_index + 1U >= COMPONENT_WINDOW_SAMPLE_COUNT)
        {
            app_imu_commit_window(runtime, sample_index);
            sample_index = 0U;
        }
        else
        {
            sample_index++;
        }
    }
}
