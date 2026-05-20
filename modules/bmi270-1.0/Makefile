KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
PWD := $(shell pwd)
ARCH := $(shell uname -m)
Target_ko := bmi270_core
Target_ko1 := bmi270_i2c
Target_dts := bmi270_overlay
# Module name

obj-m += $(Target_ko).o  $(Target_ko1).o 

all: $(Target_ko).ko $(Target_dts).dtbo

$(Target_ko).ko: $(Target_ko).c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) modules

$(Target_dts).dtbo: $(Target_dts).dts
	dtc -@ -I dts -O dtb -o $@ $<

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dts).dtbo
install: all
	mkdir -p $(EXTRADIR)
	cp bmi270-init-data.fw /lib/firmware/
	cp $(Target_ko).ko $(Target_ko1).ko $(EXTRADIR)
	if [ "$(KERNELDIR)" = "/lib/modules/$(shell uname -r)/build" ]; then depmod -a ; fi
	if [ -f $(Target_dts).dtbo ]; then cp $(Target_dts).dtbo /boot/firmware/overlays/; fi


.PHONY: all clean install