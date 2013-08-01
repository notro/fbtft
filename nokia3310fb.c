/*
 * FB driver for the Nokia 5110/3310 LCD display
 *
 * The display is monochrome and the video memory is RGB565.
 * Any pixel value except 0 turns the pixel on.
 *
 * Copyright (C) 2013 Noralf Tronnes
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


#define DRVNAME	    "nokia3310fb"
#define WIDTH       84
#define HEIGHT      48
#define TXBUFLEN    84*6

static unsigned contrast = 0x40;
module_param(contrast, uint, 0);
MODULE_PARM_DESC(contrast, "Vop[6:0] Contrast: 0 - 0x7F (default: 0x40)");

static unsigned tc = 0;
module_param(tc, uint, 0);
MODULE_PARM_DESC(tc, "TC[1:0] Temperature coefficient: 0-3 (default: 0)");

static unsigned bs = 4;
module_param(bs, uint, 0);
MODULE_PARM_DESC(bs, "BS[2:0] Bias voltage level: 0-7 (default: 4)");


static int nokia3310fb_init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* Function set */
	write_reg(par, 0x21); /* 5:1  1
	                         2:0  PD - Powerdown control: chip is active
							 1:0  V  - Entry mode: horizontal addressing
							 0:1  H  - Extended instruction set control: extended
						  */

	/* H=1 Set Vop (contrast) */
	write_reg(par, 0x80 | (contrast & 0x7F)); /*
	                         7:1  1
	                         6-0: Vop[6:0] - Operation voltage
	                      */

	/* H=1 Temperature control */
	write_reg(par, 0x04 | (tc & 0x3)); /* 
	                         2:1  1
	                         1:x  TC1 - Temperature Coefficient: 0x10
							 0:x  TC0
						  */

	/* H=1 Bias system */
	write_reg(par, 0x10 | (bs & 0x7)); /* 
	                         4:1  1
	                         3:0  0
							 2:x  BS2 - Bias System
							 1:x  BS1
							 0:x  BS0
	                      */

	/* Function set */
	write_reg(par, 0x22); /* 5:1  1
	                         2:0  PD - Powerdown control: chip is active
							 1:1  V  - Entry mode: vertical addressing
							 0:0  H  - Extended instruction set control: basic
						  */

	/* H=0 Display control */
	write_reg(par, 0x08 | 4); /* 
	                         3:1  1
	                         2:1  D  - DE: 10=normal mode
							 1:0  0
							 0:0  E
						  */

	return 0;
}

static void nokia3310fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* H=0 Set X address of RAM */
	write_reg(par, 0x80); /* 7:1  1
	                         6-0: X[6:0] - 0x00
	                      */

	/* H=0 Set Y address of RAM */
	write_reg(par, 0x40); /* 7:0  0
	                         6:1  1
	                         2-0: Y[2:0] - 0x0
	                      */
}

static int nokia3310fb_write_vmem(struct fbtft_par *par)
{
	u16 *vmem16 = (u16 *)par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int x, y, i;
	int ret = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s()\n", __func__);

	for (x=0;x<84;x++) {
		for (y=0;y<6;y++) {
			*buf = 0x00;
			for (i=0;i<8;i++) {
				*buf |= (vmem16[(y*8+i)*84+x] ? 1 : 0) << i;
			}
			buf++;
		}
	}

	/* Write data */
	gpio_set_value(par->gpio.dc, 1);
	ret = par->fbtftops.write(par, par->txbuf.buf, 6*84);
	if (ret < 0)
		dev_err(par->info->device, "%s: write failed and returned: %d\n", __func__, ret);

	return ret;
}

static int nokia3310fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}


struct fbtft_display nokia3310fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
};

static int nokia3310fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&nokia3310fb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	par->fbtftops.write_data_command = fbtft_write_data_command8_bus8;
	par->fbtftops.write_vmem = nokia3310fb_write_vmem;
	par->fbtftops.set_addr_win = nokia3310fb_set_addr_win;
	par->fbtftops.init_display = nokia3310fb_init_display;
	par->fbtftops.verify_gpios = nokia3310fb_verify_gpios;
	par->fbtftops.register_backlight = fbtft_register_backlight;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int nokia3310fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	if (info) {
		if (info->bl_dev) {
			/* turn off backlight or else it will fade out */
			info->bl_dev->props.power = FB_BLANK_POWERDOWN;
			info->bl_dev->ops->update_status(info->bl_dev);
		}
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver nokia3310fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = nokia3310fb_probe,
	.remove = nokia3310fb_remove,
};

static int __init nokia3310fb_init(void)
{
	return spi_register_driver(&nokia3310fb_driver);
}

static void __exit nokia3310fb_exit(void)
{
	spi_unregister_driver(&nokia3310fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(nokia3310fb_init);
module_exit(nokia3310fb_exit);

MODULE_DESCRIPTION("FB driver for the Nokia 5110/3310 LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
