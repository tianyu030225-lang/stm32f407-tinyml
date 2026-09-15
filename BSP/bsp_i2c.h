#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C1 is connected to PB8 (SCL) and PB9 (SDA) on this board. */
#define BSP_I2C1_TIMEOUT_DEFAULT ((uint32_t)100000U)

typedef enum
{
    BSP_I2C_OK = 0,
    BSP_I2C_ERR_ARGUMENT,
    BSP_I2C_ERR_TIMEOUT,
    BSP_I2C_ERR_NACK,
    BSP_I2C_ERR_BUS
} BSP_I2C_Status;

BSP_I2C_Status BSP_I2C_Init(void);
BSP_I2C_Status BSP_I2C_Reinit(void);
BSP_I2C_Status BSP_I2C_Recover(void);

BSP_I2C_Status BSP_I2C_Write(uint8_t address7,
                             uint8_t reg,
                             const uint8_t *data,
                             uint8_t length,
                             uint32_t timeout);
BSP_I2C_Status BSP_I2C_Read(uint8_t address7,
                            uint8_t reg,
                            uint8_t *data,
                            uint8_t length,
                            uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */
