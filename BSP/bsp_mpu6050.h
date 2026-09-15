#ifndef BSP_MPU6050_H
#define BSP_MPU6050_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_MPU6050_ADDRESS_7BIT       ((uint8_t)0x68U)
#define BSP_MPU6050_WHO_AM_I_EXPECTED  ((uint8_t)0x68U)
#define BSP_MPU6050_ACCEL_LSB_PER_G    ((float)8192.0f)

typedef enum
{
    BSP_MPU6050_OK = 0,
    BSP_MPU6050_ERR_ARGUMENT,
    BSP_MPU6050_ERR_NOT_READY,
    BSP_MPU6050_ERR_I2C,
    BSP_MPU6050_ERR_WHO_AM_I,
    BSP_MPU6050_ERR_RETRY_EXHAUSTED
} BSP_MPU6050_Status;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} BSP_MPU6050_AccelRaw;

BSP_MPU6050_Status BSP_MPU6050_Init(uint32_t timeout);
BSP_MPU6050_Status BSP_MPU6050_ReadWhoAmI(uint8_t *value, uint32_t timeout);
BSP_MPU6050_Status BSP_MPU6050_ReadAccelRaw(BSP_MPU6050_AccelRaw *sample,
                                             uint32_t timeout);
float BSP_MPU6050_RawToG(int16_t raw_value);

#ifdef __cplusplus
}
#endif

#endif /* BSP_MPU6050_H */
