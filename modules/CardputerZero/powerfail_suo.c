// SPDX-License-Identifier: GPL-2.0
/*
 * Power fail GPIO driver
 *
 * 检测到掉电 GPIO 低电平有效后，执行：
 *
 *   S: handle_sysrq('s')
 *   U: handle_sysrq('u')
 *   O: kernel_power_off()
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/atomic.h>

struct powerfail_suo_data {
    struct device *dev;
    struct gpio_desc *gpiod;
    int irq;
    u32 debounce_ms;
    atomic_t triggered;
    struct work_struct work;
};

static void powerfail_suo_work(struct work_struct *work)
{
    struct powerfail_suo_data *data =
        container_of(work, struct powerfail_suo_data, work);
    int val;

    /*
     * 软件消抖。
     */
    if (data->debounce_ms)
        msleep(data->debounce_ms);

    /*
     * irq-gpios 使用原始电平判断：
     *     实际低电平 -> 掉电信号有效
     *     实际高电平 -> 掉电信号无效
     */
    val = gpiod_get_raw_value_cansleep(data->gpiod);
    if (val < 0) {
        dev_err(data->dev, "failed to read powerfail gpio: %d\n", val);

        atomic_set(&data->triggered, 0);
        enable_irq(data->irq);
        return;
    }

    if (val > 0) {
        dev_warn(data->dev,
                 "powerfail irq occurred, but gpio is inactive, val=%d\n",
                 val);

        atomic_set(&data->triggered, 0);
        enable_irq(data->irq);
        return;
    }

    // dev_emerg(data->dev, "Power fail detected, execute SysRq S-U-O sequence\n");
    // handle_sysrq('i');
    // msleep(100);
    // /*
    //  * S: sync
    //  *
    //  * 对应 Magic SysRq:
    //  *   echo s > /proc/sysrq-trigger
    // */
    // dev_emerg(data->dev, "SysRq S: emergency sync filesystems\n");
    // handle_sysrq('s');
    // // msleep(2000);

    /*
     * U: remount readonly
     *
     * 对应 Magic SysRq:
     *   echo u > /proc/sysrq-trigger
     */
    dev_emerg(data->dev, "SysRq U: emergency remount filesystems readonly\n");
    handle_sysrq('u');

    // /*
    //  * O: power off
    //  *
    //  * 对应 Magic SysRq:
    //  *   echo o > /proc/sysrq-trigger
    //  *
    //  * 注意：
    //  * kernel_power_off() 是否真正能断电，取决于你的板级 PMIC、
    //  * 电源管理驱动、pm_power_off 回调是否配置正确。
    //  */
    // dev_emerg(data->dev, "SysRq O: power off now\n");
    // kernel_power_off();

    /*
     * 正常情况下不会执行到这里。
     */
    // dev_emerg(data->dev, "kernel_power_off returned unexpectedly\n");
}

static irqreturn_t powerfail_suo_irq_handler(int irq, void *dev_id)
{
    struct powerfail_suo_data *data = dev_id;

    /*
     * 防止重复触发。
     */
    if (atomic_xchg(&data->triggered, 1))
        return IRQ_HANDLED;

    /*
     * 禁止后续中断。
     * 掉电处理只允许执行一次。
     */
    disable_irq_nosync(data->irq);

    /*
     * 不在硬中断里面执行 sync/remount/poweroff。
     * 放到 workqueue 里执行。
     */
    schedule_work(&data->work);

    return IRQ_HANDLED;
}

static int powerfail_suo_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct powerfail_suo_data *data;
    int ret;

    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->dev = dev;
    atomic_set(&data->triggered, 0);
    INIT_WORK(&data->work, powerfail_suo_work);

    of_property_read_u32(dev->of_node, "debounce-ms", &data->debounce_ms);

    /*
     * 对应设备树属性：
     *
     *   irq-gpios = <&gpio 23 0x00>;
     */
    data->gpiod = devm_gpiod_get(dev, "irq", GPIOD_IN);
    if (IS_ERR(data->gpiod)) {
        ret = PTR_ERR(data->gpiod);
        dev_err(dev, "failed to get irq gpio: %d\n", ret);
        return ret;
    }

    data->irq = platform_get_irq(pdev, 0);
    if (data->irq < 0) {
        dev_err(dev, "failed to get irq from interrupts property: %d\n",
                data->irq);
        return data->irq;
    }

    /*
     * 中断触发方式由设备树 interrupts 属性决定：
     *
     *   interrupt-parent = <&gpio>;
     *   interrupts = <23 2>;  // 下降沿
     */
    ret = devm_request_irq(dev,
                           data->irq,
                           powerfail_suo_irq_handler,
                           0,
                           dev_name(dev),
                           data);
    if (ret) {
        dev_err(dev, "failed to request irq %d: %d\n",
                data->irq, ret);
        return ret;
    }

    platform_set_drvdata(pdev, data);

    dev_info(dev,
             "powerfail-suo driver probed, irq=%d, debounce=%u ms\n",
             data->irq, data->debounce_ms);

    return 0;
}

static void powerfail_suo_remove(struct platform_device *pdev)
{
    struct powerfail_suo_data *data = platform_get_drvdata(pdev);

    if (data)
        cancel_work_sync(&data->work);
}

static const struct of_device_id powerfail_suo_of_match[] = {
    { .compatible = "cardputerzero,powerfail-suo" },
    { }
};
MODULE_DEVICE_TABLE(of, powerfail_suo_of_match);

static struct platform_driver powerfail_suo_driver = {
    .probe  = powerfail_suo_probe,
    .remove = powerfail_suo_remove,
    .driver = {
        .name = "powerfail-suo",
        .of_match_table = powerfail_suo_of_match,
    },
};

module_platform_driver(powerfail_suo_driver);

MODULE_AUTHOR("dianjixz <dianjixz@m5stack.com>");
MODULE_DESCRIPTION("Power fail GPIO SysRq S-U-O driver");
MODULE_LICENSE("GPL");
