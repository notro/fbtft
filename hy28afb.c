/*
 * FB driver for the HY28A SPI LCD display
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

#define DRVNAME	    "hy28afb"
#define WIDTH       240
#define HEIGHT      320


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;


static unsigned read_devicecode(struct fbtft_par *par)
{
	int ret;
	u8 rxbuf[8] = {0, };

	write_reg(par, 0x0000);
	ret = par->fbtftops.read(par, rxbuf, 4);
	return (rxbuf[2] << 8) | rxbuf[3];
}

static int hy28afb_init_display(struct fbtft_par *par)
{
	unsigned devcode;
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	devcode = read_devicecode(par);
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "Device code: 0x%04X\n", devcode);
	if ((devcode != 0x0000) && (devcode != 0x9320))
		dev_warn(par->info->device, "Unrecognized Device code: 0x%04X (expected 0x9320)\n", devcode);

	/* Initialization sequence from ILI9320 Application Notes */

	/* *********** Start Initial Sequence ********* */ 
	write_reg(par, 0x00E5, 0x8000); // Set the Vcore voltage and this setting is must. 
	write_reg(par, 0x0000, 0x0001); // Start internal OSC. 
	write_reg(par, 0x0001, 0x0100); // set SS and SM bit 
	write_reg(par, 0x0002, 0x0700); // set 1 line inversion 
    switch (par->info->var.rotate) {  // set GRAM write direction and BGR.
	case 0:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x30);
		break;
	case 1:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x28);
		break;
	case 2:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x00);
		break;
	case 3:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x18);
		break;
	}
	write_reg(par, 0x0004, 0x0000); // Resize register 
	write_reg(par, 0x0008, 0x0202); // set the back porch and front porch 
	write_reg(par, 0x0009, 0x0000); // set non-display area refresh cycle ISC[3:0] 
	write_reg(par, 0x000A, 0x0000); // FMARK function 
	write_reg(par, 0x000C, 0x0000); // RGB interface setting 
	write_reg(par, 0x000D, 0x0000); // Frame marker Position 
	write_reg(par, 0x000F, 0x0000); // RGB interface polarity 
	
	/* ***********Power On sequence *************** */
	write_reg(par, 0x0010, 0x0000); // SAP, BT[3:0], AP, DSTB, SLP, STB 
	write_reg(par, 0x0011, 0x0007); // DC1[2:0], DC0[2:0], VC[2:0] 
	write_reg(par, 0x0012, 0x0000); // VREG1OUT voltage 
	write_reg(par, 0x0013, 0x0000); // VDV[4:0] for VCOM amplitude 
	mdelay(200); // Dis-charge capacitor power voltage 
	write_reg(par, 0x0010, 0x17B0); // SAP, BT[3:0], AP, DSTB, SLP, STB 
	write_reg(par, 0x0011, 0x0031); // R11h=0x0031 at VCI=3.3V DC1[2:0], DC0[2:0], VC[2:0] 
	mdelay(50); // Delay 50ms 
	write_reg(par, 0x0012, 0x0138); // R12h=0x0138 at VCI=3.3V VREG1OUT voltage 
	mdelay(50); // Delay 50ms 
	write_reg(par, 0x0013, 0x1800); // R13h=0x1800 at VCI=3.3V VDV[4:0] for VCOM amplitude 
	write_reg(par, 0x0029, 0x0008); // R29h=0x0008 at VCI=3.3V VCM[4:0] for VCOMH 
	mdelay(50); 
	write_reg(par, 0x0020, 0x0000); // GRAM horizontal Address 
	write_reg(par, 0x0021, 0x0000); // GRAM Vertical Address 
	
	/* ----------- Adjust the Gamma Curve ---------- */
	write_reg(par, 0x0030, 0x0000); 
	write_reg(par, 0x0031, 0x0505); 
	write_reg(par, 0x0032, 0x0004); 
	write_reg(par, 0x0035, 0x0006); 
	write_reg(par, 0x0036, 0x0707); 
	write_reg(par, 0x0037, 0x0105); 
	write_reg(par, 0x0038, 0x0002); 
	write_reg(par, 0x0039, 0x0707); 
	write_reg(par, 0x003C, 0x0704); 
	write_reg(par, 0x003D, 0x0807); 
	
	/* ------------------ Set GRAM area --------------- */
	write_reg(par, 0x0050, 0x0000); // Horizontal GRAM Start Address 
	write_reg(par, 0x0051, 0x00EF); // Horizontal GRAM End Address 
	write_reg(par, 0x0052, 0x0000); // Vertical GRAM Start Address 
	write_reg(par, 0x0053, 0x013F); // Vertical GRAM Start Address 
	write_reg(par, 0x0060, 0x2700); // Gate Scan Line 
	write_reg(par, 0x0061, 0x0001); // NDL,VLE, REV 
	write_reg(par, 0x006A, 0x0000); // set scrolling line 
	
	/* -------------- Partial Display Control --------- */
	write_reg(par, 0x0080, 0x0000); 
	write_reg(par, 0x0081, 0x0000); 
	write_reg(par, 0x0082, 0x0000); 
	write_reg(par, 0x0083, 0x0000); 
	write_reg(par, 0x0084, 0x0000); 
	write_reg(par, 0x0085, 0x0000); 
	
	/* -------------- Panel Control ------------------- */
	write_reg(par, 0x0090, 0x0010); 
	write_reg(par, 0x0092, 0x0000); 
	write_reg(par, 0x0093, 0x0003); 
	write_reg(par, 0x0095, 0x0110); 
	write_reg(par, 0x0097, 0x0000); 
	write_reg(par, 0x0098, 0x0000); 
	write_reg(par, 0x0007, 0x0173); // 262K color and display ON 

	return 0;
}

static int xxhy28afb_init_display(struct fbtft_par *par)
{
	unsigned devcode;
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	devcode = read_devicecode(par);
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "Device code: 0x%04X\n", devcode);

    write_reg(par, 0x00,0x0000);
    write_reg(par, 0x01,0x0100); /* Driver Output Contral */
    write_reg(par, 0x02,0x0700); /* LCD Driver Waveform Contral */

	/* Entry mode R03h */
    switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x30);
		break;
	case 1:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x28);
		break;
	case 2:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x00);
		break;
	case 3:
		write_reg(par, 0x3, (!par->bgr << 12) | 0x18);
		break;
	}

    write_reg(par, 0x04,0x0000); /* Scalling Contral */
    write_reg(par, 0x08,0x0202); /* Display Contral 2 */
    write_reg(par, 0x09,0x0000); /* Display Contral 3 */
    write_reg(par, 0x0a,0x0000); /* Frame Cycle Contal */
    write_reg(par, 0x0c,(1<<0)); /* Extern Display Interface Contral 1 */
    write_reg(par, 0x0d,0x0000); /* Frame Maker Position */
    write_reg(par, 0x0f,0x0000); /* Extern Display Interface Contral 2 */
    mdelay(50);
    write_reg(par, 0x07,0x0101); /* Display Contral */
    mdelay(50);
    write_reg(par, 0x10,(1<<12)|(0<<8)|(1<<7)|(1<<6)|(0<<4)); /* Power Control 1 */
    write_reg(par, 0x11,0x0007);                              /* Power Control 2 */
    write_reg(par, 0x12,(1<<8)|(1<<4)|(0<<0));                /* Power Control 3 */
    write_reg(par, 0x13,0x0b00);                              /* Power Control 4 */
    write_reg(par, 0x29,0x0000);                              /* Power Control 7 */
    write_reg(par, 0x2b,(1<<14)|(1<<4));

    write_reg(par, 0x50,0);       /* Set X Start */
    write_reg(par, 0x51,239);     /* Set X End */
    write_reg(par, 0x52,0);       /* Set Y Start */
    write_reg(par, 0x53,319);     /* Set Y End */
    mdelay(50);

    write_reg(par, 0x60,0x2700); /* Driver Output Control */
    write_reg(par, 0x61,0x0001); /* Driver Output Control */ 
    write_reg(par, 0x6a,0x0000); /* Vertical Scroll Control */

    write_reg(par, 0x80,0x0000); /* Display Position? Partial Display 1 */
    write_reg(par, 0x81,0x0000); /* RAM Address Start? Partial Display 1 */
    write_reg(par, 0x82,0x0000); /* RAM Address End-Partial Display 1 */
    write_reg(par, 0x83,0x0000); /* Display Position? Partial Display 2 */
    write_reg(par, 0x84,0x0000); /* RAM Address Start? Partial Display 2 */
    write_reg(par, 0x85,0x0000); /* RAM Address End? Partial Display 2 */

    write_reg(par, 0x90,(0<<7)|(16<<0)); /* Frame Cycle Contral */
    write_reg(par, 0x92,0x0000);         /* Panel Interface Contral 2 */
    write_reg(par, 0x93,0x0001);         /* Panel Interface Contral 3 */
    write_reg(par, 0x95,0x0110);         /* Frame Cycle Contral */
    write_reg(par, 0x97,(0<<8));
    write_reg(par, 0x98,0x0000);         /* Frame Cycle Contral */
    write_reg(par, 0x07,0x0133);

    mdelay(100);   /* mdelay 50 ms */

	return 0;
}

static void hy28afb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

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


struct fbtft_display hy28afb_display = {
	.width = WIDTH,
	.height = HEIGHT,
};

static int hy28afb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&hy28afb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	if(!par->startbyte)
		par->startbyte = 0b01110000;
	fbtft_debug_init(par);
	par->fbtftops.init_display = hy28afb_init_display;
	par->fbtftops.write_reg = fbtft_write_reg16_bus8;
	par->fbtftops.set_addr_win = hy28afb_set_addr_win;
	par->fbtftops.register_backlight = fbtft_register_backlight;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int hy28afb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver hy28afb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = hy28afb_probe,
	.remove = hy28afb_remove,
};

static int __init hy28afb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);
	return spi_register_driver(&hy28afb_driver);
}

static void __exit hy28afb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);
	spi_unregister_driver(&hy28afb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(hy28afb_init);
module_exit(hy28afb_exit);

MODULE_DESCRIPTION("FB driver for the HY28A SPI LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
