KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
ROOTFSPATH ?=
PWD ?= $(shell pwd)
ARCH ?= $(shell uname -m)


kermel_module := magnetometer bmi270
kermel_module_flage  := CONFIG_BMC150_MAGN=m CONFIG_BMC150_MAGN_BMI270=m CONFIG_BMI270=m CONFIG_BMI270_I2C=m
kermel_ko     := 
kermel_dtbo   := bmi270_bmm150_overlay.dtbo bmi270_overlay.dtbo


define kermel_module_template
$(1): $$(wildcard $(1)/*.c)
	$$(kermel_module_flage) $$(MAKE) -C $$(KERNELDIR) M=$$(PWD)/$(1) ARCH=$$(ARCH) modules
	cp $$(PWD)/$(1)/*.ko ./
endef

$(foreach m,$(kermel_module),$(eval $(call kermel_module_template,$(m))))


%.dtbo: %.dts
	cpp -nostdinc -undef -x assembler-with-cpp $(DTS_CC_FLAGE) $< $<.tmp
	dtc -@ -I dts -O dtb -o $@ $<.tmp


all: $(kermel_module) $(kermel_dtbo)


clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dts).dtbo

install: all
	mkdir -p $(EXTRADIR)
	cp bmi270-init-data.fw $(ROOTFSPATH)/lib/firmware/
	cp ./*.ko $(EXTRADIR) || true
	cp ./*.dtbo /boot/firmware/overlays/  || true
	if [ "$(KERNELDIR)" = "/lib/modules/$(shell uname -r)/build" ]; then depmod -a ; fi


.PHONY: all clean install bmi270 magnetometer