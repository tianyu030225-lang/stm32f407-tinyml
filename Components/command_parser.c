#include "command_parser.h"

static void component_command_skip_spaces(
    const char *line,
    uint16_t length,
    uint16_t *index)
{
    while ((*index < length) &&
           ((line[*index] == ' ') || (line[*index] == '\t')))
    {
        (*index)++;
    }
}

static uint8_t component_command_next_token(
    const char *line,
    uint16_t length,
    uint16_t *index,
    uint16_t *start,
    uint16_t *end)
{
    component_command_skip_spaces(line, length, index);
    if (*index >= length)
    {
        return 0U;
    }

    *start = *index;
    while ((*index < length) &&
           (line[*index] != ' ') && (line[*index] != '\t'))
    {
        (*index)++;
    }
    *end = *index;
    return 1U;
}

static uint8_t component_command_token_equals(
    const char *line,
    uint16_t start,
    uint16_t end,
    const char *literal)
{
    uint16_t literal_length;
    uint16_t index;

    literal_length = 0U;
    while (literal[literal_length] != '\0')
    {
        literal_length++;
    }

    if ((uint16_t)(end - start) != literal_length)
    {
        return 0U;
    }

    for (index = 0U; index < literal_length; index++)
    {
        if (line[(uint16_t)(start + index)] != literal[index])
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t component_command_is_end(
    const char *line,
    uint16_t length,
    uint16_t *index)
{
    component_command_skip_spaces(line, length, index);
    return (uint8_t)(*index == length);
}

/*
 * Parse 0..1 with at most three fractional digits.  The result is in
 * thousandths, so no floating-point code is needed on the command path.
 */
static uint8_t component_command_parse_threshold(
    const char *line,
    uint16_t start,
    uint16_t end,
    uint16_t *threshold_milli)
{
    uint16_t index;
    uint16_t fraction;
    uint16_t fraction_digits;
    uint8_t integer_part;

    if ((uint16_t)(end - start) == 0U)
    {
        return 0U;
    }

    if ((line[start] != '0') && (line[start] != '1'))
    {
        return 0U;
    }
    integer_part = (uint8_t)(line[start] - '0');
    index = (uint16_t)(start + 1U);
    fraction = 0U;
    fraction_digits = 0U;

    if (index < end)
    {
        if (line[index] != '.')
        {
            return 0U;
        }
        index++;
        if (index >= end)
        {
            return 0U;
        }

        while (index < end)
        {
            if ((line[index] < '0') || (line[index] > '9'))
            {
                return 0U;
            }
            if (fraction_digits >= 3U)
            {
                return 2U;
            }
            fraction = (uint16_t)(fraction * 10U +
                                   (uint16_t)(line[index] - '0'));
            fraction_digits++;
            index++;
        }
    }

    while (fraction_digits < 3U)
    {
        fraction = (uint16_t)(fraction * 10U);
        fraction_digits++;
    }

    if ((integer_part == 1U) && (fraction != 0U))
    {
        return 2U;
    }

    *threshold_milli = (uint16_t)(integer_part * 1000U + fraction);
    return 1U;
}

static component_command_parse_status_t component_command_parse_ai(
    const char *line,
    uint16_t length,
    uint16_t *index,
    app_control_message_t *message)
{
    uint16_t start;
    uint16_t end;
    uint8_t threshold_result;

    if (!component_command_next_token(line, length, index, &start, &end))
    {
        return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
    }

    if (component_command_token_equals(line, start, end, "INFO"))
    {
        if (!component_command_is_end(line, length, index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_AI_INFO;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    if (component_command_token_equals(line, start, end, "START"))
    {
        if (!component_command_is_end(line, length, index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_AI_START;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    if (component_command_token_equals(line, start, end, "STOP"))
    {
        if (!component_command_is_end(line, length, index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_AI_STOP;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    if (component_command_token_equals(line, start, end, "THRESHOLD"))
    {
        if (!component_command_next_token(line, length, index, &start, &end))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        threshold_result = component_command_parse_threshold(
            line, start, end, &message->threshold_milli);
        if (threshold_result == 0U)
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        if (threshold_result == 2U)
        {
            return COMPONENT_COMMAND_PARSE_RANGE;
        }
        if (!component_command_is_end(line, length, index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_AI_THRESHOLD;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
}

static component_command_parse_status_t component_command_parse_dataset(
    const char *line,
    uint16_t length,
    uint16_t *index,
    app_control_message_t *message)
{
    uint16_t start;
    uint16_t end;

    if (!component_command_next_token(line, length, index, &start, &end))
    {
        return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
    }

    if (component_command_token_equals(line, start, end, "STOP"))
    {
        if (!component_command_is_end(line, length, index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_DATASET_STOP;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    if (!component_command_token_equals(line, start, end, "START"))
    {
        return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
    }

    if (!component_command_next_token(line, length, index, &start, &end))
    {
        return COMPONENT_COMMAND_PARSE_ARGUMENT;
    }

    if (component_command_token_equals(line, start, end, "STABLE"))
    {
        message->label = APP_LABEL_STABLE;
    }
    else if (component_command_token_equals(line, start, end, "VIBRATION"))
    {
        message->label = APP_LABEL_VIBRATION;
    }
    else if (component_command_token_equals(line, start, end, "IMPACT"))
    {
        message->label = APP_LABEL_IMPACT;
    }
    else
    {
        return COMPONENT_COMMAND_PARSE_ARGUMENT;
    }

    if (!component_command_is_end(line, length, index))
    {
        return COMPONENT_COMMAND_PARSE_ARGUMENT;
    }
    message->command = APP_CONTROL_DATASET_START;
    return COMPONENT_COMMAND_PARSE_OK;
}

static component_command_parse_status_t component_command_parse_two_word(
    const char *line,
    uint16_t length,
    uint16_t *index,
    const char *word,
    app_control_command_t command,
    app_control_message_t *message)
{
    uint16_t start;
    uint16_t end;

    if (!component_command_next_token(line, length, index, &start, &end))
    {
        return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
    }
    if (!component_command_token_equals(line, start, end, word))
    {
        return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
    }
    if (!component_command_is_end(line, length, index))
    {
        return COMPONENT_COMMAND_PARSE_ARGUMENT;
    }
    message->command = command;
    return COMPONENT_COMMAND_PARSE_OK;
}

component_command_parse_status_t component_command_parse(
    const char *line,
    uint16_t length,
    app_control_message_t *message)
{
    uint16_t index;
    uint16_t start;
    uint16_t end;

    if ((line == (const char *)0) ||
        (message == (app_control_message_t *)0))
    {
        return COMPONENT_COMMAND_PARSE_INVALID_ARGUMENT;
    }
    if (length > COMPONENT_COMMAND_MAX_LINE_LENGTH)
    {
        return COMPONENT_COMMAND_PARSE_TOO_LONG;
    }
    if (length == 0U)
    {
        return COMPONENT_COMMAND_PARSE_EMPTY;
    }

    app_control_message_clear(message);
    index = 0U;
    if (!component_command_next_token(line, length, &index, &start, &end))
    {
        return COMPONENT_COMMAND_PARSE_EMPTY;
    }

    if (component_command_token_equals(line, start, end, "STATUS"))
    {
        if (!component_command_is_end(line, length, &index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_STATUS;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    if (component_command_token_equals(line, start, end, "AI"))
    {
        return component_command_parse_ai(line, length, &index, message);
    }

    if (component_command_token_equals(line, start, end, "DATASET"))
    {
        return component_command_parse_dataset(line, length, &index, message);
    }

    if (component_command_token_equals(line, start, end, "STATS"))
    {
        return component_command_parse_two_word(
            line, length, &index, "RESET", APP_CONTROL_STATS_RESET, message);
    }

    if (component_command_token_equals(line, start, end, "HELP"))
    {
        if (!component_command_is_end(line, length, &index))
        {
            return COMPONENT_COMMAND_PARSE_ARGUMENT;
        }
        message->command = APP_CONTROL_HELP;
        return COMPONENT_COMMAND_PARSE_OK;
    }

    return COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND;
}
