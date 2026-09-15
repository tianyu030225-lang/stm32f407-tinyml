#include "app_types.h"

void app_control_message_clear(app_control_message_t *message)
{
    if (message == (app_control_message_t *)0)
    {
        return;
    }
    message->command = APP_CONTROL_NONE;
    message->label = APP_LABEL_NONE;
    message->threshold_milli = 0U;
}

const char *app_label_name(app_label_t label)
{
    switch (label)
    {
        case APP_LABEL_STABLE:
            return "STABLE";
        case APP_LABEL_VIBRATION:
            return "VIBRATION";
        case APP_LABEL_IMPACT:
            return "IMPACT";
        case APP_LABEL_NONE:
        default:
            return "NONE";
    }
}

const char *app_class_name(app_class_t class_id)
{
    switch (class_id)
    {
        case APP_CLASS_STABLE:
            return "STABLE";
        case APP_CLASS_VIBRATION:
            return "VIBRATION";
        case APP_CLASS_IMPACT:
            return "IMPACT";
        case APP_CLASS_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

const char *app_control_command_name(app_control_command_t command)
{
    switch (command)
    {
        case APP_CONTROL_STATUS:
            return "STATUS";
        case APP_CONTROL_AI_INFO:
            return "AI INFO";
        case APP_CONTROL_AI_START:
            return "AI START";
        case APP_CONTROL_AI_STOP:
            return "AI STOP";
        case APP_CONTROL_AI_THRESHOLD:
            return "AI THRESHOLD";
        case APP_CONTROL_DATASET_START:
            return "DATASET START";
        case APP_CONTROL_DATASET_STOP:
            return "DATASET STOP";
        case APP_CONTROL_STATS_RESET:
            return "STATS RESET";
        case APP_CONTROL_HELP:
            return "HELP";
        case APP_CONTROL_NONE:
        default:
            return "NONE";
    }
}
