/*
 * FB driver for the Nokia 5110/3310 LCD display
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"


#define DRVNAME	    "nokia3310fb"
#define WIDTH       48
#define HEIGHT      84
#define BPP         1
#define FPS			5


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

static bool rotate = 0;
module_param(rotate, bool, 0);
MODULE_PARM_DESC(rotate, "Rotate display");

static int nokia3310fb_init_display(struct fbtft_par *par)
{
	u8 contrast = 0x40;  /* between 0x00 and 0x7F */
	u8 tc = 0;           /* Temperature coefficient, between 0 and 3 */
	u8 bias = 4;         /* Bias, between 0 and 7 */

	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* Function set */
	write_cmd(par, 0x21); /* 5:1  1
	                         2:0  PD - Powerdown control: chip is active
							 1:0  V  - Entry mode: horizontal addressing
							 0:1  H  - Extended instruction set control: extended
						  */

	/* H=1 Set Vop (contrast) */
	write_cmd(par, 0x80 | contrast); /*
	                         7:1  1
	                         6-0: Vop[6:0] - Operation voltage
	                      */

	/* H=1 Temperature control */
	write_cmd(par, 0x04 | tc); /* 
	                         2:1  1
	                         1:x  TC1 - Temperature Coefficient: 0x10
							 0:x  TC0
						  */

	/* H=1 Bias system */
	write_cmd(par, 0x10 | bias); /* 
	                         4:1  1
	                         3:0  0
							 2:x  BS2 - Bias System
							 1:x  BS1
							 0:x  BS0
	                      */

	/* Function set */
	write_cmd(par, 0x22); /* 5:1  1
	                         2:0  PD - Powerdown control: chip is active
							 1:1  V  - Entry mode: vertical addressing
							 0:0  H  - Extended instruction set control: basic
						  */

	/* H=0 Display control */
	write_cmd(par, 0x08 | 4); /* 
	                         3:1  1
	                         2:1  D  - DE: 10=normal mode
							 1:0  0
							 0:0  E
						  */

	return 0;
}

void nokia3310fb_update_display(struct fbtft_par *par)
{
	u8 *vmem8 = vmem8 = par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int i;
	int ret = 0;

	fbtft_dev_dbg(DEBUG_UPDATE_DISPLAY, par->info->device, "%s()\n", __func__);

	if (par->info->var.xres == WIDTH) {
		/* rearrange */
		for (i=0; i<84; i++) {
			buf[i*6+0] = vmem8[i*6+5];
			buf[i*6+1] = vmem8[i*6+4];
			buf[i*6+2] = vmem8[i*6+3];
			buf[i*6+3] = vmem8[i*6+2];
			buf[i*6+4] = vmem8[i*6+1];
			buf[i*6+5] = vmem8[i*6+0];
		}
	} else {
		/* rearrange and rotate */
		dev_err(par->info->device, "%s: rotation not supported yet\n", __func__);
		return;
	}

	/* H=0 Set X address of RAM */
	write_cmd(par, 0x80); /* 7:1  1
	                         6-0: X[6:0] - 0x00
	                      */

	/* H=0 Set Y address of RAM */
	write_cmd(par, 0x40); /* 7:0  0
	                         6:1  1
	                         2-0: Y[2:0] - 0x0
	                      */

	/* Write data */
	gpio_set_value(par->gpio.dc, 1);
	ret = par->fbtftops.write(par, buf, par->info->fix.smem_len);
	if (ret < 0)
		dev_err(par->info->device, "%s: write failed and returned: %d\n", __func__, ret);
}

static unsigned long nokia3310fb_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	fbtft_dev_dbg(DEBUG_REQUEST_GPIOS_MATCH, par->info->device, "%s('%s')\n", __func__, gpio->name);

	if (strcasecmp(gpio->name, "led") == 0) {
		par->gpio.led[0] = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

static int nokia3310fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

struct fbtft_display nokia3310fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.txbuflen = -1,
};

static int __devinit nokia3310fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	if (rotate) {
		nokia3310fb_display.width = HEIGHT;
		nokia3310fb_display.height = WIDTH;
	} else {
		nokia3310fb_display.width = WIDTH;
		nokia3310fb_display.height = HEIGHT;
	}

	info = fbtft_framebuffer_alloc(&nokia3310fb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	info->fix.visual = FB_VISUAL_MONO10;
	info->var.red.offset =     0;
	info->var.red.length =     0;
	info->var.green.offset =   0;
	info->var.green.length =   0;
	info->var.blue.offset =    0;
	info->var.blue.length =    0;

	par = info->par;
	par->spi = spi;
	fbtft_debug_init(par);
	par->fbtftops.write_data_command = fbtft_write_data_command8_bus8;
	par->fbtftops.request_gpios_match = nokia3310fb_request_gpios_match;
	par->fbtftops.verify_gpios = nokia3310fb_verify_gpios;
	par->fbtftops.init_display = nokia3310fb_init_display;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.update_display = nokia3310fb_update_display;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int __devexit nokia3310fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

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
	.remove = __devexit_p(nokia3310fb_remove),
};

static int __init nokia3310fb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);
	return spi_register_driver(&nokia3310fb_driver);
}

static void __exit nokia3310fb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);
	spi_unregister_driver(&nokia3310fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(nokia3310fb_init);
module_exit(nokia3310fb_exit);

MODULE_DESCRIPTION("FB driver for the Nokia 5110/3310 LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
