#include "bsp_i2c.h"

#include "stm32f4xx_conf.h"

#define BSP_I2C1_SCL_PIN              GPIO_Pin_8
#define BSP_I2C1_SDA_PIN              GPIO_Pin_9
#define BSP_I2C1_CLOCK_SPEED_HZ       ((uint32_t)100000U)

static BSP_I2C_Status bsp_i2c_wait_event(uint32_t event, uint32_t timeout)      
{
    while (timeout != 0U)
    {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            return BSP_I2C_ERR_NACK;
        }

        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_BERR) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_ARLO) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_OVR) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_TIMEOUT) != RESET)
        {
            I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
            I2C_ClearFlag(I2C1, I2C_FLAG_ARLO);
            I2C_ClearFlag(I2C1, I2C_FLAG_OVR);
            I2C_ClearFlag(I2C1, I2C_FLAG_TIMEOUT);
            return BSP_I2C_ERR_BUS;
        }

        if (I2C_CheckEvent(I2C1, event) == SUCCESS)
        {
            return BSP_I2C_OK;
        }
        --timeout;
    }

    return BSP_I2C_ERR_TIMEOUT;
}

static BSP_I2C_Status bsp_i2c_wait_flag(uint32_t flag,
                                        FlagStatus expected,
                                        uint32_t timeout)
{
    while (timeout != 0U)
    {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            return BSP_I2C_ERR_NACK;
        }

        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_BERR) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_ARLO) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_OVR) != RESET ||
            I2C_GetFlagStatus(I2C1, I2C_FLAG_TIMEOUT) != RESET)
        {
            I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
            I2C_ClearFlag(I2C1, I2C_FLAG_ARLO);
            I2C_ClearFlag(I2C1, I2C_FLAG_OVR);
            I2C_ClearFlag(I2C1, I2C_FLAG_TIMEOUT);
            return BSP_I2C_ERR_BUS;
        }

        if (I2C_GetFlagStatus(I2C1, flag) == expected)
        {
            return BSP_I2C_OK;
        }
        --timeout;
    }

    return BSP_I2C_ERR_TIMEOUT;
}

static void bsp_i2c_restore_defaults(void)
{
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
}

static void bsp_i2c_stop(void)
{
    I2C_GenerateSTOP(I2C1, ENABLE);
    bsp_i2c_restore_defaults();
}

static BSP_I2C_Status bsp_i2c_abort(BSP_I2C_Status status)
{
    /* Release the current transaction before considering a bounded reset. */
    bsp_i2c_stop();
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) != RESET)
    {
        (void)BSP_I2C_Recover();
    }
    return status;
}

static void bsp_i2c_configure_gpio(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_I2C1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);

    gpio.GPIO_Pin = BSP_I2C1_SCL_PIN | BSP_I2C1_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_OD;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &gpio);
}

static void bsp_i2c_configure_peripheral(void)
{
    I2C_InitTypeDef i2c;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    I2C_DeInit(I2C1);
    I2C_StructInit(&i2c);
    i2c.I2C_ClockSpeed = BSP_I2C1_CLOCK_SPEED_HZ;
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1 = 0U;
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &i2c);
    bsp_i2c_restore_defaults();
    I2C_Cmd(I2C1, ENABLE);
}

static BSP_I2C_Status bsp_i2c_start(uint32_t timeout)
{
    BSP_I2C_Status status;

    status = bsp_i2c_wait_flag(I2C_FLAG_BUSY, RESET, timeout);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    return bsp_i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT, timeout);
}

static BSP_I2C_Status bsp_i2c_select_transmitter(uint8_t address7,
                                                 uint32_t timeout)
{
    I2C_Send7bitAddress(I2C1,
                        (uint8_t)(address7 << 1),
                        I2C_Direction_Transmitter);
    return bsp_i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED,
                              timeout);
}

BSP_I2C_Status BSP_I2C_Init(void)
{
    bsp_i2c_configure_gpio();
    bsp_i2c_configure_peripheral();
    return BSP_I2C_OK;
}

BSP_I2C_Status BSP_I2C_Reinit(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    I2C_Cmd(I2C1, DISABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    return BSP_I2C_Init();
}

BSP_I2C_Status BSP_I2C_Recover(void)
{
    /* Peripheral reset/reinitialization is intentionally finite. */
    bsp_i2c_stop();
    return BSP_I2C_Reinit();
}

BSP_I2C_Status BSP_I2C_Write(uint8_t address7,
                             uint8_t reg,
                             const uint8_t *data,
                             uint8_t length,
                             uint32_t timeout)
{
    BSP_I2C_Status status;
    uint8_t index;

    if (address7 > 0x7FU || data == (const uint8_t *)0 || length == 0U ||
        timeout == 0U)
    {
        return BSP_I2C_ERR_ARGUMENT;
    }

    status = bsp_i2c_start(timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    status = bsp_i2c_select_transmitter(address7, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    I2C_SendData(I2C1, reg);
    status = bsp_i2c_wait_flag(I2C_FLAG_TXE, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    for (index = 0U; index < length; ++index)
    {
        I2C_SendData(I2C1, data[index]);
        status = bsp_i2c_wait_flag(I2C_FLAG_TXE, SET, timeout);
        if (status != BSP_I2C_OK)
        {
            return bsp_i2c_abort(status);
        }
    }

    status = bsp_i2c_wait_flag(I2C_FLAG_BTF, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }
    bsp_i2c_stop();
    return BSP_I2C_OK;
}

BSP_I2C_Status BSP_I2C_Read(uint8_t address7,
                            uint8_t reg,
                            uint8_t *data,
                            uint8_t length,
                            uint32_t timeout)
{
    BSP_I2C_Status status;
    uint8_t index;
    uint8_t remaining;

    if (address7 > 0x7FU || data == (uint8_t *)0 || length == 0U ||
        timeout == 0U)
    {
        return BSP_I2C_ERR_ARGUMENT;
    }

    status = bsp_i2c_start(timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    status = bsp_i2c_select_transmitter(address7, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    I2C_SendData(I2C1, reg);
    status = bsp_i2c_wait_flag(I2C_FLAG_TXE, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    status = bsp_i2c_wait_flag(I2C_FLAG_BTF, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    status = bsp_i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    /* Keep ADDR pending until ACK/POS are programmed for this length. */
    I2C_Send7bitAddress(I2C1,
                        (uint8_t)(address7 << 1),
                        I2C_Direction_Receiver);
    status = bsp_i2c_wait_flag(I2C_FLAG_ADDR, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    remaining = length;
    if (remaining == 1U)
    {
        /* NACK the only byte, then clear ADDR and issue STOP. */
        I2C_AcknowledgeConfig(I2C1, DISABLE);
        I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
        (void)I2C1->SR2;
        I2C_GenerateSTOP(I2C1, ENABLE);
        status = bsp_i2c_wait_flag(I2C_FLAG_RXNE, SET, timeout);
        if (status != BSP_I2C_OK)
        {
            return bsp_i2c_abort(status);
        }
        data[0] = I2C_ReceiveData(I2C1);
        bsp_i2c_restore_defaults();
        return BSP_I2C_OK;
    }

    if (remaining == 2U)
    {
        /* POS=1 makes the second byte the NACKed byte. */
        I2C_AcknowledgeConfig(I2C1, DISABLE);
        I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Next);
        (void)I2C1->SR2;

        status = bsp_i2c_wait_flag(I2C_FLAG_BTF, SET, timeout);
        if (status != BSP_I2C_OK)
        {
            return bsp_i2c_abort(status);
        }

        I2C_GenerateSTOP(I2C1, ENABLE);
        data[0] = I2C_ReceiveData(I2C1);
        data[1] = I2C_ReceiveData(I2C1);
        bsp_i2c_restore_defaults();
        return BSP_I2C_OK;
    }

    /* ACK while receiving the body; POS remains at its default value. */
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
    (void)I2C1->SR2;

    index = 0U;
    remaining = length;
    while (remaining > 3U)
    {
        status = bsp_i2c_wait_flag(I2C_FLAG_RXNE, SET, timeout);
        if (status != BSP_I2C_OK)
        {
            return bsp_i2c_abort(status);
        }
        data[index] = I2C_ReceiveData(I2C1);
        ++index;
        --remaining;
    }

    /* EV7_3: BTF has the first two of the final three bytes ready. */
    status = bsp_i2c_wait_flag(I2C_FLAG_BTF, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    I2C_AcknowledgeConfig(I2C1, DISABLE);
    data[index] = I2C_ReceiveData(I2C1);
    ++index;

    /* Wait for the final two bytes before releasing the bus. */
    status = bsp_i2c_wait_flag(I2C_FLAG_BTF, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }

    I2C_GenerateSTOP(I2C1, ENABLE);
    data[index] = I2C_ReceiveData(I2C1);
    ++index;
    status = bsp_i2c_wait_flag(I2C_FLAG_RXNE, SET, timeout);
    if (status != BSP_I2C_OK)
    {
        return bsp_i2c_abort(status);
    }
    data[index] = I2C_ReceiveData(I2C1);
    bsp_i2c_restore_defaults();
    return BSP_I2C_OK;
}
