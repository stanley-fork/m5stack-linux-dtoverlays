KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRADIR ?= /lib/modules/$(shell uname -r)/extra
PWD := $(shell pwd)
ARCH := $(shell uname -m)
Target_ko := bq27xxx_battery
Target1_ko := bq27xxx_battery_i2c
Target_dts := bq27220
# Module name
obj-m += $(Target_ko).o $(Target1_ko).o 

all: $(Target_ko).ko $(Target_dts).dtbo

$(Target_ko).ko: $(Target_ko).c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) modules

$(Target_dts).dtbo: $(Target_dts).dts
	cpp -nostdinc -undef -x assembler-with-cpp $(DTS_CC_FLAGE) $< $<.tmp
	dtc -@ -I dts -O dtb -o $@ $<.tmp

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dts).dtbo *.tmp

install: all
	mkdir -p $(EXTRADIR)
	cp $(Target_ko).ko $(Target1_ko).ko $(EXTRADIR)
	if [ "$(KERNELDIR)" = "/lib/modules/$(shell uname -r)/build" ]; then depmod -a ; fi
	if [ -f $(Target_dts).dtbo ]; then cp $(Target_dts).dtbo /boot/firmware/overlays/; fi



.PHONY: all clean install