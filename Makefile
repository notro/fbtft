# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
obj-m := fbtft.o

obj-m += adafruit22fb.o
obj-m += sainsmart18fb.o

obj-m += spidevices.o

#obj-$(CONFIG_FB_TFT_SUPPORT)      += fbtft.o
#obj-$(CONFIG_FB_ADAFRUIT22)       += adafruit22fb.o
#obj-$(CONFIG_FB_SAINSMART18)       += sainsmart18fb.o

# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD  := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

help:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) help

endif
