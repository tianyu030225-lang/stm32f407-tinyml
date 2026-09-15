#ifndef APP_TX_H
#define APP_TX_H

#include <stdint.h>

#include "app_runtime.h"

uint8_t App_TxSend(app_runtime_t *runtime,
                   const uint8_t *data,
                   uint16_t length);
uint8_t App_TxSendf(app_runtime_t *runtime, const char *format, ...);

#endif /* APP_TX_H */
