// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis magnetometer driver supporting following I2C Bosch-Sensortec chips:
 *  - BMC150
 *  - BMC156
 *  - BMM150
 *
 * Copyright (c) 2016, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/device/bus.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "bmc150_magn.h"
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#define DRV_NAME	"bmi270-regmap-bridge"


#define BMI270_REG_STATUS		0x03
#define BMI270_REG_AUX_DATA_0		0x04
#define BMI270_REG_AUX_DATA_1		0x05
#define BMI270_REG_AUX_DATA_2		0x06
#define BMI270_REG_AUX_DATA_3		0x07
#define BMI270_REG_AUX_DATA_4		0x08
#define BMI270_REG_AUX_DATA_5		0x09
#define BMI270_REG_AUX_DATA_6		0x0a
#define BMI270_REG_AUX_DATA_7		0x0b
#define BMI270_REG_AUX_CONF		0x44
#define BMI270_REG_AUX_DEV_ID		0x4b
#define BMI270_REG_AUX_IF_CONF		0x4c
#define BMI270_REG_AUX_RD_ADDR		0x4d
#define BMI270_REG_AUX_WR_ADDR		0x4e
#define BMI270_REG_AUX_WR_DATA		0x4f
#define BMI270_REG_IF_CONF		0x6b
#define BMI270_REG_PWR_CTRL		0x7d

#define BMI270_STATUS_AUX_BUSY		BIT(2)
#define BMI270_IF_CONF_AUX_EN		BIT(5)
#define BMI270_PWR_CTRL_AUX_EN		BIT(0)
#define BMI270_AUX_IF_CONF_MANUAL_MODE	BIT(7)
#define BMI270_AUX_IF_CONF_MAN_RD_BURST_8B	(0x03 << 2)
#define BMI270_AUX_IF_CONF_AUX_RD_BURST_8B	0x03
#define BMI270_AUX_IF_CONF_8B_MANUAL	\
	(BMI270_AUX_IF_CONF_MANUAL_MODE | \
	 BMI270_AUX_IF_CONF_MAN_RD_BURST_8B | \
	 BMI270_AUX_IF_CONF_AUX_RD_BURST_8B)
#define BMI270_AUX_IF_CONF_8B_SYNC	\
	(BMI270_AUX_IF_CONF_MAN_RD_BURST_8B | \
	 BMI270_AUX_IF_CONF_AUX_RD_BURST_8B)
#define BMI270_AUX_CONF_ODR_25HZ		0x06
#define BMI270_AUX_CONF_ODR_100HZ		0x0A

#define BMM150_CHIP_ID_REG		0x40
#define BMM150_REG_X_L			0x42
#define BMM150_DATA_LEN			8

enum bmi270_aux_mode {
	BMI270_AUX_MODE_MANUAL,
	BMI270_AUX_MODE_SYNC,
};

struct bmi270_aux_bridge {
	struct device *dev;
	struct regmap *parent_regmap;
	u8 aux_i2c_addr;
	enum bmi270_aux_mode mode;
	struct mutex lock;
};

static int bmi270_aux_wait_idle(struct regmap *regmap_parent)
{
	unsigned int status;
	int ret, i;

	for (i = 0; i < 20; i++) {
		ret = regmap_read(regmap_parent, BMI270_REG_STATUS, &status);
		if (ret)
			return ret;

		if (!(status & BMI270_STATUS_AUX_BUSY))
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int bmi270_aux_switch_manual(struct bmi270_aux_bridge *bridge)
{
	struct regmap *regmap_parent = bridge->parent_regmap;
	int ret;

	if (bridge->mode == BMI270_AUX_MODE_MANUAL)
		return 0;

	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_IF_CONF,
			   BMI270_AUX_IF_CONF_8B_MANUAL);
	if (ret)
		return ret;

	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	bridge->mode = BMI270_AUX_MODE_MANUAL;

	return 0;
}

static int bmi270_aux_switch_sync(struct bmi270_aux_bridge *bridge)
{
	struct regmap *regmap_parent = bridge->parent_regmap;
	int ret;

	if (bridge->mode == BMI270_AUX_MODE_SYNC)
		return 0;

	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_RD_ADDR,
			   BMM150_REG_X_L);
	if (ret)
		return ret;

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_IF_CONF,
			   BMI270_AUX_IF_CONF_8B_SYNC);
	if (ret)
		return ret;

	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	/*
	 * The first mapped AUX_DATA sample is produced by the automatic AUX
	 * engine, so wait one 100 Hz AUX period after switching modes.
	 */
	usleep_range(10000, 12000);

	bridge->mode = BMI270_AUX_MODE_SYNC;

	return 0;
}

/*
 * regmap_bus.write callback parameter description:
 * context: struct bmi270_aux_bridge passed to devm_regmap_init().
 * data: write buffer supplied by the caller; data[0] is the downstream BMM150 start register address,
 *       data[1..count-1] contains the data to write sequentially.
 * count: length of the data buffer; at least 2 bytes are required: 1 register-address byte plus 1 data byte.
 */
static int bmi270_regmap_write(
	void *context, const void *data, size_t count)
{
	struct bmi270_aux_bridge *bridge = context;
	struct regmap *regmap_parent = bridge->parent_regmap;
	const u8 *buf = data;
	size_t offset;
	int ret = 0;

	if (count < 2)
		return -EINVAL;

	if (!regmap_parent)
		return -ENODEV;

	mutex_lock(&bridge->lock);

	ret = bmi270_aux_switch_manual(bridge);
	if (ret)
		goto out_unlock;

	for (offset = 1; offset < count; offset++) {
		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		ret = regmap_write(regmap_parent, BMI270_REG_AUX_WR_DATA,
				   buf[offset]);
		if (ret)
			goto out_unlock;

		ret = regmap_write(regmap_parent, BMI270_REG_AUX_WR_ADDR,
				   buf[0] + offset - 1);
		if (ret)
			goto out_unlock;

		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		usleep_range(1000, 2000);
	}

out_unlock:
	mutex_unlock(&bridge->lock);
	return ret;
}

/*
 * regmap_bus.read callback parameter description:
 * context: struct bmi270_aux_bridge passed to devm_regmap_init().
 * reg: buffer containing the downstream BMM150 register address to read.
 * reg_size: length of the reg buffer, usually a 1-byte register address.
 * val: output buffer for the read result.
 * val_size: number of bytes the caller expects to read.
 */
static int bmi270_regmap_read(void *context,
			      const void *reg, size_t reg_size,
			      void *val, size_t val_size)
{
	struct bmi270_aux_bridge *bridge = context;
	struct regmap *regmap_parent = bridge->parent_regmap;
	size_t offset = 0;
	u8 reg_addr;
	u8 *buf = val;
	int ret = 0;

	if (reg_size != 1)
		return -ENOTSUPP;
	if (!val_size)
		return 0;

	if (!regmap_parent)
		return -ENODEV;

	reg_addr = *((const u8 *)reg);

	mutex_lock(&bridge->lock);

	if (reg_addr == BMM150_REG_X_L && val_size == BMM150_DATA_LEN) {
		ret = bmi270_aux_switch_sync(bridge);
		if (ret)
			goto out_unlock;

		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		ret = regmap_bulk_read(regmap_parent, BMI270_REG_AUX_DATA_0,
				       buf, BMM150_DATA_LEN);
		goto out_unlock;
	} else if (reg_addr >= BMM150_REG_X_L &&
		   reg_addr <= BMM150_REG_X_L + BMM150_DATA_LEN - 1) {
		size_t mapped_offset = reg_addr - BMM150_REG_X_L;
		size_t mapped_len = BMM150_DATA_LEN - mapped_offset;

		if (mapped_len > val_size)
			mapped_len = val_size;

		ret = bmi270_aux_switch_sync(bridge);
		if (ret)
			goto out_unlock;

		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		ret = regmap_bulk_read(regmap_parent,
				       BMI270_REG_AUX_DATA_0 + mapped_offset,
				       buf, mapped_len);
		if (ret)
			goto out_unlock;

		if (mapped_len == val_size)
			goto out_unlock;

		offset = mapped_len;
	}

	ret = bmi270_aux_switch_manual(bridge);
	if (ret)
		goto out_unlock;

	while (offset < val_size) {
		size_t chunk = val_size - offset;

		if (chunk > 8)
			chunk = 8;

		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		ret = regmap_write(regmap_parent, BMI270_REG_AUX_RD_ADDR,
				   reg_addr + offset);
		if (ret)
			goto out_unlock;

		ret = bmi270_aux_wait_idle(regmap_parent);
		if (ret)
			goto out_unlock;

		usleep_range(1000, 2000);

		ret = regmap_bulk_read(regmap_parent, BMI270_REG_AUX_DATA_0,
				       buf + offset, chunk);
		if (ret)
			goto out_unlock;

		offset += chunk;
	}

out_unlock:
	mutex_unlock(&bridge->lock);
	return ret;
}

static int bmi270_aux_init(struct bmi270_aux_bridge *bridge)
{
	struct regmap *regmap_parent = bridge->parent_regmap;
	u8 aux_i2c_addr = bridge->aux_i2c_addr;
	int ret;

	ret = regmap_update_bits(regmap_parent, BMI270_REG_IF_CONF,
				 BMI270_IF_CONF_AUX_EN, BMI270_IF_CONF_AUX_EN);
	if (ret)
		return ret;
	usleep_range(1000, 2000);

	ret = regmap_update_bits(regmap_parent, BMI270_REG_PWR_CTRL,
				 BMI270_PWR_CTRL_AUX_EN, BMI270_PWR_CTRL_AUX_EN);
	if (ret)
		return ret;
	usleep_range(1000, 2000);

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_DEV_ID,
			   aux_i2c_addr << 1);
	if (ret)
		return ret;
	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_IF_CONF,
			   BMI270_AUX_IF_CONF_8B_MANUAL);
	if (ret)
		return ret;
	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	bridge->mode = BMI270_AUX_MODE_MANUAL;

	ret = regmap_write(regmap_parent, BMI270_REG_AUX_CONF,
			   BMI270_AUX_CONF_ODR_100HZ);
	if (ret)
		return ret;
	ret = bmi270_aux_wait_idle(regmap_parent);
	if (ret)
		return ret;

	usleep_range(2000, 3000);

	return 0;
}

static const struct regmap_bus regmap_bmi270_bus = {
	.write = bmi270_regmap_write,
	.read = bmi270_regmap_read,
};

static struct regmap *bmc150_bmi270_get_parent_regmap(struct device *dev)
{
	struct device_node *parent_np;
	struct device *parent;
	struct regmap *regmap;

	parent_np = of_parse_phandle(dev->of_node, "bmi270-parent", 0);
	if (!parent_np)
		return dev_get_regmap(dev->parent, NULL);

	parent = bus_find_device_by_of_node(&i2c_bus_type, parent_np);
	of_node_put(parent_np);
	if (!parent)
		return ERR_PTR(-EPROBE_DEFER);

	device_lock(parent);
	if (!device_is_bound(parent)) {
		device_unlock(parent);
		put_device(parent);
		return ERR_PTR(-EPROBE_DEFER);
	}
	device_unlock(parent);

	regmap = dev_get_regmap(parent, NULL);
	if (!regmap)
		regmap = ERR_PTR(-EPROBE_DEFER);

	device_link_add(dev, parent, DL_FLAG_AUTOREMOVE_CONSUMER);
	put_device(parent);

	return regmap;
}

static int bmc150_bmi270_magn_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	const char *name;
	u32 aux_i2c_addr;
	int irq, ret;
	struct bmi270_aux_bridge *bridge;

	bridge = devm_kzalloc(&pdev->dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &aux_i2c_addr);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to read BMM150 I2C address\n");

	bridge->dev = &pdev->dev;
	bridge->aux_i2c_addr = aux_i2c_addr;
	bridge->parent_regmap = bmc150_bmi270_get_parent_regmap(&pdev->dev);
	if (IS_ERR_OR_NULL(bridge->parent_regmap))
		return dev_err_probe(&pdev->dev,
				     PTR_ERR_OR_ZERO(bridge->parent_regmap) ?:
				     -ENODEV,
				     "Failed to get parent regmap\n");

	mutex_init(&bridge->lock);

	ret = bmi270_aux_init(bridge);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to initialize BMI270 auxiliary interface\n");

	regmap = devm_regmap_init(&pdev->dev, &regmap_bmi270_bus,
				  bridge, &bmc150_magn_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	name = device_get_match_data(&pdev->dev);
	if (!name)
		name = "bmm150";

	irq = platform_get_irq_optional(pdev, 0);
	if (irq == -ENXIO)
		irq = 0;

	return bmc150_magn_probe(&pdev->dev, regmap, irq, name);
}

static void bmc150_bmi270_magn_remove(struct platform_device *pdev)
{
	bmc150_magn_remove(&pdev->dev);
}

static const struct platform_device_id bmc150_bmi270_magn_id[] = {
	{ "bmm150_bmi270" },
	{ }
};
MODULE_DEVICE_TABLE(platform, bmc150_bmi270_magn_id);

static const struct of_device_id bmc150_bmi270_magn_of_match[] = {
	{
		.compatible = "bosch,bmm150-bmi270",
		.data = "bmm150",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, bmc150_bmi270_magn_of_match);

static struct platform_driver bmc150_bmi270_magn_driver = {
	.driver = {
		.name = "bmm150_bmi270_magn",
		.of_match_table = bmc150_bmi270_magn_of_match,
		.pm = &bmc150_magn_pm_ops,
	},
	.probe = bmc150_bmi270_magn_probe,
	.remove = bmc150_bmi270_magn_remove,
	.id_table = bmc150_bmi270_magn_id,
};

module_platform_driver(bmc150_bmi270_magn_driver);

MODULE_AUTHOR("dianjixz <dianjixz@m5stack.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMM150 magnetometer platform driver for BMI270 parent");
MODULE_IMPORT_NS("IIO_BMC150_MAGN");
