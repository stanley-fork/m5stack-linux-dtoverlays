# Linux Kernel API Compatibility Notes

This document tracks breaking kernel API changes relevant to the CardputerZero driver modules, and how they are handled via `modules/compat.h`.

## Breaking Changes Summary

| Change | Kernel | Old API | New API | Affected Modules |
|--------|--------|---------|---------|-----------------|
| `platform_driver .remove_new` -> `.remove` | 6.8 | `.remove_new = fn` (void) | `.remove = fn` (void) | fbtft.h, pwm_bl_m5stack.c |
| `i2c_driver .probe_new` -> `.probe` | 6.6 | `.probe_new = fn` (single-arg) | `.probe = fn` (single-arg) | All I2C drivers (already migrated) |
| `class_create` arg count | 6.4 | `class_create(THIS_MODULE, name)` | `class_create(name)` | aw882xx (not built for CZ) |
| `pwm_apply_state` -> `pwm_apply_might_sleep` | 6.7 | `pwm_apply_state(pwm, &state)` | `pwm_apply_might_sleep(pwm, &state)` | pwm_bl_m5stack.c (already migrated) |
| `power_supply_config .of_node` -> `.fwnode` | 6.17 | `.of_node = dev->of_node` | `.fwnode = dev_fwnode(dev)` | bq27xxx_battery.c |
| `devm_power_supply_register_no_ws` removed | 6.17 | `devm_power_supply_register_no_ws()` | `devm_power_supply_register()` | bq27xxx_battery.c |
| `struct device_driver .owner` removed | 6.18 | `.owner = THIS_MODULE` | Omit (set automatically) | fbtft.h platform_driver |
| Legacy `gpio_*` API removed | 6.12+ | `gpio_is_valid()`, `devm_gpio_request_one()` | `gpiod_*` consumer API | aw882xx (not built for CZ) |

## compat.h Macros

| Macro | Usage |
|-------|-------|
| `COMPAT_PLATFORM_REMOVE` | Use instead of `.remove` or `.remove_new` in `struct platform_driver` |
| `COMPAT_PSY_SET_NODE(cfg, dev)` | Sets device node on `struct power_supply_config` |
| `compat_devm_power_supply_register_no_ws(dev, desc, cfg)` | Calls appropriate power supply register function |
| `COMPAT_DRIVER_OWNER` | Expands to `.owner = THIS_MODULE,` on < 6.18, empty on >= 6.18 |

## Modules Built for CardputerZero (pi-gen)

- `st7789v-1.0/` - LCD display (ST7789V via fbtft) + PWM backlight
- `tca8418-1.0/` - Keyboard controller
- `es8389-1.0/` - Audio codec
- `bq27220-1.0/` - Battery fuel gauge
- `py32ioexp-1.0/` - PY32 I/O expander (pinctrl, GPIO, PWM, ADC)

## How to Add Compatibility for Future Breaks

1. Add a version-guarded macro to `modules/compat.h`
2. `#include "../compat.h"` in the affected source file
3. Replace the breaking API call with the compat macro
4. Test builds against both old and new kernel headers
