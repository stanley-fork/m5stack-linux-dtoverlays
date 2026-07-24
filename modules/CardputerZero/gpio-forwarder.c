// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>

struct gpio_forwarder {
	struct device *dev;
	struct gpio_chip chip;
	struct gpio_desc *backing;
	struct device_node *spi_controller;
	struct notifier_block of_notifier;
	struct mutex lock;
	u32 spi_bus_num;
	u32 spi_chip_select;
	bool requested;
	bool output;
	int value;
};

static bool __maybe_unused
gpio_forwarder_matches_spi_device(struct gpio_forwarder *forwarder,
				  struct device_node *node)
{
	u32 chip_select;

	if (!node || node->parent != forwarder->spi_controller)
		return false;

	return !of_property_read_u32(node, "reg", &chip_select) &&
	       chip_select == forwarder->spi_chip_select;
}

static bool gpio_forwarder_spi_device_enabled(struct gpio_forwarder *forwarder)
{
#if IS_ENABLED(CONFIG_OF)
	struct device_node *child;
	bool enabled = false;

	for_each_available_child_of_node(forwarder->spi_controller, child) {
		if (gpio_forwarder_matches_spi_device(forwarder, child)) {
			enabled = true;
			of_node_put(child);
			break;
		}
	}

	return enabled;
#else
	return false;
#endif
}

static int gpio_forwarder_apply_state(struct gpio_forwarder *forwarder)
{
	if (forwarder->output)
		return gpiod_direction_output_raw(forwarder->backing,
						  forwarder->value);

	return gpiod_direction_input(forwarder->backing);
}

static int gpio_forwarder_enable_locked(struct gpio_forwarder *forwarder)
{
	struct gpio_desc *backing;
	int ret;

	lockdep_assert_held(&forwarder->lock);

	if (forwarder->backing || !forwarder->requested)
		return 0;

	backing = gpiod_get(forwarder->dev, "backing", GPIOD_ASIS);
	if (IS_ERR(backing))
		return dev_err_probe(forwarder->dev, PTR_ERR(backing),
				     "failed to request backing GPIO\n");

	forwarder->backing = backing;
	ret = gpio_forwarder_apply_state(forwarder);
	if (ret) {
		gpiod_put(forwarder->backing);
		forwarder->backing = NULL;
		return ret;
	}

	dev_dbg(forwarder->dev, "backing GPIO enabled\n");
	return 0;
}

static void gpio_forwarder_disable_locked(struct gpio_forwarder *forwarder)
{
	lockdep_assert_held(&forwarder->lock);

	if (!forwarder->backing)
		return;

	gpiod_put(forwarder->backing);
	forwarder->backing = NULL;
	dev_dbg(forwarder->dev, "backing GPIO disabled\n");
}

static int gpio_forwarder_sync_locked(struct gpio_forwarder *forwarder)
{
	lockdep_assert_held(&forwarder->lock);

	if (gpio_forwarder_spi_device_enabled(forwarder))
		return gpio_forwarder_enable_locked(forwarder);

	gpio_forwarder_disable_locked(forwarder);
	return 0;
}

static int gpio_forwarder_request(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&forwarder->lock);
	forwarder->requested = true;
	ret = gpio_forwarder_sync_locked(forwarder);
	if (ret)
		forwarder->requested = false;
	mutex_unlock(&forwarder->lock);

	return ret;
}

static void gpio_forwarder_free(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);

	mutex_lock(&forwarder->lock);
	forwarder->requested = false;
	gpio_forwarder_disable_locked(forwarder);
	mutex_unlock(&forwarder->lock);
}

static int gpio_forwarder_get_direction(struct gpio_chip *chip,
					unsigned int offset)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int direction;

	mutex_lock(&forwarder->lock);
	direction = forwarder->output ? GPIO_LINE_DIRECTION_OUT :
					GPIO_LINE_DIRECTION_IN;
	mutex_unlock(&forwarder->lock);

	return direction;
}

static int gpio_forwarder_direction_input(struct gpio_chip *chip,
					  unsigned int offset)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&forwarder->lock);
	forwarder->output = false;
	ret = gpio_forwarder_sync_locked(forwarder);
	if (!ret && forwarder->backing)
		ret = gpiod_direction_input(forwarder->backing);
	mutex_unlock(&forwarder->lock);

	return ret;
}

static int gpio_forwarder_direction_output(struct gpio_chip *chip,
					   unsigned int offset, int value)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&forwarder->lock);
	forwarder->output = true;
	forwarder->value = value;
	ret = gpio_forwarder_sync_locked(forwarder);
	if (!ret && forwarder->backing)
		ret = gpiod_direction_output_raw(forwarder->backing, value);
	mutex_unlock(&forwarder->lock);

	return ret;
}

static int gpio_forwarder_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int value;

	mutex_lock(&forwarder->lock);
	if (forwarder->backing)
		value = gpiod_get_raw_value_cansleep(forwarder->backing);
	else
		value = forwarder->value;
	mutex_unlock(&forwarder->lock);

	return value;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
static int gpio_forwarder_set(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&forwarder->lock);
	forwarder->value = value;
	ret = gpio_forwarder_sync_locked(forwarder);
	if (!ret && forwarder->backing)
		ret = gpiod_set_raw_value_cansleep(forwarder->backing, value);
	mutex_unlock(&forwarder->lock);

	return ret;
}
#else
static void gpio_forwarder_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	struct gpio_forwarder *forwarder = gpiochip_get_data(chip);

	mutex_lock(&forwarder->lock);
	forwarder->value = value;
	if (!gpio_forwarder_sync_locked(forwarder) && forwarder->backing)
		gpiod_set_raw_value_cansleep(forwarder->backing, value);
	mutex_unlock(&forwarder->lock);
}
#endif

static int gpio_forwarder_of_notify(struct notifier_block *notifier,
				    unsigned long action, void *arg)
{
#if IS_ENABLED(CONFIG_OF)
	struct gpio_forwarder *forwarder =
		container_of(notifier, struct gpio_forwarder, of_notifier);
	int ret;

	mutex_lock(&forwarder->lock);
	ret = gpio_forwarder_sync_locked(forwarder);
	mutex_unlock(&forwarder->lock);

	return notifier_from_errno(ret);
#else
	return NOTIFY_DONE;
#endif
}

static void gpio_forwarder_release_backing(void *data)
{
	struct gpio_forwarder *forwarder = data;

	mutex_lock(&forwarder->lock);
	gpio_forwarder_disable_locked(forwarder);
	mutex_unlock(&forwarder->lock);
	of_node_put(forwarder->spi_controller);
}

static void gpio_forwarder_unregister_notifier(void *data)
{
	struct gpio_forwarder *forwarder = data;

	of_reconfig_notifier_unregister(&forwarder->of_notifier);
}

static int gpio_forwarder_probe(struct platform_device *pdev)
{
	static const char * const line_names[] = { "gpiospi" };
	struct device *dev = &pdev->dev;
	struct gpio_forwarder *forwarder;
	struct gpio_chip *chip;
	char spi_alias[16];
	int ret;

	forwarder = devm_kzalloc(dev, sizeof(*forwarder), GFP_KERNEL);
	if (!forwarder)
		return -ENOMEM;

	forwarder->dev = dev;
	ret = of_property_read_u32(dev->of_node, "spi-bus-num",
				   &forwarder->spi_bus_num);
	if (ret)
		return dev_err_probe(dev, ret,
				     "missing spi-bus-num property\n");

	snprintf(spi_alias, sizeof(spi_alias), "spi%u",
		 forwarder->spi_bus_num);
	forwarder->spi_controller = of_find_node_by_path(spi_alias);
	if (!forwarder->spi_controller)
		return dev_err_probe(dev, -ENODEV,
				     "cannot find %s device-tree alias\n",
				     spi_alias);

	ret = of_property_read_u32(dev->of_node, "spi-chip-select",
				   &forwarder->spi_chip_select);
	if (ret) {
		of_node_put(forwarder->spi_controller);
		return dev_err_probe(dev, ret,
				     "missing spi-chip-select property\n");
	}

	mutex_init(&forwarder->lock);
	forwarder->output = true;
	forwarder->value = 0;

	ret = devm_add_action_or_reset(dev, gpio_forwarder_release_backing,
				       forwarder);
	if (ret)
		return ret;

	chip = &forwarder->chip;
	chip->label = dev_name(dev);
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->request = gpio_forwarder_request;
	chip->free = gpio_forwarder_free;
	chip->get_direction = gpio_forwarder_get_direction;
	chip->direction_input = gpio_forwarder_direction_input;
	chip->direction_output = gpio_forwarder_direction_output;
	chip->get = gpio_forwarder_get;
	chip->set = gpio_forwarder_set;
	chip->base = -1;
	chip->ngpio = ARRAY_SIZE(line_names);
	chip->names = line_names;
	chip->can_sleep = true;

	ret = devm_gpiochip_add_data(dev, chip, forwarder);
	if (ret)
		return ret;

	forwarder->of_notifier.notifier_call = gpio_forwarder_of_notify;
	ret = of_reconfig_notifier_register(&forwarder->of_notifier);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register OF notifier\n");

	return devm_add_action_or_reset(dev, gpio_forwarder_unregister_notifier,
					forwarder);
}

static const struct of_device_id gpio_forwarder_of_match[] = {
	{ .compatible = "m5stack,gpio-forwarder" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_forwarder_of_match);

static struct platform_driver gpio_forwarder_driver = {
	.probe = gpio_forwarder_probe,
	.driver = {
		.name = "m5stack-gpio-forwarder",
		.of_match_table = gpio_forwarder_of_match,
	},
};
module_platform_driver(gpio_forwarder_driver);

MODULE_AUTHOR("M5Stack");
MODULE_DESCRIPTION("Dynamically activated single-line GPIO forwarder");
MODULE_LICENSE("GPL");
