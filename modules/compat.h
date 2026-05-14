/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _M5STACK_COMPAT_H
#define _M5STACK_COMPAT_H

#include <linux/version.h>

/*
 * platform_driver .remove:
 *   < 6.8: .remove_new for void-returning, .remove for int-returning
 *   >= 6.8: .remove_new removed, .remove is void-returning
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
  #define COMPAT_PLATFORM_REMOVE .remove
#else
  #define COMPAT_PLATFORM_REMOVE .remove_new
#endif

/*
 * power_supply_config .of_node -> .fwnode (6.17+)
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
  #include <linux/property.h>
  #define COMPAT_PSY_SET_NODE(cfg, dev) ((cfg)->fwnode = dev_fwnode(dev))
#else
  #define COMPAT_PSY_SET_NODE(cfg, dev) ((cfg)->of_node = (dev)->of_node)
#endif

/*
 * devm_power_supply_register_no_ws removed in 6.17+
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
  #define compat_devm_power_supply_register_no_ws(dev, desc, cfg) \
      devm_power_supply_register(dev, desc, cfg)
#else
  #define compat_devm_power_supply_register_no_ws(dev, desc, cfg) \
      devm_power_supply_register_no_ws(dev, desc, cfg)
#endif

/*
 * struct device_driver .owner removed in 6.18+
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
  #define COMPAT_DRIVER_OWNER .owner = THIS_MODULE,
#else
  #define COMPAT_DRIVER_OWNER
#endif

#endif /* _M5STACK_COMPAT_H */
