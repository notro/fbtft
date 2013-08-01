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
#define GAMMA       "5 1 1 1 1 1 1 1 " \
                    "1 1 1 1 1 1 1 1 " \
                    "1 1 2 2 1 1 1 2 " \
                    "2 2 2 2 3 3 3 3 " \
                    "3 3 3 3 3 3 3 4 " \
                    "4 4 4 4 4 4 4 4 " \
                    "4 4 4 5 5 5 5 5 " \
                    "5 5 5 5 5 5 5"


void ssd1351fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

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
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

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

	write_cmd(par, 0xaf); // Set Sleep Mode Display On

	return 0;
}

/*
	Grayscale Lookup Table
	GS1 - GS63
	The "Gamma curve" contains the relative values between the entries in the Lookup table.

	From datasheet:
	The next 63 data bytes define Gray Scale (GS) Table by 
	setting the gray scale pulse width in unit of DCLK's 
	(ranges from 0d ~ 180d) 

	0 = Setting of GS1 < Setting of GS2 < Setting of GS3..... < Setting of GS62 < Setting of GS63

*/
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	int i, acc = 0;

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	/* verify lookup table */
	for (i=0;i<63;i++) {
		acc += curves[i];
		if (acc > 180) {
			dev_err(par->info->device, "Illegal value(s) in Grayscale Lookup Table. At index=%d, the accumulated value has exceeded 180\n", i);
			return -EINVAL;
		}
		if (curves[i] == 0) {
			dev_err(par->info->device, "Illegal value in Grayscale Lookup Table. Value can't be zero\n");
			return -EINVAL;
		}
	}

	acc = 0;
	write_cmd(par, 0xB8);
	for (i=0;i<63;i++) {
		acc += curves[i];
		write_data(par, acc);
	}

	return 0;
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_par_dbg(DEBUG_BLANK, par, "%s(blank=%s)\n", __func__, on ? "true" : "false");
	if (on)
		write_cmd(par, 0xAE);
	else
		write_cmd(par, 0xAF);
	return 0;
}

static int ssd1351fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

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
	.gamma_num = 1,
	.gamma_len = 63,
	.gamma = GAMMA,
};

static int ssd1351fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&ssd1351fb_display, &spi->dev);

	if (!info) return -ENOMEM;

	par = info->par;
	par->spi = spi;

	par->fbtftops.init_display  = ssd1351fb_init_display;
	par->fbtftops.set_addr_win  = ssd1351fb_set_addr_win;
	par->fbtftops.verify_gpios  = ssd1351fb_verify_gpios;
	par->fbtftops.blank = blank;
	par->fbtftops.set_gamma = set_gamma;

	ret = fbtft_register_framebuffer(info);

	if (ret >= 0) return 0;

	fbtft_framebuffer_release(info);

	return ret;
}

static int ssd1351fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

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
	return spi_register_driver(&ssd1351fb_driver);
}

static void __exit ssd1351fb_exit(void)
{
	spi_unregister_driver(&ssd1351fb_driver);
}

module_init(ssd1351fb_init);
module_exit(ssd1351fb_exit);

MODULE_DESCRIPTION("SSD1351 OLED Driver");
MODULE_AUTHOR("James Davies");
MODULE_LICENSE("GPL");
