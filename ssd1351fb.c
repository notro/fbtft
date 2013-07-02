#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME     "ssd1351fb"
#define WIDTH       128
#define HEIGHT      128

/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;


void ssd1351fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	write_cmd(par, 0x15);
	write_data(par, xs);
	write_data(par, xe);

	write_cmd(par, 0x75);
	write_data(par, ys);
	write_data(par, ye);

	write_cmd(par, 0x5c);   
}

static int ssd1351fb_init_display(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	// Reset the device.
	par->fbtftops.reset(par);

	// Write the init sequence.
	write_cmd(par, 0xfd); // Command Lock
	write_data(par, 0x12);

	write_cmd(par, 0xfd); // Command Lock
	write_data(par, 0xb1);

	write_cmd(par, 0xae); // Display Off

	write_cmd(par, 0xb3); // Front Clock Div
	write_data(par, 0xf1);

	write_cmd(par, 0xca); // Set Mux Ratio
	write_data(par, 0x7f);

	write_cmd(par, 0xa0); // Set Colour Depth
	write_data(par, 0x74); // 0xb4

	write_cmd(par, 0x15); // Set Column Address
	write_data(par, 0x00);
	write_data(par, 0x7f);

	write_cmd(par, 0x75); // Set Row Address
	write_data(par, 0x00);
	write_data(par, 0x7f);

	write_cmd(par, 0xa1); // Set Display Start Line
	write_data(par, 0x00);

	write_cmd(par, 0xa2); // Set Display Offset
	write_data(par, 0x00);

	write_cmd(par, 0xb5); // Set GPIO
	write_data(par, 0x00);

	write_cmd(par, 0xab); // Set Function Selection
	write_data(par, 0x01);

	write_cmd(par, 0xb1); // Set Phase Length
	write_data(par, 0x32);

	write_cmd(par, 0xb4); // Set Segment Low Voltage
	write_data(par, 0xa0);
	write_data(par, 0xb5);
	write_data(par, 0x55);

	write_cmd(par, 0xbb); // Set Precharge Voltage
	write_data(par, 0x17);

	write_cmd(par, 0xbe); // Set VComH Voltage
	write_data(par, 0x05);

	write_cmd(par, 0xc1); // Set Contrast
	write_data(par, 0xc8);
	write_data(par, 0x80);
	write_data(par, 0xc8);

	write_cmd(par, 0xc7); // Set Master Contrast
	write_data(par, 0x0f);

	write_cmd(par, 0xb6); // Set Second Precharge Period
	write_data(par, 0x01);

	write_cmd(par, 0xa6); // Set Display Mode Reset

	write_cmd(par, 0xb8); // Set CMD Grayscale Lookup
	write_data(par, 0x05);
	write_data(par, 0x06);
	write_data(par, 0x07);
	write_data(par, 0x08);
	write_data(par, 0x09);
	write_data(par, 0x0a);
	write_data(par, 0x0b);
	write_data(par, 0x0c);
	write_data(par, 0x0D);
	write_data(par, 0x0E);
	write_data(par, 0x0F);
	write_data(par, 0x10);
	write_data(par, 0x11);
	write_data(par, 0x12);
	write_data(par, 0x13);
	write_data(par, 0x14);
	write_data(par, 0x15);
	write_data(par, 0x16);
	write_data(par, 0x18);
	write_data(par, 0x1a);
	write_data(par, 0x1b);
	write_data(par, 0x1C);
	write_data(par, 0x1D);
	write_data(par, 0x1F);
	write_data(par, 0x21);
	write_data(par, 0x23);
	write_data(par, 0x25);
	write_data(par, 0x27);
	write_data(par, 0x2A);
	write_data(par, 0x2D);
	write_data(par, 0x30);
	write_data(par, 0x33);
	write_data(par, 0x36);
	write_data(par, 0x39);
	write_data(par, 0x3C);
	write_data(par, 0x3F);
	write_data(par, 0x42);
	write_data(par, 0x45);
	write_data(par, 0x48);
	write_data(par, 0x4C);
	write_data(par, 0x50);
	write_data(par, 0x54);
	write_data(par, 0x58);
	write_data(par, 0x5C);
	write_data(par, 0x60);
	write_data(par, 0x64);
	write_data(par, 0x68);
	write_data(par, 0x6C);
	write_data(par, 0x70);
	write_data(par, 0x74);
	write_data(par, 0x78);
	write_data(par, 0x7D);
	write_data(par, 0x82);
	write_data(par, 0x87);
	write_data(par, 0x8C);
	write_data(par, 0x91);
	write_data(par, 0x96);
	write_data(par, 0x9B);
	write_data(par, 0xA0);
	write_data(par, 0xA5);
	write_data(par, 0xAA);
	write_data(par, 0xAF);
	write_data(par, 0xB4);

	write_cmd(par, 0xaf); // Set Sleep Mode Display On

	return 0;
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_dev_dbg(DEBUG_BLANK, par->info->device, "%s(blank=%s)\n", __func__, on ? "true" : "false");
	if (on)
		write_cmd(par, 0xAE);
	else
		write_cmd(par, 0xAF);
	return 0;
}

static int ssd1351fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.dc < 0)
	{
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

struct fbtft_display ssd1351fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
};

static int ssd1351fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&ssd1351fb_display, &spi->dev);

	if (!info) return -ENOMEM;

	par = info->par;
	par->spi = spi;

	fbtft_debug_init(par);

	par->fbtftops.init_display  = ssd1351fb_init_display;
	par->fbtftops.set_addr_win  = ssd1351fb_set_addr_win;
	par->fbtftops.verify_gpios  = ssd1351fb_verify_gpios;
	par->fbtftops.blank = blank;

	ret = fbtft_register_framebuffer(info);

	if (ret >= 0) return 0;

	fbtft_framebuffer_release(info);

	return ret;
}

static int ssd1351fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	if (info)
	{
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver ssd1351fb_driver =
{
	.driver =
	{
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = ssd1351fb_probe,
	.remove = ssd1351fb_remove,
};

static int __init ssd1351fb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);

	return spi_register_driver(&ssd1351fb_driver);
}

static void __exit ssd1351fb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);

	spi_unregister_driver(&ssd1351fb_driver);
}

module_init(ssd1351fb_init);
module_exit(ssd1351fb_exit);

MODULE_DESCRIPTION("SSD1351 OLED Driver");
MODULE_AUTHOR("James Davies");
MODULE_LICENSE("GPL");
