KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
PWD ?= $(shell pwd)
ARCH ?= $(shell uname -m)


kermel_module := magnetometer bmi270
kermel_module_flage  := CONFIG_BMC150_MAGN=m CONFIG_BMC150_MAGN_BMI270=m CONFIG_BMI270=m CONFIG_BMI270_I2C=m
kermel_ko     := 
kermel_dtbo   := bmi270_bmm150_overlay.dtbo bmi270_overlay.dtbo


bmi270: $(wildcard bmi270/*.c)
	$(kermel_module_flage) $(MAKE) -C $(KERNELDIR) M=$(PWD)/bmi270 ARCH=$(ARCH) modules
	cp $(PWD)/bmi270/*.ko ./

magnetometer: $(wildcard magnetometer/*.c)
	$(kermel_module_flage) $(MAKE) -C $(KERNELDIR) M=$(PWD)/magnetometer ARCH=$(ARCH) modules
	cp $(PWD)/magnetometer/*.ko ./

%.dtbo: %.dts
	cpp -nostdinc -undef -x assembler-with-cpp $(DTS_CC_FLAGE) $< $<.tmp
	dtc -@ -I dts -O dtb -o $@ $<.tmp


all: bmi270 magnetometer $(kermel_dtbo)


clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dts).dtbo

install: all
	mkdir -p $(EXTRADIR)
	cp bmi270-init-data.fw /lib/firmware/
	cp ./*.ko $(EXTRADIR) || true
	cp ./*.dtbo /boot/firmware/overlays/  || true
	if [ "$(KERNELDIR)" = "/lib/modules/$(shell uname -r)/build" ]; then depmod -a ; fi


.PHONY: all clean install bmi270 magnetometer