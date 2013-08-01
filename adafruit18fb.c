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
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "adafruit18fb"
#define WIDTH       128
#define HEIGHT      160


static int adafruit18fb_init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

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

	return 0;
}

static int adafruit18fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static void fbtft_adafruit18fb_write_data_command8_bus8_slow(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;
	struct spi_transfer	t = {
			.tx_buf		= par->buf,
			.len		= 1,
			.speed_hz	= 2000000,
		};
	struct spi_message	m;

	fbtft_par_dbg(DEBUG_WRITE_DATA_COMMAND, par, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, dc);

	*par->buf = (u8)val;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(par->spi, &m);

	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}

/* special for Green tab model */
static void adafruit18fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	write_cmd(par, FBTFT_CASET);
	write_data(par, 0x00);
	write_data(par, xs + 2);
	write_data(par, 0x00);
	write_data(par, xe + 2);

	write_cmd(par, FBTFT_RASET);
	write_data(par, 0x00);
	write_data(par, ys + 1);
	write_data(par, 0x00);
	write_data(par, ye + 1);

	write_cmd(par, FBTFT_RAMWR);
}

struct fbtft_display adafruit18fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
};

static int adafruit18fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&adafruit18fb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	par->fbtftops.init_display = adafruit18fb_init_display;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.verify_gpios = adafruit18fb_verify_gpios;
	par->fbtftops.write_data_command = fbtft_adafruit18fb_write_data_command8_bus8_slow;
	if (spi_get_device_id(spi)->driver_data == 1) 
		par->fbtftops.set_addr_win = adafruit18fb_set_addr_win; /* Green tab model */

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int adafruit18fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static const struct spi_device_id adafruit18fb_ids[] = {
	{ "adafruit18fb", 0 },
	{ "adafruit18greenfb", 1 },
	{ },
};

MODULE_DEVICE_TABLE(spi, adafruit18fb_ids);

static struct spi_driver adafruit18fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.id_table = adafruit18fb_ids,
	.probe  = adafruit18fb_probe,
	.remove = adafruit18fb_remove,
};

static int __init adafruit18fb_init(void)
{
	return spi_register_driver(&adafruit18fb_driver);
}

static void __exit adafruit18fb_exit(void)
{
	spi_unregister_driver(&adafruit18fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(adafruit18fb_init);
module_exit(adafruit18fb_exit);

MODULE_DESCRIPTION("FB driver for the Adafruit 1.8 inch LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
