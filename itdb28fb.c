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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "itdb28fb"
#define WIDTH       320
#define HEIGHT      240
#define BPP         16
#define FPS			20


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

/* Module Parameter: rotate */
static unsigned rotate = 0;
module_param(rotate, uint, 0);
MODULE_PARM_DESC(rotate, "Rotate display (0=normal, 2=upside down)");


static void itdb28fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);
	if (rotate) {
		write_reg(par, 0x0020, ys); // Horizontal GRAM Start Address
		write_reg(par, 0x0021, (par->info->var.xres - 1)-xs); // Vertical GRAM Start Address
	}
	else {
		write_reg(par, 0x0020, (par->info->var.yres - 1)-ys); // Horizontal GRAM Start Address
		write_reg(par, 0x0021, xs); // Vertical GRAM Start Address
	}
	write_reg(par, 0x0022);
}

#define ILI9325_DRIVER_OUTPUT_CONTROL(par, sm, ss) \
        write_reg(par, 0x01, (((sm & 1) << 10) | ((ss & 1) << 8)) )

#if 1
static int itdb28fb_init_display(struct fbtft_par *par)
{
	/*
	WARNING: This init-sequence is partially derived from example code, partially
	from the datasheet and partially (especially the msleep()s) from experimentation.
	This seems to work best on the LCDs I have; YMMV.
	*/
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* Activate chip */
	gpio_set_value(par->gpio.cs, 0);

	write_reg(par, 0x00E3, 0x3008); // Set internal timing
	write_reg(par, 0x00E7, 0x0012); // Set internal timing
	write_reg(par, 0x00EF, 0x1231); // Set internal timing
	msleep(200);

	/* Driver Output Control (R01h)
	     SM - Scan mode, combined with R60h
		 SS - Direction of outputs            */
	ILI9325_DRIVER_OUTPUT_CONTROL(par, 0, 1);  /* SM=0, SS=1 (S720 to S1)*/
//	write_reg(par, 0x0001, 0x0100); // set SS and SM bit
	msleep(200);

	write_reg(par, 0x0002, 0x0700); // set 1 line inversion
	if (rotate)
		write_reg(par, 0x0003, 0x1018); // set GRAM write direction and BGR=1.
	else
		write_reg(par, 0x0003, 0x1028); // set GRAM write direction and BGR=1.
	msleep(100);
	write_reg(par, 0x0004, 0x0000); // Resize register
	msleep(100);
	write_reg(par, 0x0008, 0x0207); // set the back porch and front porch
	msleep(100);
	write_reg(par, 0x0009, 0x0000); // set non-display area refresh cycle ISC[3:0]
	msleep(100);
	write_reg(par, 0x000A, 0x0000); // FMARK function
	msleep(100);
	write_reg(par, 0x000C, 0x0000); // RGB interface setting
	msleep(100);
	write_reg(par, 0x000D, 0x0000); // Frame marker Position
	msleep(100);
	write_reg(par, 0x000F, 0x0000); // RGB interface polarity
	//--------------Power On sequence --------------//
	msleep(100); //'wait 2 frames or more'
	write_reg(par, 0x0010, 0x0000); // SAP); Trans_Dat_16(BT[3:0]); Trans_Dat_16(AP); Trans_Dat_16(DSTB); Trans_Dat_16(SLP); Trans_Dat_16(STB
	msleep(100); //'wait 2 frames or more'
	write_reg(par, 0x0011, 0x0007); // DC1[2:0]); Trans_Dat_16(DC0[2:0]); Trans_Dat_16(VC[2:0]
	msleep(100); //'wait 2 frames or more'
	write_reg(par, 0x0012, 0x0000); // VREG1OUT voltage
	msleep(100);
	write_reg(par, 0x0013, 0x0000); // VDV[4:0] for VCOM amplitude
	msleep(400); // Dis-charge capacitor power voltage
	write_reg(par, 0x0010, 0x1490); // SAP); Trans_Dat_16(BT[3:0]); Trans_Dat_16(AP); Trans_Dat_16(DSTB); Trans_Dat_16(SLP); Trans_Dat_16(STB
	write_reg(par, 0x0011, 0x0227); // R11h=0x0221 at VCI=3.3V); Trans_Dat_16(DC1[2:0]); Trans_Dat_16(DC0[2:0]); Trans_Dat_16(VC[2:0]
	msleep(100); // Delayms 50m
	write_reg(par, 0x0012, 0x001c); // External reference voltage= Vci;
	msleep(100); // Delayms 50ms
	write_reg(par, 0x0013, 0x0A00); // R13=0F00 when R12=009E;VDV[4:0] for VCOM amplitude
	write_reg(par, 0x0029, 0x000F); // R29=0019 when R12=009E;VCM[5:0] for VCOMH//0012//
	write_reg(par, 0x002B, 0x000D); // Frame Rate = 91Hz
	msleep(100); // Delayms 50ms
	write_reg(par, 0x0020, 0x0000); // GRAM horizontal Address
	write_reg(par, 0x0021, 0x0000); // GRAM Vertical Address
	// ----------- Adjust the Gamma Curve ----------//
	write_reg(par, 0x0030, 0x0000);
	write_reg(par, 0x0031, 0x0203);
	write_reg(par, 0x0032, 0x0001);
	write_reg(par, 0x0035, 0x0205);
	write_reg(par, 0x0036, 0x030C);
	write_reg(par, 0x0037, 0x0607);
	write_reg(par, 0x0038, 0x0405);
	write_reg(par, 0x0039, 0x0707);
	write_reg(par, 0x003C, 0x0502);
	write_reg(par, 0x003D, 0x1008);
	//------------------ Set GRAM area ---------------//
	msleep(100);
	write_reg(par, 0x0050, 0x0000); // Horizontal GRAM Start Address
	write_reg(par, 0x0051, 0x00EF); // Horizontal GRAM End Address
	write_reg(par, 0x0052, 0x0000); // Vertical GRAM Start Address
	write_reg(par, 0x0053, 0x013F); // Vertical GRAM Start Address
	write_reg(par, 0x0060, 0xA700); // Gate Scan Line
	write_reg(par, 0x0061, 0x0001); // NDL,VLE); Trans_Dat_16(REV
	write_reg(par, 0x006A, 0x0000); // set scrolling line
	msleep(500);
	//-------------- Partial Display Control ---------//
	write_reg(par, 0x0080, 0x0000);
	write_reg(par, 0x0081, 0x0000);
	write_reg(par, 0x0082, 0x0000);
	write_reg(par, 0x0083, 0x0000);
	write_reg(par, 0x0084, 0x0000);
	write_reg(par, 0x0085, 0x0000);
	msleep(100);
	//-------------- Panel Control -------------------//
	write_reg(par, 0x0090, 0x0010);
	write_reg(par, 0x0092, 0x0600);//0x0000
	write_reg(par, 0x0093, 0x0003);
	write_reg(par, 0x0095, 0x0110);
	write_reg(par, 0x0097, 0x0000);
	write_reg(par, 0x0098, 0x0000);
	msleep(100); // Delayms 50ms
	write_reg(par, 0x0007, 0x0133); // 262K color and display ON
	msleep(200);
	write_reg(par, 0x0022);
	write_reg(par, 0x0022);

	return 0;
}

#else
static int itdb28fb_init_display(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	write_cmd(par, 0x00E3); write_data(par, 0x3008); // Set internal timing

	write_cmd(par, 0x00E7); write_data(par, 0x0012); // Set internal timing
	write_cmd(par, 0x00EF); write_data(par, 0x1231); // Set internal timing
	msleep(200);
	
	/* 01h - Driver Putput Control */
	write_cmd(par, 0x0001);
	write_data(par, 0x0100); /* 10:0  SM     Scan mode, combined with R60h
	                        8:1  SS     Direction of outputs: S720 to S1
						*/
 	msleep(200);

	/* 02h - LCD Driving Wave Control */
	write_cmd(par, 0x0002);
	write_data(par, 0x0700); /* 10:1    1
	                        9:1  B/C    Line inversion
							8:1  EOR    To set line inversion
						*/

 /* TODO: Shouldn't TRI be 0 ?? */
 	/* 03h - Entry Mode */
	write_cmd(par, 0x0003);
	write_data(par, 0x1028); /* 15:1  TRI     Data transfer in 3x 8-bit
	                       14:0  DFM     Data Frame Mode: 2bit + 8bit + 8bit
						   12:0  BGR     RGB
						    9:0  HWM
							7:0  ORG     Origin address is not moved
							5:1  I/D1    Address counter: Horizontal=decrement, Vertical=increment
							4:0  I/D0
							3:1  AM      GRAM update direction: vertical
						*/
	msleep(100);

	/* 04h - Resizing Control Register */
	write_cmd(par, 0x0004);
	write_data(par, 0x0000); /*  9:0  RCV1
	                        8:0  RCV0
							5:0  RCH1
							4:0  RCH0    No resizing
							1:0  RSZ1
							0:0  RSZ0
						*/
	msleep(100);

	/* 08h - Display Control 2 */
	write_cmd(par, 0x0008);
	write_data(par, 0x0207); /* 11:0  FP3    Front porch: 2 lines
	                       10:0  FP2
						    9:1  FP1
							8:0  FP0
							3:0  BP3    Back Porch: 7 lines
							2:1  BP2
							1:1  BP1
							0:1  BP0
						*/
	msleep(100);

	/* 09h - Display Control 3 (non-display area)*/
	write_cmd(par, 0x0009);
	write_data(par, 0x0000); /* 10:0  PTS2    Source output level: +=V63, -=V0
	                        9:0  PTS1
							8:0  PTS0
							5:0  PTG1    Scan mode: normal scan
							4:0  PTG0
							3:0  ISC3    Scan cycle interval: 0 frame
							2:0  ISC2
							1:0  ISC1
							0:0  ISC0
						*/
	msleep(100);

	/* 0Ah - Display Control 4 */
	write_cmd(par, 0x000A);
	write_data(par, 0x0000); /*  3:0  FMARKOE :disabled
	                        2:0  FMI2    Output interval of FMARK signal
						    1:0  FMI1
						    0:0  FMI0
						*/
	msleep(100);

	/* 0Ch - RGB Display Interface Control 1 */
	write_cmd(par, 0x000C);
	write_data(par, 0x0000); /* 14:0
	                       13:0
						   12:0
						    8:0  RM     GRAM interface: System interface/VSYNC interface
							5:0
							4:0
							1:0
							0:0
						*/
	msleep(100);

	/* 0Dh - Frame Marker Posistion */
	write_cmd(par, 0x000D);
	write_data(par, 0x0000); /* 8-0:0  FMP[8:0]    output position of frame cycle: 0th line */
	msleep(100);

	/* 0Fh - RGB Display Interface Control 2 */
	write_cmd(par, 0x000F);
	write_data(par, 0x0000); /* RGB interface signal polarity */


	//--------------Power On sequence --------------//
	msleep(100); //'wait 2 frames or more'

	/* 10h - Power Control 1 */
	write_cmd(par, 0x0010);
	write_data(par, 0x0000); /* 12:0  SAP    Source Driver Output Control: Disabled
	                       10:0  BT2    Operating voltage step-up factor
						    9:0  BT1
							8:0  BT0
							7:0  APE    Power Supply: Disabled
							6:0  AP2    Op-Amp current constant: Halt
							5:0  AP1
							4:0  AP0
							2:0  DSTB   Deep Standby mode: No
							1:0  SLP    Sleep Mode: No
							0:0  STB    Standby Mode: No
						*/
	msleep(100); //'wait 2 frames or more'

	/* 11h - Power Control 2 */
	write_cmd(par, 0x0011);
	write_data(par, 0x0007); /* 10:0  DC12   Step-up circuit 2 frequency: Fosc/4
	                        9:0  DC11
							8:0  DC10
							6:0  DC02   Step-up circuit 1 frequency: Fosc
							5:0  DC01
							4:0  DC00
							2:1  VC2    Vci to Vci1 factor: 1.0 x Vci
							1:1  VC1
							0:1  VC0
						*/
	msleep(100); //'wait 2 frames or more'

	/* 12h - Power Control 4 */
	write_cmd(par, 0x0012);
	write_data(par, 0x0000); /*  7:0  VCIRE   Reference voltage: External Vci
	                        4:0  PON
						    3:0  VRH3    Amplifying rate of Vci applied to output of VREG1OUT: Halt
						    2:0  VRH2
						    1:0  VRH1
						    0:0  VRH0
						*/
 	msleep(100);

	/* 13h - Power Control 4 */
	write_cmd(par, 0x0013);
	write_data(par, 0x0000); /* 12:0  VDV4    Vcom amplitude: VREG1OUT x 0.70
	                       11:0  VDV3
						   10:0  VDV2
						    9:0  VDV1
							8:0  VDV0
						*/
	msleep(400); // Dis-charge capacitor power voltage

	/* 10h - Power Control 1 */
	write_cmd(par, 0x0010);
	write_data(par, 0x1490); /* 12:1  SAP    Source Driver Output Control: Enable
	                       10:1  BT2    Operating voltage step-up factor
						    9:0  BT1
							8:0  BT0
							7:1  APE    Power Supply: Enabled
							6:0  AP2    Op-Amp current constant: 1.0
							5:0  AP1
							4:1  AP0
							2:0  DSTB   Deep Standby mode: No
							1:0  SLP    Sleep Mode: No
							0:0  STB    Standby Mode: No
						*/

	/* 11h - Power Control 2 */
	write_cmd(par, 0x0011);
	write_data(par, 0x0227); /* 10:0  DC12   Step-up circuit 2 frequency: Fosc/16
	                        9:1  DC11
							8:0  DC10
							6:0  DC02   Step-up circuit 1 frequency: Fosc/4
							5:1  DC01
							4:0  DC00
							2:1  VC2    Vci to Vci1 factor: 1.0 x Vci
							1:1  VC1
							0:1  VC0
						*/
	msleep(100);

	/* 12h - Power Control 4 */
	write_cmd(par, 0x0012);
	write_data(par, 0x001c); /*  7:0  VCIRE   Reference voltage: External Vci
	                        4:1  PON     VGL output: Enabled
						    3:1  VRH3    Amplifying rate of Vci applied to output of VREG1OUT: Vci x 1.80
						    2:1  VRH2
						    1:0  VRH1
						    0:0  VRH0
						*/
	msleep(100);

	/* 13h - Power Control 4 */
	write_cmd(par, 0x0013);
	write_data(par, 0x0A00); /* 12:0  VDV4    Vcom amplitude: VREG1OUT x 0.90
	                       11:1  VDV3
						   10:0  VDV2
						    9:1  VDV1
							8:0  VDV0
						*/

	/* 29h - Power Control 7 */
	write_cmd(par, 0x0029);
	write_data(par, 0x000F); /*  5-0:1111  VCM[5:0]    VcomH voltage: VREG1OUT x 0.760 */
 
	/* 2Bh - Frame Rate and Color Control */
	write_cmd(par, 0x002B);
	write_data(par, 0x000D); /*  3-0:1101    FRS[3:0]    Frame rate: 128 */
// Frame Rate = 91Hz
	msleep(100);

	/* 20h - GRAM Horizontal Address Set */
	write_cmd(par, 0x0020);
	write_data(par, 0x0000); /*  7-0:00000000  AD[7:0]    Address counter: 0 */

	/* 21h - GRAM Vertical Address Set */
	write_cmd(par, 0x0021);
	write_data(par, 0x0000); /*  8-0:000000000  AD[16:8]  Address counter: 0 */

	/* 30h:3Dh - Gamma Control */
	write_cmd(par, 0x0030); write_data(par, 0x0000);
	write_cmd(par, 0x0031); write_data(par, 0x0203);
	write_cmd(par, 0x0032); write_data(par, 0x0001);
	write_cmd(par, 0x0035); write_data(par, 0x0205);
	write_cmd(par, 0x0036); write_data(par, 0x030C);
	write_cmd(par, 0x0037); write_data(par, 0x0607);
	write_cmd(par, 0x0038); write_data(par, 0x0405);
	write_cmd(par, 0x0039); write_data(par, 0x0707);
	write_cmd(par, 0x003C); write_data(par, 0x0502);
	write_cmd(par, 0x003D); write_data(par, 0x1008);
	msleep(100);

	//------------------ Set GRAM area ---------------//
	/* 50h:6Ah - Horizontal and Vertical RAM Address Position */
	write_cmd(par, 0x0050); write_data(par, 0x0000); // Horizontal GRAM Start Address
	write_cmd(par, 0x0051); write_data(par, 0x00EF); // Horizontal GRAM End Address
	write_cmd(par, 0x0052); write_data(par, 0x0000); // Vertical GRAM Start Address
	write_cmd(par, 0x0053); write_data(par, 0x013F); // Vertical GRAM Start Address
	/* 60h:6Ah Gate Scan Control */
	write_cmd(par, 0x0060);
	write_data(par, 0xA700); /*   15:1       GS          Gate Driver Scan Direction
	                       13-8:100111  NL[5:0]     Number of lines to driver: 
	                        5-0:000000  SCN[5:0]    Start Gate Line: 0h + GS=1 + SM=0 from 01h -> G320
						*/
	write_cmd(par, 0x0061);
	write_data(par, 0x0001); /*  2:0  NDL    Source driver ouput level in non-display area: +=V63, -=V0
	                        1:0  VLE    Vertical Scroll: Fixed
							0:1  REV    Grayscale inversion: Enable
						*/
 	write_cmd(par, 0x006A);
	write_data(par, 0x0000); /*  8-0:0h  VL[8:0]    Scrolling amount of base image: 0 */
 	msleep(500);

	/*-------------- Partial Display Control ---------*/
	/* 80h - Partial Image 1 Display Position */
	write_cmd(par, 0x0080);
	write_data(par, 0x0000); /* 8-0:0h  PTDP0[8:0]    Display position of partial image 1: 0 */
	/* 81h - Partial Image 1 RAM Start Address */
	write_cmd(par, 0x0081);
	write_data(par, 0x0000); /* 8-0:0h  PTSA0[8:0] */
	/* 82h - Partial Image 1 RAM End Address */
	write_cmd(par, 0x0082);
	write_data(par, 0x0000); /* 8-0:0h  PTEA0[8:0] */
	/* 83h - Partial Image 2 Display Position */
	write_cmd(par, 0x0083);
	write_data(par, 0x0000); /* 8-0:0h  PTDP1[8:0]    Display position of partial image 2: 0 */
	/* 84h - Partial Image 2 RAM Start Address */
	write_cmd(par, 0x0084);
	write_data(par, 0x0000); /* 8-0:0h  PTSA1[8:0] */
	/* 85h - Partial Image 2 RAM End Address */
	write_cmd(par, 0x0085);
	write_data(par, 0x0000); /* 8-0:0h  PTEA1[8:0] */
	msleep(100);

	//-------------- Panel Control -------------------//
	/* 90h - Panel Interface Control 1 */
	write_cmd(par, 0x0090);
	write_data(par, 0x0010); /*  9:0  DIVI1    Internal clock division ratio: 1 -> fosc/1
	                        8:0  DIVI0
							4:1  RTNI4    1H clock number: 16 clocks
							3:0  RTNI3
							2:0  RTNI2
							1:0  RTNI1
							0:0  RTNI0
						*/
	/* 90h - Panel Interface Control 2 */
	write_cmd(par, 0x0092);
	write_data(par, 0x0600); /* 10:1  NOWI2    Gate output non-overlap period: 6 clocks
	                        9:1  NOWI1
							8:0  NOWI0
						*/
 
	/* 93h - ?? */
	write_cmd(par, 0x0093); write_data(par, 0x0003);

/* is RGB interface in use ?? */
	/* 90h - Panel Interface Control 4 */
	write_cmd(par, 0x0095);
	write_data(par, 0x0110); /*  9:0  DIVE1    Division ratio of DOTCLK when syncronized with RGB interface signals: 1/4
	                        8:1  DIVE0
							5:0  RTNE5    1H line clock number of RGB interface mode: 10h -> 16 clocks
							4:1  RTNE4
							3:0  RTNE3
							2:0  RTNE2
							1:0  RTNE1
							0:0  RTNE0
						*/

	/* 97h - ?? */
	write_cmd(par, 0x0097); write_data(par, 0x0000);

	/* 98h - ?? */
	write_cmd(par, 0x0098); write_data(par, 0x0000);
	msleep(100);

	/* 07h - Display Control 1 */
	write_cmd(par, 0x0007);
	write_data(par, 0x0133); /* 13:0  PTDE1 Partial Image 2: Disabled
	                       12:0  PTDE0 Partial Image 1: Disabled
							8:1  BASEE Base image display: Enable
							5:1  GON   Normal display
							4:1  DTE   Normal display
							3:0  CL    Colors: 262,144
							1:1  D1    Turn on display
							0:1  D0
						*/
 	msleep(200);

	/* 22h - Write Data to GRAM */
	write_cmd(par, 0x0022);
	write_cmd(par, 0x0022);

	return 0;
}
#endif



static int itdb28fb_blank(struct fbtft_par *par, bool on)
{
	if (par->gpio.led[0] == -1)
		return -EINVAL;

	fbtft_dev_dbg(DEBUG_BLANK, par->info->device, "%s(%s)\n", __func__, on ? "on" : "off");
	
	if (on)
		/* Turn off backlight */
		gpio_set_value(par->gpio.led[0], 0);
	else
		/* Turn on backlight */
		gpio_set_value(par->gpio.led[0], 1);

	return 0;
}

static int itdb28fb_verify_gpios(struct fbtft_par *par)
{
	int i;

	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.cs < 0) {
		dev_err(par->info->device, "Missing info about 'cs' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}
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

	return 0;
}

struct fbtft_display itdb28fb_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
};

static int __devinit itdb28fb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &pdev->dev, "%s()\n", __func__);

	if (!(rotate == 0 || rotate == 2)) {
		dev_warn(&pdev->dev, "module parameter 'rotate' can only be 0=Normal or 2=Upside down. Setting it to Normal.\n");
		rotate = 0;
	}
	info = fbtft_framebuffer_alloc(&itdb28fb_display, &pdev->dev);
	if (!info)
		return -ENOMEM;

	info->var.rotate = rotate;
	par = info->par;
	par->pdev = pdev;
	fbtft_debug_init(par);
	par->fbtftops.init_display = itdb28fb_init_display;
	par->fbtftops.write = fbtft_write_gpio8_wr;
	par->fbtftops.write_data_command = fbtft_write_data_command16_bus8;
	par->fbtftops.write_reg = fbtft_write_reg16_bus8;
	par->fbtftops.set_addr_win = itdb28fb_set_addr_win;
	par->fbtftops.blank = itdb28fb_blank;
	par->fbtftops.verify_gpios = itdb28fb_verify_gpios;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	/* turn on backlight */
	itdb28fb_blank(par, false);

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int __devexit itdb28fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &pdev->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct platform_driver itdb28fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = itdb28fb_probe,
	.remove = __devexit_p(itdb28fb_remove),
};

static int __init itdb28fb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME" - %s()\n", __func__);

	return platform_driver_register(&itdb28fb_driver);
}

static void __exit itdb28fb_exit(void)
{
	fbtft_pr_debug(DRVNAME" - %s()\n", __func__);
	platform_driver_unregister(&itdb28fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(itdb28fb_init);
module_exit(itdb28fb_exit);

MODULE_DESCRIPTION("FB driver for the ITDB02-2.8 LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
