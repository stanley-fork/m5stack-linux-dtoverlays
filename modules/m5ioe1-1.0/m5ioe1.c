// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the M5IOE1 GPIO Expander with ADC and Pinctrl support.
 *
 * Copyright (C) 2020 Tesla Motors, Inc.
 */

#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

/* ============================================================================
 * 寄存器定义
 * ============================================================================ */

#define M5IOE1_UID_L                    0x00
#define M5IOE1_UID_H                    0x01
#define M5IOE1_REV                      0x02
#define M5IOE1_GPIO_M_L                 0x03
#define M5IOE1_GPIO_M_H                 0x04
#define M5IOE1_GPIO_O_L                 0x05
#define M5IOE1_GPIO_O_H                 0x06
#define M5IOE1_GPIO_I_L                 0x07
#define M5IOE1_GPIO_I_H                 0x08
#define M5IOE1_GPIO_PU_L                0x09
#define M5IOE1_GPIO_PU_H                0x0A
#define M5IOE1_GPIO_PD_L                0x0B
#define M5IOE1_GPIO_PD_H                0x0C
#define M5IOE1_GPIO_IE_L                0x0D
#define M5IOE1_GPIO_IE_H                0x0E
#define M5IOE1_GPIO_IP_L                0x0F
#define M5IOE1_GPIO_IP_H                0x10
#define M5IOE1_GPIO_IS_L                0x11
#define M5IOE1_GPIO_IS_H                0x12
#define M5IOE1_GPIO_DRV_L               0x13
#define M5IOE1_GPIO_DRV_H               0x14
#define M5IOE1_ADC_CTRL                 0x15
#define M5IOE1_ADC_D_L                  0x16
#define M5IOE1_ADC_D_H                  0x17
#define M5IOE1_TEMP_CTRL                0x18
#define M5IOE1_TEMP_D_L                 0x19
#define M5IOE1_TEMP_D_H                 0x1A
#define M5IOE1_PWM1_L                   0x1B
#define M5IOE1_PWM1_H                   0x1C
#define M5IOE1_PWM2_L                   0x1D
#define M5IOE1_PWM2_H                   0x1E
#define M5IOE1_PWM3_L                   0x1F
#define M5IOE1_PWM3_H                   0x20
#define M5IOE1_PWM4_L                   0x21
#define M5IOE1_PWM4_H                   0x22
#define M5IOE1_IIC_CFG                  0x23
#define M5IOE1_LED_CFG                  0x24
#define M5IOE1_PWM_FREQ_L               0x25
#define M5IOE1_PWM_FREQ_H               0x26
#define M5IOE1_REF_VOLT_L               0x27
#define M5IOE1_REF_VOLT_H               0x28
#define M5IOE1_RESET                    0x29
#define M5IOE1_LED_RAM_S                0x30
#define M5IOE1_LED_RAM_E                0x6F
#define M5IOE1_RTC_RAM_S                0x70
#define M5IOE1_RTC_RAM_E                0x8F
#define M5IOE1_PULSE                    0x90
#define M5IOE1_REG_MAX                  0x90

/* ADC控制位定义 */
#define M5IOE1_ADC_CTRL_BUSY            (1 << 7)
#define M5IOE1_ADC_CTRL_START           (1 << 6)
#define M5IOE1_ADC_CTRL_CH_MASK         0x07
#define M5IOE1_ADC_CH_DISABLE           0
#define M5IOE1_ADC_CH_ADC1              1  /* IO2 */
#define M5IOE1_ADC_CH_ADC2              2  /* IO4 */
#define M5IOE1_ADC_CH_ADC3              3  /* IO5 */
#define M5IOE1_ADC_CH_ADC4              4  /* IO7 */

/* 温度传感器控制位定义 */
#define M5IOE1_TEMP_CTRL_BUSY           (1 << 7)
#define M5IOE1_TEMP_CTRL_START          (1 << 6)

/* I2C配置位定义 */
#define M5IOE1_I2C_CFG_INTERNAL_PULL    (1 << 6)
#define M5IOE1_I2C_CFG_WAKE_TYPE        (1 << 5)
#define M5IOE1_I2C_CFG_SPD              (1 << 4)
#define M5IOE1_I2C_CFG_SLEEP_MASK       0x0F

/* LED配置位定义 */
#define M5IOE1_LED_CFG_REFRESH          (1 << 6)
#define M5IOE1_LED_CFG_LED_MASK         0x3F

#define M5IOE1_PWM_EN_BIT               (1 << 7)
#define M5IOE1_PWM_POL_BIT              (1 << 6)
#define M5IOE1_PWM_DUTY_MASK            0x0F

/* 默认值定义 */
#define M5IOE1_DEFAULT_GPIO_DRV_L       0x00
#define M5IOE1_DEFAULT_GPIO_DRV_H       0x00
#define M5IOE1_DEFAULT_PWM_FREQ_L       0xF4
#define M5IOE1_DEFAULT_PWM_FREQ_H       0x01
#define M5IOE1_DEFAULT_PWM_FREQ         500

#define I2C_READ_RETRIES                5
#define M5IOE1_RESET_DELAY_MS           1
#define M5IOE1_N_GPIO                   14
#define M5IOE1_ADC_RESOLUTION           12
#define M5IOE1_ADC_MAX_VAL              ((1 << M5IOE1_ADC_RESOLUTION) - 1)

/* ADC超时时间 */
#define M5IOE1_ADC_TIMEOUT_MS           100

/* GPIO方向定义 */
#define M5IOE1_DIRECTION_TO_GPIOD(x) \
    ((x) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN)

#define GPIOD_DIRECTION_TO_M5IOE1(x) \
    ((x) == GPIO_LINE_DIRECTION_OUT ? 1 : 0)

/* ADC通道枚举 */
enum m5ioe1_adc_channel {
    M5IOE1_ADC_CHANNEL_1 = 0,  /* IO2 */
    M5IOE1_ADC_CHANNEL_2,      /* IO4 */
    M5IOE1_ADC_CHANNEL_3,      /* IO5 */
    M5IOE1_ADC_CHANNEL_4,      /* IO7 */
    M5IOE1_ADC_CHANNEL_TEMP,   /* 温度传感器 */
    M5IOE1_ADC_CHANNEL_VREF,   /* 参考电压 */
    M5IOE1_ADC_NUM_CHANNELS,
};

/* Pinctrl功能定义 */
enum m5ioe1_pin_function {
    M5IOE1_FUNC_GPIO = 0,
    M5IOE1_FUNC_ADC,
    M5IOE1_FUNC_PWM,
    M5IOE1_FUNC_LED,
};

struct m5ioe1_priv {
    struct i2c_client *i2c;
    struct regmap *regmap;
    struct gpio_chip gpio;
    struct pwm_chip *pwm_chip;
    struct pinctrl_dev *pctldev;
    struct gpio_desc *reset_gpio;
    struct mutex lock;
    u16 vref_mv;

#ifdef CONFIG_GPIO_PI4IOE5V64XX_IRQ
    struct irq_chip irq_chip;
    struct mutex irq_lock;
    uint16_t irq_mask;
#endif
};

/* ============================================================================
 * Regmap 配置
 * ============================================================================ */

static bool m5ioe1_readable_reg(struct device *dev, unsigned int reg)
{
    return reg <= M5IOE1_REG_MAX;
}

static bool m5ioe1_writeable_reg(struct device *dev, unsigned int reg)
{
    return reg <= M5IOE1_REG_MAX;
}

static bool m5ioe1_volatile_reg(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case M5IOE1_GPIO_I_L:
    case M5IOE1_GPIO_I_H:
    case M5IOE1_ADC_CTRL:
    case M5IOE1_ADC_D_L:
    case M5IOE1_ADC_D_H:
    case M5IOE1_TEMP_CTRL:
    case M5IOE1_TEMP_D_L:
    case M5IOE1_TEMP_D_H:
    case M5IOE1_REF_VOLT_L:
    case M5IOE1_REF_VOLT_H:
    case M5IOE1_RTC_RAM_S ... M5IOE1_RTC_RAM_E:
    case M5IOE1_GPIO_IE_L:
    case M5IOE1_GPIO_IE_H:
        return true;
    default:
        return false;
    }
}

static const struct regmap_config m5ioe1_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = M5IOE1_REG_MAX,
    .writeable_reg = m5ioe1_writeable_reg,
    .readable_reg = m5ioe1_readable_reg,
    .volatile_reg = m5ioe1_volatile_reg,
};

static int m5ioe1_byte_reg_read(void *context, unsigned int reg,
                unsigned int *val)
{
    struct device *dev = context;
    struct i2c_client *i2c = to_i2c_client(dev);
    int ret;
    int retries = I2C_READ_RETRIES;

    if (reg > 0xff)
        return -EINVAL;

    do {
        ret = i2c_smbus_read_byte_data(i2c, reg);
    } while (ret < 0 && retries-- > 0);

    if (ret < 0)
        return ret;

    *val = ret;

    return 0;
}

static int m5ioe1_byte_reg_write(void *context, unsigned int reg,
                 unsigned int val)
{
    struct device *dev = context;
    struct i2c_client *i2c = to_i2c_client(dev);
    int ret;
    int retries = I2C_READ_RETRIES;

    if (val > 0xff || reg > 0xff)
        return -EINVAL;

    do {
        ret = i2c_smbus_write_byte_data(i2c, reg, val);
    } while (ret != 0 && retries-- > 0);

    return ret;
}

static struct regmap_bus m5ioe1_regmap_bus = {
    .reg_write = m5ioe1_byte_reg_write,
    .reg_read = m5ioe1_byte_reg_read,
};

static struct regmap *m5ioe1_setup_regmap(struct i2c_client *i2c,
                      int m5ioe1_dev_id)
{
    if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        return ERR_PTR(-ENOTSUPP);

    if (m5ioe1_dev_id == 0) {
        return devm_regmap_init(&i2c->dev, &m5ioe1_regmap_bus,
                    &i2c->dev, &m5ioe1_regmap_config);
    }

    return ERR_PTR(-EINVAL);
}

/* ============================================================================
 * GPIO 功能实现
 * ============================================================================ */

static int m5ioe1_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
    int ret, io_dir, direction;
    struct m5ioe1_priv *m5ioe1 = gpiochip_get_data(chip);
    struct device *dev = &m5ioe1->i2c->dev;

    if (m5ioe1->gpio.ngpio == M5IOE1_N_GPIO) {
        if (offset < 8) {
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_M_L, &io_dir);
        } else {
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_M_H, &io_dir);
            offset = offset % 8;
        }

        if (ret) {
            dev_err(dev, "Failed to read I/O direction: %d", ret);
            return ret;
        }

        direction = M5IOE1_DIRECTION_TO_GPIOD((io_dir >> offset) & 1);

        dev_dbg(dev, "get_direction : offset=%u, direction=%s, reg=0x%X",
            offset,
            (direction == GPIO_LINE_DIRECTION_IN) ? "input" : "output",
            io_dir);

        return direction;
    }

    return -1;
}

static int m5ioe1_gpio_set_direction(struct gpio_chip *chip, unsigned offset,
                     int direction)
{
    int ret, reg;
    struct m5ioe1_priv *m5ioe1 = gpiochip_get_data(chip);
    struct device *dev = &m5ioe1->i2c->dev;

    dev_dbg(dev, "set_direction : offset=%u, direction=%s", offset,
        (direction == GPIO_LINE_DIRECTION_IN) ? "input" : "output");

    if (m5ioe1->gpio.ngpio == M5IOE1_N_GPIO) {
        if (offset < 8) {
            reg = M5IOE1_GPIO_M_L;
        } else {
            reg = M5IOE1_GPIO_M_H;
            offset = offset % 8;
        }

        ret = regmap_update_bits(m5ioe1->regmap, reg, 1 << offset,
                     GPIOD_DIRECTION_TO_M5IOE1(direction) <<
                     offset);
        if (ret) {
            dev_err(dev, "Failed to set direction: %d", ret);
            return ret;
        }

        return ret;
    }

    return -1;
}

static int m5ioe1_gpio_get(struct gpio_chip *chip, unsigned offset)
{
    int ret, out, reg = 0;
    struct m5ioe1_priv *m5ioe1 = gpiochip_get_data(chip);
    struct device *dev = &m5ioe1->i2c->dev;

    if (m5ioe1->gpio.ngpio == M5IOE1_N_GPIO) {
        if (offset < 8) {
            reg = M5IOE1_GPIO_I_L;
        } else {
            reg = M5IOE1_GPIO_I_H;
            offset = offset % 8;
        }
    }

    ret = regmap_read(m5ioe1->regmap, reg, &out);
    if (ret) {
        dev_err(dev, "Failed to read output: %d", ret);
        return ret;
    }

    dev_dbg(dev, "gpio_get : offset=%u, val=%s reg=0x%X", offset,
        out & (1 << (offset % 8)) ? "1" : "0", out);

    if (out & (1 << (offset % 8)))
        return 1;

    return 0;
}

static int m5ioe1_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
    int ret, reg = 0;
    struct m5ioe1_priv *m5ioe1 = gpiochip_get_data(chip);
    struct device *dev = &m5ioe1->i2c->dev;

    dev_dbg(dev, "gpio_set : offset=%u, val=%s",
        offset, value ? "1" : "0");

    if (m5ioe1->gpio.ngpio != M5IOE1_N_GPIO)
        return -EINVAL;

    if (offset < 8) {
        reg = M5IOE1_GPIO_O_L;
    } else {
        reg = M5IOE1_GPIO_O_H;
        offset = offset % 8;
    }

    ret = regmap_update_bits(m5ioe1->regmap, reg, 1 << offset,
                 value ? (1 << offset) : 0);
    if (ret) {
        dev_err(dev, "Failed to write output: %d", ret);
        return ret;
    }

    return 0;
}

static int m5ioe1_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
    return m5ioe1_gpio_set_direction(chip, offset,
                     GPIO_LINE_DIRECTION_IN);
}

static int m5ioe1_gpio_direction_output(struct gpio_chip *chip,
                    unsigned offset, int value)
{
    int ret;
    struct m5ioe1_priv *m5ioe1 = gpiochip_get_data(chip);
    struct device *dev = &m5ioe1->i2c->dev;

    ret = m5ioe1_gpio_set_direction(chip, offset,
                    GPIO_LINE_DIRECTION_OUT);
    if (ret) {
        dev_err(dev, "Failed to set direction: %d", ret);
        return ret;
    }

    ret = m5ioe1_gpio_set(chip, offset, value);
    if (ret) {
        dev_err(dev, "Failed to set output value: %d", ret);
        return ret;
    }

    return 0;
}

static int m5ioe1_gpio_setup(struct m5ioe1_priv *m5ioe1,
                 int m5ioe1_dev_id)
{
    int ret;
    struct device *dev = &m5ioe1->i2c->dev;
    struct gpio_chip *gc = &m5ioe1->gpio;

    ret = regmap_write(m5ioe1->regmap, M5IOE1_GPIO_DRV_L,
               M5IOE1_DEFAULT_GPIO_DRV_L);
    if (ret) {
        dev_err(dev, "Failed to init GPIO_DRV_L: %d\n", ret);
        return ret;
    }

    ret = regmap_write(m5ioe1->regmap, M5IOE1_GPIO_DRV_H,
               M5IOE1_DEFAULT_GPIO_DRV_H);
    if (ret) {
        dev_err(dev, "Failed to init GPIO_DRV_H: %d\n", ret);
        return ret;
    }

    gc->ngpio = M5IOE1_N_GPIO;
    gc->label = m5ioe1->i2c->name;
    gc->parent = &m5ioe1->i2c->dev;
    gc->owner = THIS_MODULE;
    gc->base = -1;
    gc->can_sleep = true;
    gc->get_direction = m5ioe1_gpio_get_direction;
    gc->direction_input = m5ioe1_gpio_direction_input;
    gc->direction_output = m5ioe1_gpio_direction_output;
    gc->get = m5ioe1_gpio_get;
    gc->set = m5ioe1_gpio_set;

    ret = devm_gpiochip_add_data(dev, gc, m5ioe1);
    if (ret) {
        dev_err(dev, "devm_gpiochip_add_data failed: %d", ret);
        return ret;
    }

    return 0;
}

/* ============================================================================
 * PWM 功能实现
 * ============================================================================ */

static int m5ioe1_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
                const struct pwm_state *state)
{
    struct m5ioe1_priv *pc = pwmchip_get_drvdata(chip);
    struct device *dev = &pc->i2c->dev;
    unsigned int freq, duty;
    unsigned int pwm_l_reg, pwm_h_reg;
    u8 pwm_h_val = 0;
    int ret;

    pwm_l_reg = M5IOE1_PWM1_L + (pwm->hwpwm * 2);
    pwm_h_reg = M5IOE1_PWM1_H + (pwm->hwpwm * 2);

    if (!state->enabled) {
        ret = regmap_update_bits(pc->regmap, pwm_h_reg,
                     M5IOE1_PWM_EN_BIT, 0);
        if (ret) {
            dev_err(dev, "Failed to disable PWM%d: %d\n",
                pwm->hwpwm + 1, ret);
            return ret;
        }

        dev_dbg(dev, "PWM%d disabled\n", pwm->hwpwm + 1);
        return 0;
    }

    if (state->period == 0)
        return -EINVAL;

    freq = DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC, state->period);
    duty = DIV_ROUND_CLOSEST_ULL((u64)state->duty_cycle * 4095,
                     state->period);

    if (duty > 4095)
        duty = 4095;

    dev_dbg(dev,
        "PWM%d: period=%lluns, duty=%lluns, freq=%uHz, duty_val=%u, pol=%d\n",
        pwm->hwpwm + 1, state->period, state->duty_cycle,
        freq, duty, state->polarity);

    ret = regmap_write(pc->regmap, M5IOE1_PWM_FREQ_L, freq & 0xFF);
    if (ret) {
        dev_err(dev, "Failed to write PWM_FREQ_L: %d\n", ret);
        return ret;
    }

    ret = regmap_write(pc->regmap, M5IOE1_PWM_FREQ_H, (freq >> 8) & 0xFF);
    if (ret) {
        dev_err(dev, "Failed to write PWM_FREQ_H: %d\n", ret);
        return ret;
    }

    ret = regmap_write(pc->regmap, pwm_l_reg, duty & 0xFF);
    if (ret) {
        dev_err(dev, "Failed to write PWM%d_L: %d\n",
            pwm->hwpwm + 1, ret);
        return ret;
    }

    pwm_h_val = (duty >> 8) & M5IOE1_PWM_DUTY_MASK;
    pwm_h_val |= M5IOE1_PWM_EN_BIT;

    if (state->polarity == PWM_POLARITY_INVERSED)
        pwm_h_val |= M5IOE1_PWM_POL_BIT;

    ret = regmap_write(pc->regmap, pwm_h_reg, pwm_h_val);
    if (ret) {
        dev_err(dev, "Failed to write PWM%d_H: %d\n",
            pwm->hwpwm + 1, ret);
        return ret;
    }

    dev_dbg(dev, "PWM%d enabled\n", pwm->hwpwm + 1);

    return 0;
}

static const struct pwm_ops m5ioe1_pwm_ops = {
    .apply = m5ioe1_pwm_apply,
};

static int m5ioe1_pwm_setup(struct m5ioe1_priv *m5ioe1,
                int m5ioe1_dev_id)
{
    int ret, i;
    struct device *dev = &m5ioe1->i2c->dev;
    struct pwm_chip *chip;

    chip = devm_pwmchip_alloc(dev, 4, sizeof(*m5ioe1));
    if (IS_ERR(chip)) {
        ret = PTR_ERR(chip);
        dev_err(dev, "Failed to allocate PWM chip: %d\n", ret);
        return ret;
    }

    m5ioe1->pwm_chip = chip;
    pwmchip_set_drvdata(chip, m5ioe1);
    chip->ops = &m5ioe1_pwm_ops;

    ret = regmap_write(m5ioe1->regmap, M5IOE1_PWM_FREQ_L,
               M5IOE1_DEFAULT_PWM_FREQ_L);
    if (ret) {
        dev_err(dev, "Failed to init PWM_FREQ_L: %d\n", ret);
        return ret;
    }

    ret = regmap_write(m5ioe1->regmap, M5IOE1_PWM_FREQ_H,
               M5IOE1_DEFAULT_PWM_FREQ_H);
    if (ret) {
        dev_err(dev, "Failed to init PWM_FREQ_H: %d\n", ret);
        return ret;
    }

    for (i = 0; i < 4; i++) {
        unsigned int pwm_l_reg = M5IOE1_PWM1_L + (i * 2);
        unsigned int pwm_h_reg = M5IOE1_PWM1_H + (i * 2);

        ret = regmap_write(m5ioe1->regmap, pwm_l_reg, 0x00);
        if (ret) {
            dev_err(dev, "Failed to init PWM%d_L: %d\n",
                i + 1, ret);
            return ret;
        }

        ret = regmap_write(m5ioe1->regmap, pwm_h_reg, 0x00);
        if (ret) {
            dev_err(dev, "Failed to init PWM%d_H: %d\n",
                i + 1, ret);
            return ret;
        }
    }

    ret = devm_pwmchip_add(dev, chip);
    if (ret < 0) {
        dev_err(dev, "pwmchip_add() failed: %d\n", ret);
        return ret;
    }

    dev_info(dev, "PWM device registered with %d channels\n", chip->npwm);

    return 0;
}

/* ============================================================================
 * ADC/IIO 功能实现
 * ============================================================================ */

static int m5ioe1_adc_read_raw(struct m5ioe1_priv *m5ioe1,
                   int channel, int *val)
{
    struct device *dev = &m5ioe1->i2c->dev;
    unsigned int ctrl_val, busy;
    unsigned int val_l, val_h;
    int ret;
    unsigned long timeout;

    mutex_lock(&m5ioe1->lock);

    if (channel == M5IOE1_ADC_CHANNEL_TEMP) {
        ret = regmap_write(m5ioe1->regmap, M5IOE1_TEMP_CTRL,
                   M5IOE1_TEMP_CTRL_START);
        if (ret) {
            dev_err(dev, "Failed to start temperature conversion: %d\n",
                ret);
            goto out_unlock;
        }

        timeout = jiffies + msecs_to_jiffies(M5IOE1_ADC_TIMEOUT_MS);

        do {
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_TEMP_CTRL, &busy);
            if (ret) {
                dev_err(dev, "Failed to read TEMP_CTRL: %d\n",
                    ret);
                goto out_unlock;
            }

            if (!(busy & M5IOE1_TEMP_CTRL_BUSY))
                break;

            usleep_range(100, 200);
        } while (time_before(jiffies, timeout));

        if (busy & M5IOE1_TEMP_CTRL_BUSY) {
            dev_err(dev, "Temperature conversion timeout\n");
            ret = -ETIMEDOUT;
            goto out_unlock;
        }

        ret = regmap_read(m5ioe1->regmap, M5IOE1_TEMP_D_L, &val_l);
        if (ret) {
            dev_err(dev, "Failed to read TEMP_D_L: %d\n", ret);
            goto out_unlock;
        }

        ret = regmap_read(m5ioe1->regmap, M5IOE1_TEMP_D_H, &val_h);
        if (ret) {
            dev_err(dev, "Failed to read TEMP_D_H: %d\n", ret);
            goto out_unlock;
        }

        *val = val_l | ((val_h & 0x0F) << 8);

    } else if (channel == M5IOE1_ADC_CHANNEL_VREF) {
        ret = regmap_read(m5ioe1->regmap, M5IOE1_REF_VOLT_L, &val_l);
        if (ret) {
            dev_err(dev, "Failed to read REF_VOLT_L: %d\n", ret);
            goto out_unlock;
        }

        ret = regmap_read(m5ioe1->regmap, M5IOE1_REF_VOLT_H, &val_h);
        if (ret) {
            dev_err(dev, "Failed to read REF_VOLT_H: %d\n", ret);
            goto out_unlock;
        }

        *val = val_l | (val_h << 8);
        m5ioe1->vref_mv = *val;

    } else {
        if (channel < M5IOE1_ADC_CHANNEL_1 ||
            channel > M5IOE1_ADC_CHANNEL_4) {
            ret = -EINVAL;
            goto out_unlock;
        }

        ctrl_val = (channel + 1) | M5IOE1_ADC_CTRL_START;

        ret = regmap_write(m5ioe1->regmap, M5IOE1_ADC_CTRL,
                   ctrl_val);
        if (ret) {
            dev_err(dev, "Failed to start ADC conversion: %d\n",
                ret);
            goto out_unlock;
        }

        timeout = jiffies + msecs_to_jiffies(M5IOE1_ADC_TIMEOUT_MS);

        do {
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_ADC_CTRL, &busy);
            if (ret) {
                dev_err(dev, "Failed to read ADC_CTRL: %d\n",
                    ret);
                goto out_unlock;
            }

            if (!(busy & M5IOE1_ADC_CTRL_BUSY))
                break;

            usleep_range(100, 200);
        } while (time_before(jiffies, timeout));

        if (busy & M5IOE1_ADC_CTRL_BUSY) {
            dev_err(dev, "ADC conversion timeout\n");
            ret = -ETIMEDOUT;
            goto out_unlock;
        }

        ret = regmap_read(m5ioe1->regmap, M5IOE1_ADC_D_L, &val_l);
        if (ret) {
            dev_err(dev, "Failed to read ADC_D_L: %d\n", ret);
            goto out_unlock;
        }

        ret = regmap_read(m5ioe1->regmap, M5IOE1_ADC_D_H, &val_h);
        if (ret) {
            dev_err(dev, "Failed to read ADC_D_H: %d\n", ret);
            goto out_unlock;
        }

        *val = val_l | ((val_h & 0x0F) << 8);
    }

    ret = 0;

    dev_dbg(dev, "ADC read channel %d: raw=%d\n", channel, *val);

out_unlock:
    mutex_unlock(&m5ioe1->lock);
    return ret;
}

static int m5ioe1_iio_read_raw(struct iio_dev *indio_dev,
                   struct iio_chan_spec const *chan,
                   int *val, int *val2, long mask)
{
    struct m5ioe1_priv *m5ioe1 = iio_priv(indio_dev);
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        ret = m5ioe1_adc_read_raw(m5ioe1, chan->channel, val);
        if (ret < 0)
            return ret;

        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        if (chan->type == IIO_VOLTAGE) {
            *val = m5ioe1->vref_mv;
            *val2 = M5IOE1_ADC_RESOLUTION;
            return IIO_VAL_FRACTIONAL_LOG2;
        } else if (chan->type == IIO_TEMP) {
            *val = 1;
            return IIO_VAL_INT;
        }

        return -EINVAL;

    default:
        return -EINVAL;
    }
}

static const struct iio_info m5ioe1_iio_info = {
    .read_raw = m5ioe1_iio_read_raw,
};

#define M5IOE1_ADC_CHANNEL(_idx, _type, _name) {	\
    .type = _type,					\
    .indexed = 1,					\
    .channel = _idx,				\
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
                  BIT(IIO_CHAN_INFO_SCALE),	\
    .datasheet_name = _name,			\
}

static const struct iio_chan_spec m5ioe1_iio_channels[] = {
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_1, IIO_VOLTAGE, "ADC1_IO2"),
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_2, IIO_VOLTAGE, "ADC2_IO4"),
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_3, IIO_VOLTAGE, "ADC3_IO5"),
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_4, IIO_VOLTAGE, "ADC4_IO7"),
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_TEMP, IIO_TEMP, "TEMP"),
    M5IOE1_ADC_CHANNEL(M5IOE1_ADC_CHANNEL_VREF, IIO_VOLTAGE, "VREF"),
};

static int m5ioe1_adc_setup(struct m5ioe1_priv *m5ioe1)
{
    struct device *dev = &m5ioe1->i2c->dev;
    struct iio_dev *indio_dev;
    struct m5ioe1_priv *priv;
    int ret;
    unsigned int vref_l, vref_h;

    indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
    if (!indio_dev)
        return -ENOMEM;

    priv = iio_priv(indio_dev);
    *priv = *m5ioe1;

    indio_dev->name = m5ioe1->i2c->name;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &m5ioe1_iio_info;
    indio_dev->channels = m5ioe1_iio_channels;
    indio_dev->num_channels = ARRAY_SIZE(m5ioe1_iio_channels);

    ret = regmap_read(m5ioe1->regmap, M5IOE1_REF_VOLT_L, &vref_l);
    if (ret) {
        dev_err(dev, "Failed to read REF_VOLT_L: %d\n", ret);
        return ret;
    }

    ret = regmap_read(m5ioe1->regmap, M5IOE1_REF_VOLT_H, &vref_h);
    if (ret) {
        dev_err(dev, "Failed to read REF_VOLT_H: %d\n", ret);
        return ret;
    }

    m5ioe1->vref_mv = vref_l | (vref_h << 8);
    if (m5ioe1->vref_mv == 0)
        m5ioe1->vref_mv = 3300;

    dev_info(dev, "Reference voltage: %u mV\n", m5ioe1->vref_mv);

    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "Failed to register IIO device: %d\n", ret);
        return ret;
    }

    dev_info(dev, "ADC device registered with %d channels\n",
         indio_dev->num_channels);

    return 0;
}

/* ============================================================================
 * Pinctrl 功能实现
 * ============================================================================ */

static const struct pinctrl_pin_desc m5ioe1_pins[] = {
    PINCTRL_PIN(0, "IO1"),
    PINCTRL_PIN(1, "IO2"),
    PINCTRL_PIN(2, "IO3"),
    PINCTRL_PIN(3, "IO4"),
    PINCTRL_PIN(4, "IO5"),
    PINCTRL_PIN(5, "IO6"),
    PINCTRL_PIN(6, "IO7"),
    PINCTRL_PIN(7, "IO8"),
    PINCTRL_PIN(8, "IO9"),
    PINCTRL_PIN(9, "IO10"),
    PINCTRL_PIN(10, "IO11"),
    PINCTRL_PIN(11, "IO12"),
    PINCTRL_PIN(12, "IO13"),
    PINCTRL_PIN(13, "IO14"),
};

static int m5ioe1_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
    return 0;
}

static const char *m5ioe1_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
                         unsigned selector)
{
    return NULL;
}

static int m5ioe1_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
                     unsigned selector,
                     const unsigned **pins,
                     unsigned *num_pins)
{
    return -EINVAL;
}

static const struct pinctrl_ops m5ioe1_pinctrl_ops = {
    .get_groups_count = m5ioe1_pinctrl_get_groups_count,
    .get_group_name = m5ioe1_pinctrl_get_group_name,
    .get_group_pins = m5ioe1_pinctrl_get_group_pins,
    .dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
    .dt_free_map = pinconf_generic_dt_free_map,
};

static int m5ioe1_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
    return 0;
}

static const char *m5ioe1_pinmux_get_function_name(struct pinctrl_dev *pctldev,
                           unsigned selector)
{
    return NULL;
}

static int m5ioe1_pinmux_get_function_groups(struct pinctrl_dev *pctldev,
                         unsigned selector,
                         const char * const **groups,
                         unsigned * const num_groups)
{
    return -EINVAL;
}

static int m5ioe1_pinmux_set_mux(struct pinctrl_dev *pctldev,
                 unsigned func_selector,
                 unsigned group_selector)
{
    return 0;
}

static const struct pinmux_ops m5ioe1_pinmux_ops = {
    .get_functions_count = m5ioe1_pinmux_get_functions_count,
    .get_function_name = m5ioe1_pinmux_get_function_name,
    .get_function_groups = m5ioe1_pinmux_get_function_groups,
    .set_mux = m5ioe1_pinmux_set_mux,
};

static int m5ioe1_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
                  unsigned long *config)
{
    struct m5ioe1_priv *m5ioe1 = pinctrl_dev_get_drvdata(pctldev);
    struct device *dev = &m5ioe1->i2c->dev;
    enum pin_config_param param = pinconf_to_config_param(*config);
    unsigned int val, reg;
    int ret;
    u16 arg = 0;

    if (pin >= M5IOE1_N_GPIO)
        return -EINVAL;

    if (pin < 8)
        reg = pin;
    else
        reg = pin % 8;

    switch (param) {
    case PIN_CONFIG_BIAS_PULL_UP:
        if (pin < 8)
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_PU_L, &val);
        else
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_PU_H, &val);

        if (ret)
            return ret;

        arg = !!(val & (1 << reg));
        break;

    case PIN_CONFIG_BIAS_PULL_DOWN:
        if (pin < 8)
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_PD_L, &val);
        else
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_PD_H, &val);

        if (ret)
            return ret;

        arg = !!(val & (1 << reg));
        break;

    case PIN_CONFIG_DRIVE_OPEN_DRAIN:
        if (pin < 8)
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_DRV_L, &val);
        else
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_DRV_H, &val);

        if (ret)
            return ret;

        arg = !!(val & (1 << reg));
        break;

    case PIN_CONFIG_DRIVE_PUSH_PULL:
        if (pin < 8)
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_DRV_L, &val);
        else
            ret = regmap_read(m5ioe1->regmap,
                      M5IOE1_GPIO_DRV_H, &val);

        if (ret)
            return ret;

        arg = !(val & (1 << reg));
        break;

    default:
        dev_dbg(dev, "Unsupported pinconf param %d\n", param);
        return -ENOTSUPP;
    }

    *config = pinconf_to_config_packed(param, arg);

    return 0;
}

static int m5ioe1_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
                  unsigned long *configs, unsigned num_configs)
{
    struct m5ioe1_priv *m5ioe1 = pinctrl_dev_get_drvdata(pctldev);
    struct device *dev = &m5ioe1->i2c->dev;
    enum pin_config_param param;
    u32 arg;
    unsigned int reg_pu, reg_pd, reg_drv;
    int i, ret;
    unsigned int bit;

    if (pin >= M5IOE1_N_GPIO)
        return -EINVAL;

    if (pin < 8) {
        reg_pu = M5IOE1_GPIO_PU_L;
        reg_pd = M5IOE1_GPIO_PD_L;
        reg_drv = M5IOE1_GPIO_DRV_L;
        bit = pin;
    } else {
        reg_pu = M5IOE1_GPIO_PU_H;
        reg_pd = M5IOE1_GPIO_PD_H;
        reg_drv = M5IOE1_GPIO_DRV_H;
        bit = pin % 8;
    }

    for (i = 0; i < num_configs; i++) {
        param = pinconf_to_config_param(configs[i]);
        arg = pinconf_to_config_argument(configs[i]);

        switch (param) {
        case PIN_CONFIG_BIAS_DISABLE:
            ret = regmap_update_bits(m5ioe1->regmap, reg_pu,
                         1 << bit, 0);
            if (ret)
                return ret;

            ret = regmap_update_bits(m5ioe1->regmap, reg_pd,
                         1 << bit, 0);
            if (ret)
                return ret;

            dev_dbg(dev, "Pin %u: bias disabled\n", pin);
            break;

        case PIN_CONFIG_BIAS_PULL_UP:
            ret = regmap_update_bits(m5ioe1->regmap, reg_pu,
                         1 << bit, 1 << bit);
            if (ret)
                return ret;

            ret = regmap_update_bits(m5ioe1->regmap, reg_pd,
                         1 << bit, 0);
            if (ret)
                return ret;

            dev_dbg(dev, "Pin %u: pull-up enabled\n", pin);
            break;

        case PIN_CONFIG_BIAS_PULL_DOWN:
            ret = regmap_update_bits(m5ioe1->regmap, reg_pu,
                         1 << bit, 0);
            if (ret)
                return ret;

            ret = regmap_update_bits(m5ioe1->regmap, reg_pd,
                         1 << bit, 1 << bit);
            if (ret)
                return ret;

            dev_dbg(dev, "Pin %u: pull-down enabled\n", pin);
            break;

        case PIN_CONFIG_DRIVE_OPEN_DRAIN:
            ret = regmap_update_bits(m5ioe1->regmap, reg_drv,
                         1 << bit, 1 << bit);
            if (ret)
                return ret;

            dev_dbg(dev, "Pin %u: open-drain mode\n", pin);
            break;

        case PIN_CONFIG_DRIVE_PUSH_PULL:
            ret = regmap_update_bits(m5ioe1->regmap, reg_drv,
                         1 << bit, 0);
            if (ret)
                return ret;

            dev_dbg(dev, "Pin %u: push-pull mode\n", pin);
            break;

        default:
            dev_err(dev, "Unsupported pinconf param %d\n", param);
            return -ENOTSUPP;
        }
    }

    return 0;
}

static const struct pinconf_ops m5ioe1_pinconf_ops = {
    .pin_config_get = m5ioe1_pinconf_get,
    .pin_config_set = m5ioe1_pinconf_set,
    .is_generic = true,
};

static struct pinctrl_desc m5ioe1_pinctrl_desc = {
    .name = "m5ioe1-pinctrl",
    .pins = m5ioe1_pins,
    .npins = ARRAY_SIZE(m5ioe1_pins),
    .pctlops = &m5ioe1_pinctrl_ops,
    .pmxops = &m5ioe1_pinmux_ops,
    .confops = &m5ioe1_pinconf_ops,
    .owner = THIS_MODULE,
};

static int m5ioe1_pinctrl_setup(struct m5ioe1_priv *m5ioe1)
{
    struct device *dev = &m5ioe1->i2c->dev;
    int ret;

    m5ioe1->pctldev = devm_pinctrl_register(dev,
                         &m5ioe1_pinctrl_desc,
                         m5ioe1);
    if (IS_ERR(m5ioe1->pctldev)) {
        ret = PTR_ERR(m5ioe1->pctldev);
        dev_err(dev, "Failed to register pinctrl device: %d\n", ret);
        return ret;
    }

    dev_info(dev, "Pinctrl device registered with %d pins\n",
         m5ioe1_pinctrl_desc.npins);

    return 0;
}

/* ============================================================================
 * Reset 和 Probe 实现
 * ============================================================================ */

static int m5ioe1_reset_setup(struct m5ioe1_priv *m5ioe1)
{
    /*
    struct i2c_client *client = m5ioe1->i2c;
    struct device *dev = &client->dev;
    struct gpio_desc *gpio;

    gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(gpio))
        return PTR_ERR(gpio);

    if (gpio) {
        dev_info(dev, "Reset pin=%d\n", desc_to_gpio(gpio));
        m5ioe1->reset_gpio = gpio;
    }
    */

    return 0;
}

static void m5ioe1_reset(struct m5ioe1_priv *m5ioe1)
{
    /*
    struct i2c_client *client = m5ioe1->i2c;
    struct device *dev = &client->dev;

    if (m5ioe1->reset_gpio) {
        dev_info(dev, "Resetting\n");

        gpiod_set_value(m5ioe1->reset_gpio, 0);
        msleep(M5IOE1_RESET_DELAY_MS);

        gpiod_set_value(m5ioe1->reset_gpio, 1);
        msleep(M5IOE1_RESET_DELAY_MS);

        gpiod_set_value(m5ioe1->reset_gpio, 0);
        msleep(M5IOE1_RESET_DELAY_MS);
    }
    */
}

static int m5ioe1_probe(struct i2c_client *client)
{
    int ret;
    struct device *dev = &client->dev;
    struct m5ioe1_priv *m5ioe1;
    const struct i2c_device_id *id = i2c_client_get_device_id(client);
    int m5ioe1_dev_id = (int)id->driver_data;

    dev_info(dev, "m5ioe1 probe()\n");

    m5ioe1 = devm_kzalloc(dev, sizeof(struct m5ioe1_priv), GFP_KERNEL);
    if (!m5ioe1)
        return -ENOMEM;

    i2c_set_clientdata(client, m5ioe1);

    m5ioe1->i2c = client;

    mutex_init(&m5ioe1->lock);

    m5ioe1->regmap = m5ioe1_setup_regmap(client, m5ioe1_dev_id);
    if (IS_ERR(m5ioe1->regmap)) {
        ret = PTR_ERR(m5ioe1->regmap);
        dev_err(&client->dev, "Failed to init register map: %d\n",
            ret);
        goto err_mutex;
    }

    ret = m5ioe1_reset_setup(m5ioe1);
    if (ret < 0) {
        dev_err(dev, "failed to configure reset-gpio: %d", ret);
        goto err_mutex;
    }

    m5ioe1_reset(m5ioe1);

    ret = m5ioe1_pinctrl_setup(m5ioe1);
    if (ret < 0) {
        dev_err(dev, "Failed to setup Pinctrl: %d", ret);
        goto err_mutex;
    }

    ret = m5ioe1_gpio_setup(m5ioe1, m5ioe1_dev_id);
    if (ret < 0) {
        dev_err(dev, "Failed to setup GPIOs: %d", ret);
        goto err_mutex;
    }

    ret = m5ioe1_pwm_setup(m5ioe1, m5ioe1_dev_id);
    if (ret < 0) {
        dev_err(dev, "Failed to setup PWM: %d", ret);
        goto err_mutex;
    }

    ret = m5ioe1_adc_setup(m5ioe1);
    if (ret < 0) {
        dev_err(dev, "Failed to setup ADC: %d", ret);
        goto err_mutex;
    }

    dev_info(dev, "probe finished successfully");

    return 0;

err_mutex:
    mutex_destroy(&m5ioe1->lock);
    return ret;
}

static void m5ioe1_remove(struct i2c_client *client)
{
    struct m5ioe1_priv *m5ioe1 = i2c_get_clientdata(client);

    mutex_destroy(&m5ioe1->lock);
}

static const struct i2c_device_id m5ioe1_id_table[] = {
    { "m5ioe1", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, m5ioe1_id_table);

#ifdef CONFIG_OF
static const struct of_device_id m5ioe1_of_match[] = {
    { .compatible = "m5stack,m5ioe1", .data = (void *)0 },
    { }
};
MODULE_DEVICE_TABLE(of, m5ioe1_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id m5ioe1_acpi_match_table[] = {
    { "M5IOE1", 0 },
    { }
};
MODULE_DEVICE_TABLE(acpi, m5ioe1_acpi_match_table);
#endif

static struct i2c_driver m5ioe1_driver = {
    .driver = {
        .name = "m5ioe1-gpio",
        .of_match_table = of_match_ptr(m5ioe1_of_match),
#ifdef CONFIG_ACPI
        .acpi_match_table = ACPI_PTR(m5ioe1_acpi_match_table),
#endif
    },
    .probe = m5ioe1_probe,
    .remove = m5ioe1_remove,
    .id_table = m5ioe1_id_table,
};

static int __init m5ioe1_init(void)
{
    return i2c_add_driver(&m5ioe1_driver);
}
subsys_initcall(m5ioe1_init);

static void __exit m5ioe1_exit(void)
{
    i2c_del_driver(&m5ioe1_driver);
}
module_exit(m5ioe1_exit);

MODULE_AUTHOR("dianjixz <dianjixz@m5stack.com>");
MODULE_DESCRIPTION("M5IOE1 I2C GPIO expander driver with ADC and Pinctrl");
MODULE_LICENSE("GPL v2");