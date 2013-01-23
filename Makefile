# Core module
obj-$(CONFIG_FB_TFT)             += fbtft.o

# drivers
obj-$(CONFIG_FB_ADAFRUIT22)      += adafruit22fb.o
obj-$(CONFIG_FB_SAINSMART18)     += sainsmart18fb.o

# Testing module
obj-$(CONFIG_FB_TFT_SPIDEVICES)  += spidevices.o

