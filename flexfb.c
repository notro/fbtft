/*
 * Generic FB driver for TFT LCD displays
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
#include <linux/platform_device.h>

#include "fbtft.h"

#define DRVNAME	    "flexfb"


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

static unsigned int width = 0;
module_param(width, uint, 0);
MODULE_PARM_DESC(width, "Display width (required)");

static unsigned int height = 0;
module_param(height, uint, 0);
MODULE_PARM_DESC(height, "Display height (required)");

static int init[512];
static int init_num = 0;
module_param_array(init, int, &init_num, 0);
MODULE_PARM_DESC(init, "Init sequence (required)");

static unsigned int setaddrwin = 0;
module_param(setaddrwin, uint, 0);
MODULE_PARM_DESC(setaddrwin, "Which set_addr_win() implementation to use");

static unsigned int buswidth = 8;
module_param(buswidth, uint, 0);
MODULE_PARM_DESC(buswidth, "Width of databus (default: 8)");

static unsigned int regwidth = 8;
module_param(regwidth, uint, 0);
MODULE_PARM_DESC(regwidth, "Width of controller register (default: 8)");

static bool nobacklight = false;
module_param(nobacklight, bool, 0);
MODULE_PARM_DESC(nobacklight, "Turn off backlight functionality.");

static unsigned rotate = 0;
module_param(rotate, uint, 0);
MODULE_PARM_DESC(rotate, "Rotate display (0=normal, 1=clockwise, 2=upside down, 3=counterclockwise)");

static bool latched = false;
module_param(latched, bool, 0);
MODULE_PARM_DESC(latched, "Use with latched 16-bit databus");


static int flexfb_init_display(struct fbtft_par *par)
{
	char msg[128];
	char str[16];
	int i = 0;
	int j;

	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	/* make sure stop marker exists */
	if (init[init_num - 1] != -3) {
		dev_err(par->info->device, "argument 'init': missing stop marker (-3) at end of init sequence\n");
		return -1;
	}

	while (i < init_num) {
		if (init[i] >= 0) {
			dev_err(par->info->device, "argument 'init': missing delimiter at position %d\n", i);
			return -1;
		}
		if (init[i] == -3) {
			/* done */
			return 0;
		}
		if ( ((i+1) == init_num) || (init[i+1] < 0) ) {
			dev_err(par->info->device, "argument 'init': missing value after delimiter %d at position %d\n", init[i], i);
			return -1;
		}
		switch (init[i]) {
		case -1:
			i++;
			/* make debug message */
			strcpy(msg, "");
			j = i + 1;
			while (init[j] >= 0) {
				sprintf(str, "0x%02X ", init[j]);
				strcat(msg, str);
				j++;
			}
			fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "init: write(0x%02X) %s\n", init[i], msg);
			/* Write */
			par->fbtftops.write_data_command(par, 0, init[i++]);
			while (init[i] >= 0) {
				par->fbtftops.write_data_command(par, 1, init[i++]);
			}
			break;
		case -2:
			i++;
			fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "init: mdelay(%d)\n", init[i]);
			mdelay(init[i++]);
			break;
		default:
			dev_err(par->info->device, "argument 'init': unknown delimiter %d at position %d\n", init[i], i);
			return -1;
		}
	}

	dev_err(par->info->device, "%s: something is wrong. Shouldn't get here.\n", __func__);
	return -1;
}

/* ili9320, ili9325 */
static void flexfb_set_addr_win_1(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);
	switch (rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 2:
		write_reg(par, 0x0020, width - 1 - xs);
		write_reg(par, 0x0021, height - 1 - ys);
		break;
	case 1:
		write_reg(par, 0x0020, height - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 3:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, width - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
}

/* ssd1289 */
static void flexfb_set_addr_win_2(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	switch (rotate) {
	/* R4Eh - Set GDDRAM X address counter */
	/* R4Fh - Set GDDRAM Y address counter */
	case 0:
		write_reg(par, 0x4e, xs);
		write_reg(par, 0x4f, ys);
		break;
	case 2:
		write_reg(par, 0x4e, par->info->var.xres - 1 - xs);
		write_reg(par, 0x4f, par->info->var.yres - 1 - ys);
		break;
	case 1:
		write_reg(par, 0x4e, par->info->var.yres - 1 - ys);
		write_reg(par, 0x4f, xs);
		break;
	case 3:
		write_reg(par, 0x4e, ys);
		write_reg(par, 0x4f, par->info->var.xres - 1 - xs);
		break;
	}

	/* R22h - RAM data write */
	write_reg(par, 0x22, 0);
}

static int flexfb_verify_gpios_dc(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static int flexfb_verify_gpios_db(struct fbtft_par *par)
{
	int i;
	int num_db = buswidth;

	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (par->gpio.wr < 0) {
		dev_err(par->info->device, "Missing info about 'wr' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (latched && (par->gpio.latch < 0)) {
		dev_err(par->info->device, "Missing info about 'latch' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (latched)
		num_db=buswidth/2;
	for (i=0;i < num_db;i++) {
		if (par->gpio.db[i] < 0) {
			dev_err(par->info->device, "Missing info about 'db%02d' gpio. Aborting.\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static struct fbtft_display flex_display = { };

static int flexfb_probe_common(struct spi_device *sdev, struct platform_device *pdev)
{
	struct device *dev;
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	if (sdev)
		dev = &sdev->dev;
	else
		dev = &pdev->dev;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, dev, "%s(%s)\n", __func__, sdev ? "'SPI device'" : "'Platform device'");

	if (width == 0 || height == 0) {
		dev_err(dev, "argument(s) missing: width and height has to be set.\n");
		return -EINVAL;
	}
	flex_display.width = width;
	flex_display.height = height;
	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, dev, "Display size: %dx%d\n", width, height);

	info = fbtft_framebuffer_alloc(&flex_display, dev);
	if (!info)
		return -ENOMEM;

	info->var.rotate = rotate;
	par = info->par;
	if (sdev)
		par->spi = sdev;
	else
		par->pdev = pdev;
	fbtft_debug_init(par);
	par->fbtftops.init_display = flexfb_init_display;

	/* registerwrite functions */
	switch (regwidth) {
	case 8:
		par->fbtftops.write_reg = fbtft_write_reg8_bus8;
		par->fbtftops.write_data_command = fbtft_write_data_command8_bus8;
		break;
	case 16:
		par->fbtftops.write_reg = fbtft_write_reg16_bus8;
		par->fbtftops.write_data_command = fbtft_write_data_command16_bus8;
		break;
	default:
		dev_err(dev, "argument 'regwidth': %d is not supported.\n", regwidth);
		return -EINVAL;
	}

	/* bus functions */
	if (sdev) {
		switch (buswidth) {
		case 8:
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus8;
			if (!par->startbyte)
				par->fbtftops.verify_gpios = flexfb_verify_gpios_dc;
			break;
		case 9:
			if (regwidth == 16) {
				dev_err(dev, "argument 'regwidth': %d is not supported with buswidth=%d and SPI.\n", regwidth, buswidth);
				return -EINVAL;
			}
			par->fbtftops.write_data_command = fbtft_write_data_command8_bus9;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus9;
			sdev->bits_per_word=9;
			ret = sdev->master->setup(sdev);
			if (ret) {
				dev_err(dev, "SPI 9-bit setup failed: %d.\n", ret);
				return ret;
			}
			break;
		default:
			dev_err(dev, "argument 'buswidth': %d is not supported with SPI.\n", buswidth);
			return -EINVAL;
		}
		par->fbtftops.write = fbtft_write_spi;
	} else {
		par->fbtftops.verify_gpios = flexfb_verify_gpios_db;
		switch (buswidth) {
		case 8:
			par->fbtftops.write = fbtft_write_gpio8_wr;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus8;
			break;
		case 16:
			par->fbtftops.write_reg = fbtft_write_reg16_bus16;
			par->fbtftops.write_data_command = fbtft_write_data_command16_bus16;
			if (latched)
				par->fbtftops.write = fbtft_write_gpio16_wr_latched;
			else
				par->fbtftops.write = fbtft_write_gpio16_wr;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus16;
			break;
		default:
			dev_err(dev, "argument 'buswidth': %d is not supported with parallel.\n", buswidth);
			return -EINVAL;
		}
	}

	/* set_addr_win function */
	switch (setaddrwin) {
	case 0:
		/* use default */
		break;
	case 1:
		par->fbtftops.set_addr_win = flexfb_set_addr_win_1;
		break;
	case 2:
		par->fbtftops.set_addr_win = flexfb_set_addr_win_2;
		break;
	default:
		dev_err(dev, "argument 'setaddrwin': unknown value %d.\n", setaddrwin);
		return -EINVAL;
	}

	if (!nobacklight)
		par->fbtftops.register_backlight = fbtft_register_backlight;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int flexfb_remove_common(struct device *dev, struct fb_info *info)
{
	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static int flexfb_probe_spi(struct spi_device *spi)
{
	return flexfb_probe_common(spi, NULL);
}

static int flexfb_remove_spi(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	return flexfb_remove_common(&spi->dev, info);
}

static int flexfb_probe_pdev(struct platform_device *pdev)
{
	return flexfb_probe_common(NULL, pdev);
}

static int flexfb_remove_pdev(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	return flexfb_remove_common(&pdev->dev, info);
}

static struct spi_driver flexfb_spi_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = flexfb_probe_spi,
	.remove = flexfb_remove_spi,
};

static const struct platform_device_id flexfb_platform_ids[] = {
	{ "flexpfb", 0 },
	{ },
};

static struct platform_driver flexfb_platform_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.id_table = flexfb_platform_ids,
	.probe  = flexfb_probe_pdev,
	.remove = flexfb_remove_pdev,
};

static int __init flexfb_init(void)
{
	int ret, ret2;

	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);
	ret = spi_register_driver(&flexfb_spi_driver);
	ret2 = platform_driver_register(&flexfb_platform_driver);
	if (ret < 0)
		return ret;
	return ret2;
}

static void __exit flexfb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);
	spi_unregister_driver(&flexfb_spi_driver);
	platform_driver_unregister(&flexfb_platform_driver);
}

/* ------------------------------------------------------------------------- */

module_init(flexfb_init);
module_exit(flexfb_exit);

MODULE_DESCRIPTION("Generic FB driver for TFT LCD displays");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
