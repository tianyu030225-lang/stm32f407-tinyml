#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

#include "app_mode.h"

typedef enum
{
    APP_LABEL_NONE = 0,
    APP_LABEL_STABLE,
    APP_LABEL_VIBRATION,
    APP_LABEL_IMPACT
} app_label_t;

typedef enum
{
    APP_CLASS_UNKNOWN = 0,
    APP_CLASS_STABLE,
    APP_CLASS_VIBRATION,
    APP_CLASS_IMPACT
} app_class_t;

typedef uint32_t app_alarm_flags_t;

#define APP_ALARM_FLAG_NONE          ((app_alarm_flags_t)0U)
#define APP_ALARM_FLAG_VIBRATION     ((app_alarm_flags_t)1U << 0)
#define APP_ALARM_FLAG_IMPACT        ((app_alarm_flags_t)1U << 1)
#define APP_ALARM_FLAG_SENSOR_FAULT  ((app_alarm_flags_t)1U << 2)
#define APP_ALARM_FLAG_MODEL_FAULT   ((app_alarm_flags_t)1U << 3)
#define APP_ALARM_FLAG_UNKNOWN       ((app_alarm_flags_t)1U << 4)
#define APP_ALARM_FLAG_WINDOW_DROP   ((app_alarm_flags_t)1U << 5)

typedef enum
{
    APP_CONTROL_NONE = 0,
    APP_CONTROL_STATUS,
    APP_CONTROL_AI_INFO,
    APP_CONTROL_AI_START,
    APP_CONTROL_AI_STOP,
    APP_CONTROL_AI_THRESHOLD,
    APP_CONTROL_DATASET_START,
    APP_CONTROL_DATASET_STOP,
    APP_CONTROL_STATS_RESET,
    APP_CONTROL_HELP
} app_control_command_t;

typedef struct
{
    app_control_command_t command;
    app_label_t label;
    uint16_t threshold_milli;
} app_control_message_t;

typedef struct
{
    app_class_t class_id;
    uint16_t confidence_milli;
    uint32_t latency_us;
    uint32_t window_sequence;
    app_alarm_flags_t alarm_flags;
} app_inference_result_t;

typedef struct
{
    uint32_t sample_count;
    uint32_t window_count;
    uint32_t window_drop_count;
    uint32_t i2c_error_count;
    uint32_t sensor_reset_count;
    uint32_t model_error_count;
    uint32_t unknown_count;
    uint32_t rx_overflow_count;
    uint32_t invalid_command_count;
    uint32_t inference_count;
    uint32_t inference_total_us;
    uint32_t inference_average_us;
    uint32_t inference_p95_us;
    uint32_t inference_max_us;
    uint16_t imu_task_min_stack_words;
    uint16_t command_task_min_stack_words;
    uint16_t ai_task_min_stack_words;
} app_system_stats_t;

typedef struct
{
    app_mode_t mode;
    uint8_t sensor_ready;
    uint8_t model_ready;
    uint8_t reserved0;
    app_label_t capture_label;
    app_class_t last_class;
    uint16_t threshold_milli;
    uint32_t session_id;
    uint32_t last_window_sequence;
    uint16_t last_confidence_milli;
    uint16_t reserved1;
    uint32_t last_latency_us;
    app_alarm_flags_t alarm_flags;
    app_system_stats_t stats;
} app_system_snapshot_t;

void app_control_message_clear(app_control_message_t *message);
const char *app_label_name(app_label_t label);
const char *app_class_name(app_class_t class_id);
const char *app_control_command_name(app_control_command_t command);

#endif /* APP_TYPES_H */
