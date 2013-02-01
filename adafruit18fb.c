/*
 * FB driver for the Adafruit 1.8" LCD display
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * Thanks to Kamal Mostafa and Matt Porter for their work on st7735fb.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "adafruit18fb"
#define WIDTH       128
#define HEIGHT      160
#define BPP         16
#define FPS			10


static int adafruit18fb_init_display(struct fbtft_par *par)
{
	fbtft_write_cmdDef write_cmd = par->fbtftops.write_cmd;
	fbtft_write_dataDef write_data = par->fbtftops.write_data;

	dev_dbg(par->info->device, "adafruit18fb_init_display()\n");

	par->fbtftops.reset(par);

	par->throttle_speed = 2000000;

	// SWRESET - Software reset
	write_cmd(par, 0x01);
	mdelay(150);

	// SLPOUT - Sleep out & booster on
	write_cmd(par, 0x11);
	mdelay(500);

	// FRMCTR1 - frame rate control - normal mode
	write_cmd(par, 0xB1);
	write_data(par, 0x01);	// frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D)
	write_data(par, 0x2C);
	write_data(par, 0x2D);

	// FRMCTR2 - frame rate control - idle mode
	write_cmd(par, 0xB2);
	write_data(par, 0x01);	// frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D)
	write_data(par, 0x2C);
	write_data(par, 0x2D);

	// FRMCTR1 - frame rate control - partial mode
	write_cmd(par, 0xB3);
	write_data(par, 0x01);	// dot inversion mode
	write_data(par, 0x2C);
	write_data(par, 0x2D);
	write_data(par, 0x01);	// line inversion mode
	write_data(par, 0x2C);
	write_data(par, 0x2D);

	// INVCTR - // display inversion control
	write_cmd(par, 0xB4);
	write_data(par, 0x07);	// no inversion

	// PWCTR1 - Power Control
	write_cmd(par, 0xC0);
	write_data(par, 0xA2);
	write_data(par, 0x02);	// -4.6V
	write_data(par, 0x84);	// AUTO mode

	// PWCTR2 - Power Control
	write_cmd(par, 0xC1);
	write_data(par, 0xC5);	// VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD

	// PWCTR3 - Power Control
	write_cmd(par, 0xC2);
	write_data(par, 0x0A);	// Opamp current small
	write_data(par, 0x00);	// Boost frequency

	// PWCTR4 - Power Control
	write_cmd(par, 0xC3);
	write_data(par, 0x8A);	// BCLK/2, Opamp current small & Medium low
	write_data(par, 0x2A);

	// PWCTR5 - Power Control
	write_cmd(par, 0xC4);
	write_data(par, 0x8A);
	write_data(par, 0xEE);

	// VMCTR1 - Power Control
	write_cmd(par, 0xC5);
	write_data(par, 0x0E);

	// INVOFF - Display inversion off
	write_cmd(par, 0x20);

	// MADCTL - Memory data access control
	write_cmd(par, 0x36);
	write_data(par, 0xC8);	// row address/col address, bottom to top refresh

	// COLMOD - Interface pixel format
	write_cmd(par, 0x3A);
	write_data(par, 0x05);

	// GMCTRP1 - Gamma control
	write_cmd(par, 0xE0);
	write_data(par, 0x02);
	write_data(par, 0x1c);
	write_data(par, 0x07);
	write_data(par, 0x12);
	write_data(par, 0x37);
	write_data(par, 0x32);
	write_data(par, 0x29);
	write_data(par, 0x2d);
	write_data(par, 0x29);
	write_data(par, 0x25);
	write_data(par, 0x2b);
	write_data(par, 0x39);
	write_data(par, 0x00);
	write_data(par, 0x01);
	write_data(par, 0x03);
	write_data(par, 0x10);

	// GMCTRN1 - Gamma control
	write_cmd(par, 0xE1);
	write_data(par, 0x03);
	write_data(par, 0x1d);
	write_data(par, 0x07);
	write_data(par, 0x06);
	write_data(par, 0x2e);
	write_data(par, 0x2c);
	write_data(par, 0x29);
	write_data(par, 0x2d);
	write_data(par, 0x2e);
	write_data(par, 0x2e);
	write_data(par, 0x37);
	write_data(par, 0x3f);
	write_data(par, 0x00);
	write_data(par, 0x00);
	write_data(par, 0x02);
	write_data(par, 0x10);

	// DISPON - Display On
	write_cmd(par, 0x29);
	mdelay(100);

	// NORON - Partial off (Normal)
	write_cmd(par, 0x13);
	mdelay(10);

	par->throttle_speed = 0;

	return 0;
}

struct fbtft_display adafruit18fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
};

static int __devinit adafruit18fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	dev_dbg(&spi->dev, "probe()\n");

	info = fbtft_framebuffer_alloc(&adafruit18fb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	par->fbtftops.init_display = adafruit18fb_init_display;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto fbreg_fail;

	if (par->gpio.dc < 0) {
		dev_err(&spi->dev, "Missing info about D/C gpio. Aborting.\n");
		goto fbreg_fail;
	}

	return 0;

fbreg_fail:
	fbtft_framebuffer_release(info);

	return ret;
}

static int __devexit adafruit18fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	dev_dbg(&spi->dev, "remove()\n");

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver adafruit18fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = adafruit18fb_probe,
	.remove = __devexit_p(adafruit18fb_remove),
};

static int __init adafruit18fb_init(void)
{
	pr_debug("\n\n"DRVNAME" - init\n");
	return spi_register_driver(&adafruit18fb_driver);
}

static void __exit adafruit18fb_exit(void)
{
	pr_debug(DRVNAME" - exit\n");
	spi_unregister_driver(&adafruit18fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(adafruit18fb_init);
module_exit(adafruit18fb_exit);

MODULE_DESCRIPTION("FB driver for the Adafruit 1.8 inch LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
