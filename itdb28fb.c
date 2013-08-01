/*
 * FB driver for the ITDB02-2.8 LCD display
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * Based on ili9325.c by Jeroen Domburg
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

#define DRVNAME     "itdb28fb"
#define WIDTH       240
#define HEIGHT      320
#define BPP         16
#define FPS         20
#define GAMMA       "0F 00 7 2 0 0 6 5 4 1\n" \
                    "04 16 2 7 6 3 2 1 7 7"


/* Power supply configuration */
#define ILI9325_BT  6        /* VGL=Vci*4 , VGH=Vci*4 */
#define ILI9325_VC  0b011    /* Vci1=Vci*0.80 */
#define ILI9325_VRH 0b1101   /* VREG1OUT=Vci*1.85 */
#define ILI9325_VDV 0b10010  /* VCOMH amplitude=VREG1OUT*0.98 */
#define ILI9325_VCM 0b001010 /* VCOMH=VREG1OUT*0.735 */

/* 
Verify that this configuration is within the Voltage limits

Display module configuration: Vcc = IOVcc = Vci = 3.3V

 Voltages
----------
Vci                                =   3.3
Vci1           =  Vci * 0.80       =   2.64
DDVDH          =  Vci1 * 2         =   5.28
VCL            = -Vci1             =  -2.64
VREG1OUT       =  Vci * 1.85       =   4.88
VCOMH          =  VREG1OUT * 0.735 =   3.59
VCOM amplitude =  VREG1OUT * 0.98  =   4.79
VGH            =  Vci * 4          =  13.2
VGL            = -Vci * 4          = -13.2

 Limits
--------
Power supplies
1.65 < IOVcc < 3.30   =>  1.65 < 3.3 < 3.30
2.40 < Vcc   < 3.30   =>  2.40 < 3.3 < 3.30
2.50 < Vci   < 3.30   =>  2.50 < 3.3 < 3.30

Source/VCOM power supply voltage
 4.50 < DDVDH < 6.0   =>  4.50 <  5.28 <  6.0
-3.0  < VCL   < -2.0  =>  -3.0 < -2.64 < -2.0
VCI - VCL < 6.0       =>  5.94 < 6.0

Gate driver output voltage
 10  < VGH   < 20     =>   10 <  13.2  < 20
-15  < VGL   < -5     =>  -15 < -13.2  < -5
VGH - VGL < 32        =>   26.4 < 32

VCOM driver output voltage
VCOMH - VCOML < 6.0   =>  4.79 < 6.0
*/

static int itdb28fb_init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	/* Initialization sequence from ILI9325 Application Notes */

	/* ----------- Start Initial Sequence ----------- */
	write_reg(par, 0x00E3, 0x3008); /* Set internal timing */
	write_reg(par, 0x00E7, 0x0012); /* Set internal timing */
	write_reg(par, 0x00EF, 0x1231); /* Set internal timing */
	write_reg(par, 0x0001, 0x0100); /* set SS and SM bit */
	write_reg(par, 0x0002, 0x0700); /* set 1 line inversion */
	switch (par->info->var.rotate) {
	/* AM: GRAM update direction: horiz/vert. I/D: Inc/Dec address counter */
	case 0:
		write_reg(par, 0x03, 0x1030);
		break;
	case 2:
		write_reg(par, 0x03, 0x1000);
		break;
	case 1:
		write_reg(par, 0x03, 0x1028);
		break;
	case 3:
		write_reg(par, 0x03, 0x1018);
		break;
	}
	write_reg(par, 0x0004, 0x0000); /* Resize register */
	write_reg(par, 0x0008, 0x0207); /* set the back porch and front porch */
	write_reg(par, 0x0009, 0x0000); /* set non-display area refresh cycle ISC[3:0] */
	write_reg(par, 0x000A, 0x0000); /* FMARK function */
	write_reg(par, 0x000C, 0x0000); /* RGB interface setting */
	write_reg(par, 0x000D, 0x0000); /* Frame marker Position */
	write_reg(par, 0x000F, 0x0000); /* RGB interface polarity */

	/* ----------- Power On sequence ----------- */
	write_reg(par, 0x0010, 0x0000); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
	write_reg(par, 0x0011, 0x0007); /* DC1[2:0], DC0[2:0], VC[2:0] */
	write_reg(par, 0x0012, 0x0000); /* VREG1OUT voltage */
	write_reg(par, 0x0013, 0x0000); /* VDV[4:0] for VCOM amplitude */
	mdelay(200); /* Dis-charge capacitor power voltage */
	write_reg(par, 0x0010, ( (1 << 12) | ((ILI9325_BT & 0b111) << 8) | (1 << 7) | (0b001 << 4) ) ); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
	write_reg(par, 0x0011, (0x220 | (ILI9325_VC & 0b111)) ); /* DC1[2:0], DC0[2:0], VC[2:0] */
	mdelay(50); /* Delay 50ms */
	write_reg(par, 0x0012, (ILI9325_VRH & 0b1111)); /* Internal reference voltage= Vci; */
	mdelay(50); /* Delay 50ms */
	write_reg(par, 0x0013, ((ILI9325_VDV & 0b11111) << 8) ); /* Set VDV[4:0] for VCOM amplitude */
	write_reg(par, 0x0029, (ILI9325_VCM & 0b111111) ); /* Set VCM[5:0] for VCOMH */
	write_reg(par, 0x002B, 0x000C); /* Set Frame Rate */
	mdelay(50); /* Delay 50ms */
	write_reg(par, 0x0020, 0x0000); /* GRAM horizontal Address */
	write_reg(par, 0x0021, 0x0000); /* GRAM Vertical Address */

	/*------------------ Set GRAM area --------------- */
	write_reg(par, 0x0050, 0x0000); /* Horizontal GRAM Start Address */
	write_reg(par, 0x0051, 0x00EF); /* Horizontal GRAM End Address */
	write_reg(par, 0x0052, 0x0000); /* Vertical GRAM Start Address */
	write_reg(par, 0x0053, 0x013F); /* Vertical GRAM Start Address */
	write_reg(par, 0x0060, 0xA700); /* Gate Scan Line a-Si TFT LCD Single Chip Driver */
	write_reg(par, 0x0061, 0x0001); /* NDL,VLE, REV */
	write_reg(par, 0x006A, 0x0000); /* set scrolling line */

	/*-------------- Partial Display Control --------- */
	write_reg(par, 0x0080, 0x0000);
	write_reg(par, 0x0081, 0x0000);
	write_reg(par, 0x0082, 0x0000);
	write_reg(par, 0x0083, 0x0000);
	write_reg(par, 0x0084, 0x0000);
	write_reg(par, 0x0085, 0x0000);

	/*-------------- Panel Control ------------------- */
	write_reg(par, 0x0090, 0x0010);
	write_reg(par, 0x0092, 0x0600);
	write_reg(par, 0x0007, 0x0133); /* 262K color and display ON */

	return 0;
}

/*
  Gamma string format:
    VRP0 VRP1 RP0 RP1 KP0 KP1 KP2 KP3 KP4 KP5 KP6
    VRN0 VRN1 RN0 RN1 KN0 KN1 KN2 KN3 KN4 KN5 KN6
*/
#define CURVE(num, idx)  curves[num*par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	unsigned long mask[] = { 0b11111, 0b11111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111, \
	                         0b11111, 0b11111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111, 0b111 };
	int i,j;

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	/* apply mask */
	for (i=0;i<2;i++)
		for (j=0;j<10;j++)
			CURVE(i,j) &= mask[i*par->gamma.num_values + j];

	write_reg(par, 0x0030, CURVE(0, 5) << 8 | CURVE(0, 4) );
	write_reg(par, 0x0031, CURVE(0, 7) << 8 | CURVE(0, 6) );
	write_reg(par, 0x0032, CURVE(0, 9) << 8 | CURVE(0, 8) );
	write_reg(par, 0x0035, CURVE(0, 3) << 8 | CURVE(0, 2) );
	write_reg(par, 0x0036, CURVE(0, 1) << 8 | CURVE(0, 0) );

	write_reg(par, 0x0037, CURVE(1, 5) << 8 | CURVE(1, 4) );
	write_reg(par, 0x0038, CURVE(1, 7) << 8 | CURVE(1, 6) );
	write_reg(par, 0x0039, CURVE(1, 9) << 8 | CURVE(1, 8) );
	write_reg(par, 0x003C, CURVE(1, 3) << 8 | CURVE(1, 2) );
	write_reg(par, 0x003D, CURVE(1, 1) << 8 | CURVE(1, 0) );

	return 0;
}
#undef CURVE

static void itdb28fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);
	switch (par->info->var.rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 2:
		write_reg(par, 0x0020, WIDTH - 1 - xs);
		write_reg(par, 0x0021, HEIGHT - 1 - ys);
		break;
	case 1:
		write_reg(par, 0x0020, WIDTH - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 3:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, HEIGHT - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
}

static int itdb28fb_verify_gpios(struct fbtft_par *par)
{
	int i;

	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (par->pdev) {
		if (par->gpio.wr < 0) {
			dev_err(par->info->device, "Missing info about 'wr' gpio. Aborting.\n");
			return -EINVAL;
		}
		for (i=0;i < 8;i++) {
			if (par->gpio.db[i] < 0) {
				dev_err(par->info->device, "Missing info about 'db%02d' gpio. Aborting.\n", i);
				return -EINVAL;
			}
		}
	}

	return 0;
}

struct fbtft_display itdb28fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.gamma_num = 2,
	.gamma_len = 10,
	.gamma = GAMMA,
};

static int itdb28fb_probe_common(struct spi_device *sdev, struct platform_device *pdev)
{
	struct device *dev;
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	if (sdev)
		dev = &sdev->dev;
	else
		dev = &pdev->dev;

	fbtft_init_dbg(dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&itdb28fb_display, dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	if (sdev)
		par->spi = sdev;
	else
		par->pdev = pdev;

	par->fbtftops.init_display = itdb28fb_init_display;
	par->fbtftops.set_gamma = set_gamma;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.write_reg = fbtft_write_reg16_bus8;
	par->fbtftops.set_addr_win = itdb28fb_set_addr_win;
	par->fbtftops.verify_gpios = itdb28fb_verify_gpios;
	if (pdev)
		par->fbtftops.write = fbtft_write_gpio8_wr;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int itdb28fb_remove_common(struct device *dev, struct fb_info *info)
{
	fbtft_init_dbg(dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static int itdb28fb_probe_spi(struct spi_device *spi)
{
	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);
	return itdb28fb_probe_common(spi, NULL);
}

static int itdb28fb_remove_spi(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);
	return itdb28fb_remove_common(&spi->dev, info);
}

static int itdb28fb_probe_pdev(struct platform_device *pdev)
{
	fbtft_init_dbg(&pdev->dev, "%s()\n", __func__);
	return itdb28fb_probe_common(NULL, pdev);
}

static int itdb28fb_remove_pdev(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	fbtft_init_dbg(&pdev->dev, "%s()\n", __func__);
	return itdb28fb_remove_common(&pdev->dev, info);
}

static const struct spi_device_id itdb28fb_platform_ids[] = {
	{ "itdb28spifb", 0 },
	{ },
};

static struct spi_driver itdb28fb_spi_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.id_table = itdb28fb_platform_ids,
	.probe  = itdb28fb_probe_spi,
	.remove = itdb28fb_remove_spi,
};

static struct platform_driver itdb28fb_platform_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = itdb28fb_probe_pdev,
	.remove = itdb28fb_remove_pdev,
};

static int __init itdb28fb_init(void)
{
	int ret;

	ret = spi_register_driver(&itdb28fb_spi_driver);
	if (ret < 0)
		return ret;
	return platform_driver_register(&itdb28fb_platform_driver);
}

static void __exit itdb28fb_exit(void)
{
	spi_unregister_driver(&itdb28fb_spi_driver);
	platform_driver_unregister(&itdb28fb_platform_driver);
}

/* ------------------------------------------------------------------------- */

module_init(itdb28fb_init);
module_exit(itdb28fb_exit);

MODULE_DESCRIPTION("FB driver for the ITDB02-2.8 LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
