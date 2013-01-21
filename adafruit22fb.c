

#define DEBUG

/*
 * linux/drivers/video/fbtft/adafruit22fb.c -- FB driver for the Adafruit 2.2" LCD display
 * Based on st7735fb by Matt Porter
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

//#include <linux/fbtft.h>
#include "fbtft.h"

#define DRVNAME	    "adafruit22fb"
#define WIDTH       176
#define HEIGHT      220
#define BPP         16
#define FPS			10

//#define TXBUFLEN	-1		// use buffer equal to videomemory*txwordsize
#define TXBUFLEN	16*PAGE_SIZE

/* Supported display modules */
#define ST7735_DISPLAY_AF_TFT18		0	/* Adafruit SPI TFT 1.8" */


int adafruit22fb_init_display(struct fbtft_par *par)
{
	fbtft_write_cmdDef write_cmd = par->fbtftops.write_cmd;
	fbtft_write_dataDef write_data = par->fbtftops.write_data;

	dev_dbg(par->info->device, "adafruit22fb_init_display()\n");

	par->fbtftops.reset(par);

	/* BTL221722-276L startup sequence, from datasheet */

	/*	SETEXTCOM: Set extended command set (C1h)
		This command is used to set extended command set access enable.
		Enable: After command (C1h), must write 3 parameters (ffh,83h,40h) by order	*/
	write_cmd(par, 0xC1);
	write_data(par, 0xFF);
	write_data(par, 0x83);
	write_data(par, 0x40);

    /*	Sleep out
		This command turns off sleep mode. 
		In this mode the DC/DC converter is enabled, Internal oscillator is started, 
		and panel scanning is started. 	*/
	write_cmd(par, 0x11);

	mdelay(150);

    /* Undoc'd register? */
	write_cmd(par, 0xCA);
	write_data(par, 0x70);
	write_data(par, 0x00);
	write_data(par, 0xD9);

    /*	SETOSC: Set Internal Oscillator (B0h)
		This command is used to set internal oscillator related settings	*/
	write_cmd(par, 0xB0);
	write_data(par, 0x01);	/* OSC_EN: Enable internal oscillator */
	write_data(par, 0x11);	/* Internal oscillator frequency: 125% x 2.52MHz */

    /* Drive ability setting */
	write_cmd(par, 0xC9);
	write_data(par, 0x90);
	write_data(par, 0x49);
	write_data(par, 0x10);
	write_data(par, 0x28);
	write_data(par, 0x28);
	write_data(par, 0x10);
	write_data(par, 0x00);
	write_data(par, 0x06);

	mdelay(20);

	/*	SETGAMMAP: Set "+" polarity Gamma Curve GC0 Related Setting (C2h) */
	write_cmd(par, 0xC2);
	write_data(par, 0x60);
	write_data(par, 0x71);
	write_data(par, 0x01);
	write_data(par, 0x0E);
	write_data(par, 0x05);
	write_data(par, 0x02);
	write_data(par, 0x09);
	write_data(par, 0x31);
	write_data(par, 0x0A);

	/*	SETGAMMAN: Set "-" polarity Gamma Curve GC0 Related Setting (C3h) */
	write_cmd(par, 0xC3);
	write_data(par, 0x67);
	write_data(par, 0x30);
	write_data(par, 0x61);
	write_data(par, 0x17);
	write_data(par, 0x48);
	write_data(par, 0x07);
	write_data(par, 0x05);
	write_data(par, 0x33);

	mdelay(10);

	/*	SETPWCTR5: Set Power Control 5(B5h)
		This command is used to set VCOM Voltage include VCOM Low and VCOM High Voltage	*/
	write_cmd(par, 0xB5);
	write_data(par, 0x35);	/* VCOMH 0110101 :  3.925 */
	write_data(par, 0x20);	/* VCOML 0100000 : -1.700 */
	write_data(par, 0x45);	/* 45h=69  VCOMH: "VMH" + 5d   VCOML: "VMH" + 5d */

	/*	SETPWCTR4: Set Power Control 4(B4h)
		VRH[4:0]:  Specify the VREG1 voltage adjusting. VREG1 voltage is for gamma voltage setting.
		BT[2:0]: Switch the output factor of step-up circuit 2 for VGH and VGL voltage generation.	*/
	write_cmd(par, 0xB4);
	write_data(par, 0x33);
	write_data(par, 0x25);
	write_data(par, 0x4C);

	mdelay(10);

	/*	Interface Pixel Format (3Ah)
		This command is used to define the format of RGB picture data, 
		which is to be transfer via the system and RGB interface.		*/
	write_cmd(par, 0x3A);
	write_data(par, 0x05);	/* RGB interface: 16 Bit/Pixel	*/

	/*	Display on (29h)
		This command is used to recover from DISPLAY OFF mode. 
		Output from the Frame Memory is enabled.				*/
	write_cmd(par, 0x29);

	mdelay(10);

	return 0;
}


struct fbtft_display adafruit22_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.txbuflen = TXBUFLEN,
	.txwordsize = 2,
	.txdatabitmask = 0x0100,
};


static int __devinit adafruit22fb_probe(struct spi_device *spi)
{
	int chip = spi_get_device_id(spi)->driver_data;
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

//--------------------------------------------------------------------------------

//[297346.179960] hx8340fb spi0.0: spi->dev.platform_data bf1e48d0
//dev_err(&spi->dev, "spi->dev.platform_data %p\n", spi->dev.platform_data);


//spi->max_speed_hz = 32000000;
//spi->mode = SPI_CPHA | SPI_CPOL; // or #define SPI_MODE_3	(SPI_CPOL|SPI_CPHA)
//--------------------------------------------------------------------------------


	dev_dbg(&spi->dev, "adafruit22fb_probe()\n");

	if (chip != ST7735_DISPLAY_AF_TFT18) {
		dev_err(&spi->dev, "only the %s device is supported\n",
			to_spi_driver(spi->dev.driver)->id_table->name);
		return -EINVAL;
	}

	spi->bits_per_word=9;
	ret = spi->master->setup(spi);
	if (ret)
		dev_err(&spi->dev, "spi->master->setup(spi) failed, returned %d\n", ret);

	info = fbtft_framebuffer_alloc(&adafruit22_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	par->fbtftops.init_display = adafruit22fb_init_display;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto fbreg_fail;

	return 0;

fbreg_fail:
	fbtft_framebuffer_release(info);

	return ret;
}

static int __devexit adafruit22fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static const struct spi_device_id adafruit22fb_ids[] = {
	{ DRVNAME, ST7735_DISPLAY_AF_TFT18 },
	{ },
};

MODULE_DEVICE_TABLE(spi, adafruit22fb_ids);

static struct spi_driver adafruit22fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.id_table = adafruit22fb_ids,
	.probe  = adafruit22fb_probe,
	.remove = __devexit_p(adafruit22fb_remove),
};

static int __init adafruit22fb_init(void)
{
	pr_debug("\n\n"DRVNAME" - init\n");
	return spi_register_driver(&adafruit22fb_driver);
}

static void __exit adafruit22fb_exit(void)
{
	pr_debug(DRVNAME" - exit\n");
	spi_unregister_driver(&adafruit22fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(adafruit22fb_init);
module_exit(adafruit22fb_exit);

MODULE_DESCRIPTION("FB driver for the Adafruit 2.2\" LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
