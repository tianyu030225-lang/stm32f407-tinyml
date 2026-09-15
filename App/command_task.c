#include "command_task.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "command_parser.h"
#include "app_tx.h"

#define APP_COMMAND_IDLE_TIMEOUT_TICKS (pdMS_TO_TICKS(100U))

static app_runtime_t *s_command_runtime;

void App_CommandBindRuntime(app_runtime_t *runtime)
{
    s_command_runtime = runtime;
}

void App_CommandRxByteFromISR(uint8_t byte_value)
{
    BaseType_t higher_priority_task_woken;

    higher_priority_task_woken = pdFALSE;
    if (s_command_runtime == (app_runtime_t *)0)
    {
        return;
    }

    (void)component_ring_buffer_push_isr(&s_command_runtime->rx_ring,
                                         byte_value);
    if (s_command_runtime->command_task != (TaskHandle_t)0)
    {
        vTaskNotifyGiveFromISR(s_command_runtime->command_task,
                               &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void app_command_send_parse_error(
    app_runtime_t *runtime,
    component_command_parse_status_t status)
{
    switch (status)
    {
        case COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND:
            (void)App_TxSendf(runtime, "ERR UNKNOWN_CMD\r\n");
            break;

        case COMPONENT_COMMAND_PARSE_RANGE:
            (void)App_TxSendf(runtime, "ERR RANGE\r\n");
            break;

        case COMPONENT_COMMAND_PARSE_TOO_LONG:
            (void)App_TxSendf(runtime, "ERR RX_OVERFLOW\r\n");
            break;

        case COMPONENT_COMMAND_PARSE_EMPTY:
        case COMPONENT_COMMAND_PARSE_ARGUMENT:
        case COMPONENT_COMMAND_PARSE_INVALID_ARGUMENT:
        case COMPONENT_COMMAND_PARSE_OK:
        default:
            (void)App_TxSendf(runtime, "ERR ARG\r\n");
            break;
    }
}

static void app_command_finish_line(
    app_runtime_t *runtime,
    char *line,
    uint16_t *line_length,
    uint8_t *discarding,
    uint8_t *line_overflow)
{
    app_control_message_t message;
    component_command_parse_status_t parse_status;

    if (*line_overflow != 0U)
    {
        runtime->producer.rx_overflow_total =
            component_ring_buffer_discard_count(&runtime->rx_ring);
        (void)App_TxSendf(runtime, "ERR RX_OVERFLOW\r\n");
    }
    else
    {
        parse_status = component_command_parse(line,
                                                *line_length,
                                                &message);
        if (parse_status == COMPONENT_COMMAND_PARSE_OK)
        {
            if (xQueueSend(runtime->control_queue,
                           &message,
                           (TickType_t)0) != pdPASS)
            {
                (void)App_TxSendf(runtime, "ERR BUSY\r\n");
            }
            else
            {
                app_runtime_signal_ai(runtime);
            }
        }
        else
        {
            if (parse_status != COMPONENT_COMMAND_PARSE_EMPTY)
            {
                runtime->producer.invalid_command_total++;
            }
            app_command_send_parse_error(runtime, parse_status);
        }
    }

    *line_length = 0U;
    *discarding = 0U;
    *line_overflow = 0U;
    line[0] = '\0';
}

static void app_command_consume_byte(
    app_runtime_t *runtime,
    uint8_t byte_value,
    char *line,
    uint16_t *line_length,
    uint8_t *discarding,
    uint8_t *line_overflow,
    uint8_t *skip_lf)
{
    if (byte_value == '\r')
    {
        app_command_finish_line(runtime,
                                line,
                                line_length,
                                discarding,
                                line_overflow);
        *skip_lf = 1U;
        return;
    }
    if (byte_value == '\n')
    {
        if (*skip_lf != 0U)
        {
            *skip_lf = 0U;
            return;
        }
        app_command_finish_line(runtime,
                                line,
                                line_length,
                                discarding,
                                line_overflow);
        return;
    }

    *skip_lf = 0U;
    if (*discarding != 0U)
    {
        return;
    }
    if (*line_length >= COMPONENT_COMMAND_MAX_LINE_LENGTH)
    {
        *discarding = 1U;
        *line_overflow = 1U;
        return;
    }
    line[*line_length] = (char)byte_value;
    (*line_length)++;
    line[*line_length] = '\0';
}

void App_CommandTask(void *argument)
{
    app_runtime_t *runtime;
    char line[COMPONENT_COMMAND_MAX_LINE_LENGTH + 1U];
    uint16_t line_length;
    uint8_t discarding;
    uint8_t line_overflow;
    uint8_t skip_lf;
    uint8_t byte_value;
    uint8_t first_pass;

    runtime = (app_runtime_t *)argument;
    line_length = 0U;
    discarding = 0U;
    line_overflow = 0U;
    skip_lf = 0U;
    first_pass = 1U;
    line[0] = '\0';

    for (;;)
    {
        if (first_pass == 0U)
        {
            if ((ulTaskNotifyTake(pdTRUE,
                                  APP_COMMAND_IDLE_TIMEOUT_TICKS) == 0U) &&
                ((line_length != 0U) ||
                 (discarding != 0U) ||
                 (line_overflow != 0U)))
            {
                /* Accept terminals that transmit a command without CR/LF
                 * after a short inter-command idle period. */
                app_command_finish_line(runtime,
                                        line,
                                        &line_length,
                                        &discarding,
                                        &line_overflow);
                skip_lf = 0U;
            }
        }
        first_pass = 0U;
        for (;;)
        {
            if (component_ring_buffer_is_overflowed(&runtime->rx_ring) != 0U)
            {
                discarding = 1U;
                line_overflow = 1U;
                component_ring_buffer_clear_overflow(&runtime->rx_ring);
                runtime->producer.rx_overflow_total =
                    component_ring_buffer_discard_count(&runtime->rx_ring);
            }

            if (component_ring_buffer_pop(&runtime->rx_ring,
                                          &byte_value) !=
                COMPONENT_RING_BUFFER_OK)
            {
                break;
            }
            app_command_consume_byte(runtime,
                                     byte_value,
                                     line,
                                     &line_length,
                                     &discarding,
                                     &line_overflow,
                                     &skip_lf);
        }
    }
}
