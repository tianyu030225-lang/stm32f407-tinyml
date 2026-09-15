#ifndef APP_COMMAND_TASK_H
#define APP_COMMAND_TASK_H

#include <stdint.h>

#include "app_runtime.h"

void App_CommandTask(void *argument);
void App_CommandBindRuntime(app_runtime_t *runtime);
void App_CommandRxByteFromISR(uint8_t byte_value);

#endif /* APP_COMMAND_TASK_H */
