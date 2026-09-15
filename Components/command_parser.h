#ifndef COMPONENT_COMMAND_PARSER_H
#define COMPONENT_COMMAND_PARSER_H

#include <stdint.h>

#include "app_types.h"

/* The upper layer supplies a complete line without CR or LF. */
#define COMPONENT_COMMAND_MAX_LINE_LENGTH (64U)

typedef enum
{
    COMPONENT_COMMAND_PARSE_OK = 0,
    COMPONENT_COMMAND_PARSE_EMPTY,
    COMPONENT_COMMAND_PARSE_TOO_LONG,
    COMPONENT_COMMAND_PARSE_UNKNOWN_COMMAND,
    COMPONENT_COMMAND_PARSE_ARGUMENT,
    COMPONENT_COMMAND_PARSE_RANGE,
    COMPONENT_COMMAND_PARSE_INVALID_ARGUMENT
} component_command_parse_status_t;

/*
 * Parse one framed command.  No token or result storage is allocated; the
 * caller owns the input and output objects for the duration of the call.
 */
component_command_parse_status_t component_command_parse(
    const char *line,
    uint16_t length,
    app_control_message_t *message);

#endif /* COMPONENT_COMMAND_PARSER_H */
