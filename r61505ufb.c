/*
 * FB driver for the r61505u-9A LCD display
 *
 * Copyright (C) 2013 Christian Vogelgsang
 *
 * Based on driver code found here: https://github.com/watterott/r61505u-Adapter
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

//#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "r61505ufb"
#define WIDTH       320
#define HEIGHT      240

#define LCD_DATA		0x72
#define LCD_REGISTER	0x70


#undef write_cmd
#undef write_data

#define write_cmd(par, reg, data) r61505ufb_write_cmd(par, reg, data)

static inline void set_dc(struct fbtft_par *par, int val)
{
	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, val);
}

#define enable_cs(par)	set_dc(par, 0)
#define disable_cs(par)	set_dc(par, 1)

static void r61505ufb_write_cmd(struct fbtft_par *par, uint8_t reg, uint8_t data)
{
	uint8_t buf[2];

	buf[0] = LCD_REGISTER;
	buf[1] = reg;
	enable_cs(par);
	par->fbtftops.write(par, buf, 2);
	disable_cs(par);

	buf[0] = LCD_DATA;
	buf[1] = data;
	enable_cs(par);
	par->fbtftops.write(par, buf, 2);
	disable_cs(par);
}

static int r61505ufb_init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	//driving ability
	write_cmd(par, 0xEA, 0x00);
	write_cmd(par, 0xEB, 0x20);
	write_cmd(par, 0xEC, 0x0C);
	write_cmd(par, 0xED, 0xC4);
	write_cmd(par, 0xE8, 0x40);
	write_cmd(par, 0xE9, 0x38);
	write_cmd(par, 0xF1, 0x01);
	write_cmd(par, 0xF2, 0x10);
	write_cmd(par, 0x27, 0xA3);

	//power voltage
	write_cmd(par, 0x1B, 0x1B);
	write_cmd(par, 0x1A, 0x01);
	write_cmd(par, 0x24, 0x2F);
	write_cmd(par, 0x25, 0x57);

	//VCOM offset
	write_cmd(par, 0x23, 0x8D); //for flicker adjust

	//power on
	write_cmd(par, 0x18, 0x36);
	write_cmd(par, 0x19, 0x01); //start osc
	write_cmd(par, 0x01, 0x00); //wakeup
	write_cmd(par, 0x1F, 0x88);
	mdelay(5);
	write_cmd(par, 0x1F, 0x80);
	mdelay(5);
	write_cmd(par, 0x1F, 0x90);
	mdelay(5);
	write_cmd(par, 0x1F, 0xD0);
	mdelay(5);

	//color selection
	write_cmd(par, 0x17, 0x05); //0x0005=65k, 0x0006=262k

	//panel characteristic
	write_cmd(par, 0x36, 0x00);

	//display on
	write_cmd(par, 0x28, 0x38);
	mdelay(40);
	write_cmd(par, 0x28, 0x3C);

	//orientation
	write_cmd(par, 0x16, 0x68);

	return 0;
}

static unsigned long r61505ufb_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	if (strcasecmp(gpio->name, "led") == 0) {
		par->gpio.led[0] = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

static int r61505ufb_verify_gpios(struct fbtft_par *par)
{
	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static void r61505ufb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	uint8_t xsl, xsh, xel, xeh, ysl, ysh, yel, yeh;

	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	xsl = (uint8_t)(xs & 0xff);
	xsh = (uint8_t)((xs >> 8) & 0xff);
	xel = (uint8_t)(xe & 0xff);
	xeh = (uint8_t)((xe >> 8) & 0xff);

	ysl = (uint8_t)(ys & 0xff);
	ysh = (uint8_t)((ys >> 8) & 0xff);
	yel = (uint8_t)(ye & 0xff);
	yeh = (uint8_t)((ye >> 8) & 0xff);

	dev_dbg(par->info->device, "%s(%02x/%02x, %02x/%02x, %02x/%02x, %02x/%02x)\n", __func__, xsl, xsh, ysl, ysh, xel, xeh, yel, yeh);

	write_cmd(par, 0x03, xsl);
	write_cmd(par, 0x02, xsh);
	write_cmd(par, 0x05, xel);
	write_cmd(par, 0x04, xeh);
	write_cmd(par, 0x07, ysl);
	write_cmd(par, 0x06, ysh);
	write_cmd(par, 0x09, yel);
	write_cmd(par, 0x08, yeh);
}

static int r61505ufb_write_vmem(struct fbtft_par *par)
{
	u16 *vmem16;
	u16 *txbuf16 = NULL;
	size_t remain;
	size_t to_copy;
	int i;
	int ret = 0;
	u16 val;
	unsigned red, green, blue;
	size_t offset, len;
	uint8_t buf[2];
	size_t buf_len;

	offset = par->dirty_lines_start * par->info->fix.line_length;
	len = (par->dirty_lines_end - par->dirty_lines_start + 1) * par->info->fix.line_length;
	remain = len;
	vmem16 = (u16 *)(par->info->screen_base + offset);
	buf_len = par->txbuf.len;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s: offset=%d, len=%d\n", __func__, offset, len);

	// sanity check
	if (!par->txbuf.buf) {
		dev_err(par->info->device, "r61505ufb_write_vmem: txbuf.buf is needed to do conversion\n");
		return -1;
	}

	// commando preset
	buf[0] = LCD_REGISTER;
	buf[1] = 0x22;
	enable_cs(par);
	par->fbtftops.write(par, buf, 2);
	disable_cs(par);

	// begin pixel data
	buf[0] = LCD_DATA;
	enable_cs(par);
	par->fbtftops.write(par, buf, 1);

	while (remain) {

		// buffer needs to start with LCD_DATA so skip first byte
		to_copy = remain > buf_len ? buf_len : remain;
		txbuf16 = (u16 *)(par->txbuf.buf);
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);
		for (i=0;i<to_copy;i+=2) {
			val = *vmem16++;

			// Convert to RGB565
			red   = (val >> par->info->var.red.offset)   & ((1<<par->info->var.red.length) - 1);
			green = (val >> par->info->var.green.offset) & ((1<<par->info->var.green.length) - 1);
			blue  = (val >> par->info->var.blue.offset)  & ((1<<par->info->var.blue.length) - 1);
			val  = (red <<11) | (green <<5) | blue;

#ifdef __LITTLE_ENDIAN
				*txbuf16++ = swab16(val);
#else
				*txbuf16++ = val;
#endif
		}
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}
	disable_cs(par);

	return ret;
}

struct fbtft_display r61505ufb_display = {
	.width = WIDTH,
	.height = HEIGHT,
};

static int r61505ufb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&r61505ufb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	par->fbtftops.init_display = r61505ufb_init_display;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.request_gpios_match = r61505ufb_request_gpios_match;
	par->fbtftops.verify_gpios = r61505ufb_verify_gpios;
	par->fbtftops.set_addr_win = r61505ufb_set_addr_win;
	par->fbtftops.write_vmem = r61505ufb_write_vmem;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int r61505ufb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver r61505ufb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = r61505ufb_probe,
	.remove = r61505ufb_remove,
};

static int __init r61505ufb_init(void)
{
	return spi_register_driver(&r61505ufb_driver);
}

static void __exit r61505ufb_exit(void)
{
	spi_unregister_driver(&r61505ufb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(r61505ufb_init);
module_exit(r61505ufb_exit);

MODULE_DESCRIPTION("FB driver for the r61505u LCD display controller");
MODULE_AUTHOR("Christian Vogelgsang");
MODULE_LICENSE("GPL");
