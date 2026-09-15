#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "ai_adapter.h"
#include "app_state.h"
#include "ring_buffer.h"
#include "window_pool.h"

#define APP_CONTROL_QUEUE_LENGTH       (8U)
#define APP_WINDOW_QUEUE_LENGTH        (COMPONENT_WINDOW_POOL_COUNT)
#define APP_INFERENCE_HISTORY_LENGTH   (512U)

typedef struct
{
    component_window_id_t buffer_id;
    component_window_metadata_t metadata;
} app_window_queue_item_t;

/* Producers write raw monotonic counters; only AiTask publishes app_state.stats. */
typedef struct
{
    volatile uint32_t sample_total;
    volatile uint32_t window_total;
    volatile uint32_t window_drop_total;
    volatile uint32_t i2c_error_total;
    /* Counts bounded BSP_I2C_Recover attempts after failed accel reads. */
    volatile uint32_t sensor_reset_total;
    volatile uint32_t rx_overflow_total;
    volatile uint32_t invalid_command_total;
    volatile uint8_t sensor_fault_pending;
    volatile uint8_t resource_fault_pending;
    volatile uint8_t tx_fault_pending;
} app_runtime_producer_counters_t;

typedef struct
{
    component_ring_buffer_t rx_ring;
    component_window_pool_t window_pool;
    app_state_t state;

    QueueHandle_t free_buffer_queue;
    QueueHandle_t ready_buffer_queue;
    QueueHandle_t control_queue;
    SemaphoreHandle_t tx_mutex;

    TaskHandle_t imu_task;
    TaskHandle_t command_task;
    TaskHandle_t ai_task;

    uint8_t sensor_ready;
    volatile uint8_t imu_enabled;
    volatile uint32_t sampling_generation;
    volatile component_window_id_t imu_active_id;
    volatile uint32_t imu_active_generation;
    volatile uint32_t next_window_sequence;
    volatile app_label_t capture_label;
    volatile uint32_t session_id;

    app_runtime_producer_counters_t producer;
    ai_adapter_t ai_adapter;

    /* All fields below are written only by AiTask after initialization. */
    app_system_snapshot_t ai_snapshot;
    /* All published latency aggregates use this bounded, most-recent window. */
    uint32_t inference_latency_history[APP_INFERENCE_HISTORY_LENGTH];
    uint16_t inference_history_count;
    uint16_t inference_history_next;
    uint32_t inference_total_us;
    uint32_t inference_count;
    uint16_t vibration_streak;
    uint16_t stable_streak;
    uint32_t baseline_sample_total;
    uint32_t baseline_window_total;
    uint32_t baseline_window_drop_total;
    uint32_t baseline_i2c_error_total;
    uint32_t baseline_sensor_reset_total;
    uint32_t baseline_rx_overflow_total;
    uint32_t baseline_invalid_command_total;
    uint8_t buzzer_fault_announced;
    uint8_t pending_sampling_enable;
} app_runtime_t;

uint8_t app_runtime_init(app_runtime_t *runtime, uint8_t sensor_ready);
void app_runtime_signal_ai(app_runtime_t *runtime);
BaseType_t app_runtime_send_free(
    app_runtime_t *runtime,
    component_window_id_t buffer_id,
    const component_window_metadata_t *metadata);
void app_runtime_mark_resource_fault(app_runtime_t *runtime);

#endif /* APP_RUNTIME_H */
