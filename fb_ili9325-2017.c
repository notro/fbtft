/*
 * FB driver for the ILI9325 LCD Controller
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
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9325"
#define WIDTH		240
#define HEIGHT		320
#define BPP		16
#define FPS		20
#define DEFAULT_GAMMA	"0F 00 7 2 0 0 6 5 4 1\n" \
			"04 16 2 7 6 3 2 1 7 7"


static unsigned bt = 6; /* VGL=Vci*4 , VGH=Vci*4 */
module_param(bt, uint, 0);
MODULE_PARM_DESC(bt, "Sets the factor used in the step-up circuits");

static unsigned vc = 0b011; /* Vci1=Vci*0.80 */
module_param(vc, uint, 0);
MODULE_PARM_DESC(vc,
"Sets the ratio factor of Vci to generate the reference voltages Vci1");

static unsigned vrh = 0b1101; /* VREG1OUT=Vci*1.85 */
module_param(vrh, uint, 0);
MODULE_PARM_DESC(vrh,
"Set the amplifying rate (1.6 ~ 1.9) of Vci applied to output the VREG1OUT");

static unsigned vdv = 0b10010; /* VCOMH amplitude=VREG1OUT*0.98 */
module_param(vdv, uint, 0);
MODULE_PARM_DESC(vdv,
"Select the factor of VREG1OUT to set the amplitude of Vcom");

static unsigned vcm = 0b001010; /* VCOMH=VREG1OUT*0.735 */
module_param(vcm, uint, 0);
MODULE_PARM_DESC(vcm, "Set the internal VcomH voltage");


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

static int init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	bt &= 0b111;
	vc &= 0b111;
	vrh &= 0b1111;
	vdv &= 0b11111;
	vcm &= 0b111111;

	/* Initialization sequence from ILI9325 Application Notes */
    write_reg(par, 0xE5, 0x78F0); /* set SRAM internal timing */
    write_reg(par, 0x01, 0x0100); /* set Driver Output Control */
    write_reg(par, 0x02, 0x0700); /* set 1 line inversion */
    write_reg(par, 0x03, 0x1030); /* set GRAM write direction and BGR=1 */
    write_reg(par, 0x04, 0x0000); /* Resize register */
    write_reg(par, 0x08, 0x0207); /* set the back porch and front porch */
    write_reg(par, 0x09, 0x0000); /* set non-display area refresh cycle ISC[3:0] */
    write_reg(par, 0x0A, 0x0000); /* FMARK function */
    write_reg(par, 0x0C, 0x0000); /* RGB interface setting */
    write_reg(par, 0x0D, 0x0000); /* Frame marker Position */
    write_reg(par, 0x0F, 0x0000); /* RGB interface polarity */
    
    /*************Power On sequence ****************/
    write_reg(par, 0x10, 0x0000); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
    write_reg(par, 0x11, 0x0007); /* DC1[2:0], DC0[2:0], VC[2:0] */
    write_reg(par, 0x12, 0x0000); /* VREG1OUT voltage */
    write_reg(par, 0x13, 0x0000); /* VDV[4:0] for VCOM amplitude */
    write_reg(par, 0x07, 0x0001);
    mdelay(200);

    /* Dis-charge capacitor power voltage */
    write_reg(par, 0x10, 0x1090); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
    write_reg(par, 0x11, 0x0227); /* Set DC1[2:0], DC0[2:0], VC[2:0] */
    mdelay(50); /* Delay 50ms */
    write_reg(par, 0x12, 0x001F);
    mdelay(50); /* Delay 50ms */
    write_reg(par, 0x13, 0x1500); /* VDV[4:0] for VCOM amplitude */
    write_reg(par, 0x29, 0x0027); /* 04 VCM[5:0] for VCOMH */
    write_reg(par, 0x2B, 0x000D); /* Set Frame Rate */
    mdelay(50); /* Delay 50ms */
    write_reg(par, 0x20, 0x0000); /* GRAM horizontal Address */
    write_reg(par, 0x21, 0x0000); /* GRAM Vertical Address */

    /* ----------- Adjust the Gamma Curve ---------- */
    write_reg(par, 0x30, 0x0000);
    write_reg(par, 0x31, 0x0707);
    write_reg(par, 0x32, 0x0307);
    write_reg(par, 0x35, 0x0200);
    write_reg(par, 0x36, 0x0008);
    write_reg(par, 0x37, 0x0004);
    write_reg(par, 0x38, 0x0000);
    write_reg(par, 0x39, 0x0707);
    write_reg(par, 0x3C, 0x0002);
    write_reg(par, 0x3D, 0x1D04);
    
    /* ------------------ Set GRAM area --------------- */
    write_reg(par, 0x50, 0x0000); /* Horizontal GRAM Start Address */
    write_reg(par, 0x51, 0x00EF); /* Horizontal GRAM End Address */
    write_reg(par, 0x52, 0x0000); /* Vertical GRAM Start Address */
    write_reg(par, 0x53, 0x013F); /* Vertical GRAM Start Address */
    write_reg(par, 0x60, 0xA700); /* Gate Scan Line */
    write_reg(par, 0x61, 0x0001); /* NDL,VLE, REV */
    write_reg(par, 0x6A, 0x0000); /* set scrolling line */

    /* -------------- Partial Display Control --------- */
    write_reg(par, 0x80, 0x0000);
    write_reg(par, 0x81, 0x0000);
    write_reg(par, 0x82, 0x0000);
    write_reg(par, 0x83, 0x0000);
    write_reg(par, 0x84, 0x0000);
    write_reg(par, 0x85, 0x0000);

    /* -------------- Panel Control ------------------- */
    write_reg(par, 0x90, 0x0010);
    write_reg(par, 0x92, 0x0600);
    write_reg(par, 0x07, 0x0133); /* 262K color and display ON */

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);
	switch (par->info->var.rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 180:
		write_reg(par, 0x0020, WIDTH - 1 - xs);
		write_reg(par, 0x0021, HEIGHT - 1 - ys);
		break;
	case 270:
		write_reg(par, 0x0020, WIDTH - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 90:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, HEIGHT - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
}

static int set_var(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	switch (par->info->var.rotate) {
	/* AM: GRAM update direction */
	case 0:
		write_reg(par, 0x03, 0x0030 | (par->bgr << 12));
		break;
	case 180:
		write_reg(par, 0x03, 0x0000 | (par->bgr << 12));
		break;
	case 270:
		write_reg(par, 0x03, 0x0028 | (par->bgr << 12));
		break;
	case 90:
		write_reg(par, 0x03, 0x0018 | (par->bgr << 12));
		break;
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 16,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.gamma_num = 2,
	.gamma_len = 10,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
	},
};
FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9325", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9325");
MODULE_ALIAS("platform:ili9325");

MODULE_DESCRIPTION("FB driver for the ILI9325 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
