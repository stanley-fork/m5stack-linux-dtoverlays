KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
ROOTFSPATH ?=
PWD := $(CURDIR)
ARCH ?= $(shell uname -m)
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra


kermel_module := 
kermel_module_flage  := 
kermel_ko     := st7789v_m5stack.ko pwm_bl_m5stack.ko panel-mipi-dbi-m.ko
kermel_dtbo   := st7789v_m5stack.dtbo

all:  $(kermel_dtbo) kermel_ko_t $(kermel_module) fb_load

fb_load: fb_load.c
	$(CC) $(CFLAGS) -o $@ $<

obj-m += $(patsubst %.ko,%.o,$(kermel_ko))

%.dtbo: %.dts
	cpp -nostdinc -undef -x assembler-with-cpp $(DTS_CC_FLAGE) $< $<.tmp
	dtc -@ -I dts -O dtb -o $@ $<.tmp

kermel_ko_t: $(patsubst %.ko,%.c,$(kermel_ko))
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) modules


clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(kermel_dtbo) *.tmp fb_load

install: all
	mkdir -p $(EXTRADIR)
	cp cardputerzero,st7789v_lcd.bin $(ROOTFSPATH)/lib/firmware/
	cp 99-cardputer-zero-lcd.rules $(ROOTFSPATH)/etc/udev/rules.d/
	cp fb_load.service $(ROOTFSPATH)/etc/systemd/system/
	ln -sfv ../fb_load.service $(ROOTFSPATH)/etc/systemd/system/sysinit.target.wants/fb_load.service
	cp fb_load $(ROOTFSPATH)/usr/sbin/
	cp ./*.ko $(EXTRADIR) || true
	cp ./*.dtbo /boot/firmware/overlays/  || true
	if [ "$(KERNELDIR)" = "/lib/modules/$(shell uname -r)/build" ]; then depmod -a ; fi


.PHONY: all clean install bmi270 magnetometer
