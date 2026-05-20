KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
ARCH := $(shell uname -m)
Target_ko := m5ioe1
Target_dts := m5ioe1-overlay
# Module name
obj-m += $(Target_ko).o

all: $(Target_ko).ko $(Target_dts).dtbo

$(Target_ko).ko: $(Target_ko).c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) modules

$(Target_dts).dtbo: $(Target_dts).dts
	dtc -@ -I dts -O dtb -o $@ $<

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f $(Target_dts).dtbo
install: all
	mkdir -p /lib/modules/$(shell uname -r)/extra
	cp $(Target_ko).ko /lib/modules/$(shell uname -r)/extra
	depmod -a
	if [ -f $(Target_dts).dtbo ]; then cp $(Target_dts).dtbo /boot/firmware/overlays/; fi


.PHONY: all clean install