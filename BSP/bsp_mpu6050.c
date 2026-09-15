#include "bsp_mpu6050.h"

#include "bsp_i2c.h"

#define MPU6050_REG_SMPLRT_DIV        ((uint8_t)0x19U)
#define MPU6050_REG_CONFIG            ((uint8_t)0x1AU)
#define MPU6050_REG_ACCEL_CONFIG      ((uint8_t)0x1CU)
#define MPU6050_REG_ACCEL_XOUT_H      ((uint8_t)0x3BU)
#define MPU6050_REG_PWR_MGMT_1        ((uint8_t)0x6BU)
#define MPU6050_REG_WHO_AM_I          ((uint8_t)0x75U)

#define MPU6050_CONFIG_DLPF_44HZ      ((uint8_t)0x03U)
#define MPU6050_ACCEL_RANGE_4G        ((uint8_t)0x08U)
#define MPU6050_CLOCK_PLL_XGYRO       ((uint8_t)0x01U)
#define MPU6050_RETRY_COUNT           ((uint8_t)3U)

static uint8_t s_mpu6050_ready;

static BSP_MPU6050_Status bsp_mpu6050_from_i2c(BSP_I2C_Status status)
{
    if (status == BSP_I2C_ERR_ARGUMENT)
    {
        return BSP_MPU6050_ERR_ARGUMENT;
    }
    if (status == BSP_I2C_ERR_TIMEOUT)
    {
        return BSP_MPU6050_ERR_RETRY_EXHAUSTED;
    }
    return BSP_MPU6050_ERR_I2C;
}

static BSP_MPU6050_Status bsp_mpu6050_write(uint8_t reg,
                                             const uint8_t *data,
                                             uint8_t length,
                                             uint32_t timeout)
{
    BSP_I2C_Status i2c_status;
    uint8_t attempt;

    for (attempt = 0U; attempt < MPU6050_RETRY_COUNT; ++attempt)
    {
        i2c_status = BSP_I2C_Write(BSP_MPU6050_ADDRESS_7BIT,
                                   reg,
                                   data,
                                   length,
                                   timeout);
        if (i2c_status == BSP_I2C_OK)
        {
            return BSP_MPU6050_OK;
        }

        if (attempt + 1U < MPU6050_RETRY_COUNT)
        {
            (void)BSP_I2C_Reinit();
        }
    }

    return bsp_mpu6050_from_i2c(i2c_status);
}

static BSP_MPU6050_Status bsp_mpu6050_read(uint8_t reg,
                                            uint8_t *data,
                                            uint8_t length,
                                            uint32_t timeout)
{
    BSP_I2C_Status i2c_status;
    uint8_t attempt;

    for (attempt = 0U; attempt < MPU6050_RETRY_COUNT; ++attempt)
    {
        i2c_status = BSP_I2C_Read(BSP_MPU6050_ADDRESS_7BIT,
                                  reg,
                                  data,
                                  length,
                                  timeout);
        if (i2c_status == BSP_I2C_OK)
        {
            return BSP_MPU6050_OK;
        }

        if (attempt + 1U < MPU6050_RETRY_COUNT)
        {
            (void)BSP_I2C_Reinit();
        }
    }

    return bsp_mpu6050_from_i2c(i2c_status);
}

static BSP_MPU6050_Status bsp_mpu6050_write_byte(uint8_t reg,
                                                  uint8_t value,
                                                  uint32_t timeout)
{
    return bsp_mpu6050_write(reg, &value, 1U, timeout);
}

BSP_MPU6050_Status BSP_MPU6050_ReadWhoAmI(uint8_t *value, uint32_t timeout)
{
    if (value == (uint8_t *)0 || timeout == 0U)
    {
        return BSP_MPU6050_ERR_ARGUMENT;
    }

    return bsp_mpu6050_read(MPU6050_REG_WHO_AM_I, value, 1U, timeout);
}

BSP_MPU6050_Status BSP_MPU6050_Init(uint32_t timeout)
{
    BSP_I2C_Status i2c_status;
    BSP_MPU6050_Status status;
    uint8_t who_am_i;

    if (timeout == 0U)
    {
        return BSP_MPU6050_ERR_ARGUMENT;
    }

    s_mpu6050_ready = 0U;
    i2c_status = BSP_I2C_Init();
    if (i2c_status != BSP_I2C_OK)
    {
        return bsp_mpu6050_from_i2c(i2c_status);
    }

    status = BSP_MPU6050_ReadWhoAmI(&who_am_i, timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }
    if (who_am_i != BSP_MPU6050_WHO_AM_I_EXPECTED)
    {
        return BSP_MPU6050_ERR_WHO_AM_I;
    }

    status = bsp_mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1,
                                    MPU6050_CLOCK_PLL_XGYRO,
                                    timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }

    /* DLPF=44 Hz makes the internal sample rate 1 kHz; divider 9 gives 100 Hz. */
    status = bsp_mpu6050_write_byte(MPU6050_REG_CONFIG,
                                    MPU6050_CONFIG_DLPF_44HZ,
                                    timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }

    status = bsp_mpu6050_write_byte(MPU6050_REG_SMPLRT_DIV, 9U, timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }

    status = bsp_mpu6050_write_byte(MPU6050_REG_ACCEL_CONFIG,
                                    MPU6050_ACCEL_RANGE_4G,
                                    timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }

    s_mpu6050_ready = 1U;
    return BSP_MPU6050_OK;
}

BSP_MPU6050_Status BSP_MPU6050_ReadAccelRaw(BSP_MPU6050_AccelRaw *sample,
                                             uint32_t timeout)
{
    BSP_MPU6050_Status status;
    uint8_t data[6];

    if (sample == (BSP_MPU6050_AccelRaw *)0 || timeout == 0U)
    {
        return BSP_MPU6050_ERR_ARGUMENT;
    }
    if (s_mpu6050_ready == 0U)
    {
        return BSP_MPU6050_ERR_NOT_READY;
    }

    status = bsp_mpu6050_read(MPU6050_REG_ACCEL_XOUT_H,
                               data,
                               (uint8_t)sizeof(data),
                               timeout);
    if (status != BSP_MPU6050_OK)
    {
        return status;
    }

    sample->x = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    sample->y = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
    sample->z = (int16_t)(((uint16_t)data[4] << 8) | data[5]);
    return BSP_MPU6050_OK;
}

float BSP_MPU6050_RawToG(int16_t raw_value)
{
    return ((float)raw_value / BSP_MPU6050_ACCEL_LSB_PER_G);
}
