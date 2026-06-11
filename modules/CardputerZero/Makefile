KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
ROOTFSPATH ?=
PWD := $(shell pwd)
ARCH := $(shell uname -m)

ifndef CONFIG_CARDPUTERO_V0_3
CONFIG_CARDPUTERO_V0_5=y
endif


Target_ko := powerfail_suo

# 多个 dts 目标，空格分隔，不要写 .dts 后缀
Target_dts := cardputerzero-overlay spk-gpio24-high-overlay spi0-spidev2-gpio22-overlay camera-gpio16-high-overlay camera-py12-high-overlay

Target_dtbo := $(addsuffix .dtbo,$(Target_dts))

ifeq ($(CONFIG_CARDPUTERO_V0_5),y)
DTS_CC_FLAGE := -DCONFIG_CARDPUTERO_V0_5=1
export CONFIG_CARDPUTERO_V0_5
export DTS_CC_FLAGE
endif


# Module name
ifneq ($(strip $(Target_ko)),)
obj-m += $(Target_ko).o
endif
ifneq ($(strip $(Target_ko)),)
all: $(Target_dtbo) $(Target_ko).ko
else
all: $(Target_dtbo)
endif

ifneq ($(strip $(Target_ko)),)
$(Target_ko).ko: $(Target_ko).c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) modules
endif

%.dtbo: %.dts
	cpp -nostdinc -undef -x assembler-with-cpp $(DTS_CC_FLAGE) $< $<.tmp
	dtc -@ -I dts -O dtb -o $@ $<.tmp

install_dts: $(Target_dtbo)
	cp $(Target_dtbo) /boot/firmware/overlays/

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dtbo) *.tmp

install: all install_dts
	mkdir -p $(EXTRADIR)
	cp ./*.ko $(EXTRADIR) || true
	if [ -d '../bq27220-1.0' ] ; then $(MAKE) -C ../bq27220-1.0 install ; fi
	if [ -d '../es8389-1.0' ] ; then $(MAKE) -C ../es8389-1.0 install ; fi
	if [ -d '../m5ioe1-1.0' ] ; then $(MAKE) -C ../m5ioe1-1.0 install ; fi
	if [ -d '../st7789v-1.0' ] ; then $(MAKE) -C ../st7789v-1.0 install ; fi
	if [ -d '../tca8418-1.0' ] ; then $(MAKE) -C ../tca8418-1.0 install ; fi
ifeq ($(CONFIG_CARDPUTERO_V0_5),y)
	if [ -d '../imu-1.0' ] ; 		then cd ../imu-1.0 ; $(MAKE)  install ; fi
# 	if [ -d '../lsm6ds3tr-1.0' ] ; then $(MAKE) -C ../lsm6ds3tr-1.0 install ; fi
endif
	echo "ok"

config_setup:
	[ -f "/tmp/rpi-config.py" ] || curl -fsSL -o "/tmp/rpi-config.py" -L "https://gist.githubusercontent.com/dianjixz/16b2fabbc4a2a573a7c10f212d87f282/raw/rpi-config.py"
	python3 /tmp/rpi-config.py --no-backup set camera_auto_detect 0
	python3 /tmp/rpi-config.py --no-backup add dtoverlay imx219
	python3 /tmp/rpi-config.py --no-backup set dtparam=i2c_arm on
	python3 /tmp/rpi-config.py --no-backup set dtparam=i2s on
	python3 /tmp/rpi-config.py --no-backup set dtparam=spi on
	python3 /tmp/rpi-config.py --no-backup set dtparam=audio off
	python3 /tmp/rpi-config.py --no-backup section-set all enable_uart 1
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay cardputerzero-overlay
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay camera-gpio16-high-overlay
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay bq27220
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay gpio-ir,gpio_pin=13,gpio_pull=up
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay gpio-ir-tx,gpio_pin=12
	python3 /tmp/rpi-config.py --no-backup section-del all dtoverlay pwm-ir-tx,gpio_pin=12,func=4
ifeq ($(CONFIG_CARDPUTERO_V0_5),y)
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay bmi270_bmm150_overlay
	python3 /tmp/rpi-config.py --no-backup section-del all dtoverlay spk-gpio24-high-overlay
	python3 /tmp/rpi-config.py --no-backup section-del all dtoverlay lsm6ds3tr-overlay
else
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay spk-gpio24-high-overlay
	python3 /tmp/rpi-config.py --no-backup section-add all dtoverlay bmi270_overlay
endif

.PHONY: all clean install install_dts config_setup