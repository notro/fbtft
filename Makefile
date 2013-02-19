# Core module
obj-$(CONFIG_FB_TFT)             += fbtft.o
fbtft-y                          += fbtft-core.o fbtft-bus.o fbtft-io.o

# drivers
obj-$(CONFIG_FB_ADAFRUIT22)      += adafruit22fb.o
obj-$(CONFIG_FB_ADAFRUIT18)      += adafruit18fb.o
obj-$(CONFIG_FB_SAINSMART18)     += sainsmart18fb.o
obj-$(CONFIG_FB_NOKIA3310)       += nokia3310fb.o

# Testing module
obj-$(CONFIG_FB_TFT_SPIDEVICES)  += spidevices.o

