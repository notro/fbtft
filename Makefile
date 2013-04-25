# Core module
obj-$(CONFIG_FB_TFT)             += fbtft.o
fbtft-y                          += fbtft-core.o fbtft-bus.o fbtft-io.o

# drivers
obj-$(CONFIG_FB_FLEX)            += flexfb.o
obj-$(CONFIG_FB_ADAFRUIT22)      += adafruit22fb.o
obj-$(CONFIG_FB_ADAFRUIT18)      += adafruit18fb.o
obj-$(CONFIG_FB_SAINSMART18)     += sainsmart18fb.o
obj-$(CONFIG_FB_NOKIA3310)       += nokia3310fb.o
obj-$(CONFIG_FB_R61505U)         += r61505ufb.o
obj-$(CONFIG_FB_ILI9341)         += ili9341fb.o
obj-$(CONFIG_FB_ITDB28)          += itdb28fb.o

# Device module
obj-$(CONFIG_FB_TFT_FBTFT_DEVICE) += fbtft_device.o

# Test
obj-$(CONFIG_TOUCHSCREEN_ADS7846_TEST) += ads7846_test.o
