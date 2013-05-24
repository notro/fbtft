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
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME     "itdb28fb"
#define WIDTH       240
#define HEIGHT      320
#define BPP         16
#define FPS         20


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

/* Module Parameter: rotate */
static unsigned rotate = 0;
module_param(rotate, uint, 0);
MODULE_PARM_DESC(rotate, "Rotate display (0=normal, 1=clockwise, 2=upside down, 3=counterclockwise)");

/* Module Parameter: gamma */
static unsigned gamma = 1;
module_param(gamma, uint, 0);
MODULE_PARM_DESC(gamma, "Gamma profile (0=off, 1=default, 2..X=alternatives)");

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

static const u16 gamma_registers[] = {
   0x0030,0x0031,0x0032,0x0035,0x0036,0x0037,0x0038,0x0039,0x003C,0x003D
};

static const u16 gamma_profiles[][10] = {
// KP1/0  KP3/2  KP5/4  RP1/0  VRP1/0 KN1/0  KN3/2  KN5/4  RN1/0  VRN1/0
  {}, // Off
  {0x0000,0x0506,0x0104,0x0207,0x000F,0x0306,0x0102,0x0707,0x0702,0x1604}, // Default
  {0x0000,0x0203,0x0001,0x0205,0x030C,0x0607,0x0405,0x0707,0x0502,0x1008}, // http://spritesmods.com/rpi_arcade/ili9325_gpio_driver_rpi.diff
  {0x0107,0x0306,0x0207,0x0206,0x0408,0x0106,0x0102,0x0207,0x0504,0x0503}, // http://pastebin.com/HDsQ2G35
  {0x0006,0x0101,0x0003,0x0106,0x0b02,0x0302,0x0707,0x0007,0x0600,0x020b}, // http://andybrown.me.uk/wk/2012/01/01/stm32plus-ili9325-tft-driver/
  {0x0000,0x0000,0x0000,0x0206,0x0808,0x0007,0x0201,0x0000,0x0000,0x0000}, // http://mbed.org/forum/mbed/topic/3655/?page=1
  {0x0007,0x0302,0x0105,0x0206,0x0808,0x0206,0x0504,0x0007,0x0105,0x0808}, // https://bitbucket.org/plumbum/ttgui/src/eb58fe3a9401/firmware/gui/lcd_hw_ili9325.c?at=master
  {0x0000,0x0107,0x0000,0x0203,0x0402,0x0000,0x0207,0x0000,0x0203,0x0403},
  {0x0000,0x0505,0x0004,0x0006,0x0707,0x0105,0x0002,0x0707,0x0704,0x0807}, // ILI9320 2.4" LCD
  {0x0504,0x0703,0x0702,0x0101,0x0A1F,0x0504,0x0003,0x0706,0x0707,0x091F}, // ILI9320 2.8" LCD
};

static int itdb28fb_init_display(struct fbtft_par *par)
{
    int i;

	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* Activate chip */
	gpio_set_value(par->gpio.cs, 0);

	/* Initialization sequence from ILI9325 Application Notes */

	/* ----------- Start Initial Sequence ----------- */
	write_reg(par, 0x00E3, 0x3008); /* Set internal timing */
	write_reg(par, 0x00E7, 0x0012); /* Set internal timing */
	write_reg(par, 0x00EF, 0x1231); /* Set internal timing */
	write_reg(par, 0x0001, 0x0100); /* set SS and SM bit */
	write_reg(par, 0x0002, 0x0700); /* set 1 line inversion */
	switch (rotate) {
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

	/* ----------- Adjust the Gamma Curve ---------- */
    if (gamma > 0) {
        for (i=0; i<10; i++) {
            write_reg(par, gamma_registers[i], gamma_profiles[gamma][i]);
        }
    }

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

static void itdb28fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
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

static ssize_t itdb28fb_register_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not supported\n");
}

static ssize_t itdb28fb_register_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
    struct fbtft_par *par = dev_get_drvdata(dev);
    char *reg_name, *val;
    uint reg, i;
    int ret;

    val = buf;
    reg_name = strsep(&val, "=");
    if (reg_name == NULL || val == NULL) {
        pr_err(DRVNAME": unable to parse register attribute store: %s\n", buf);
        return -EINVAL;
    }
    ret = kstrtouint(reg_name, 0, &reg);
    if (ret) {
        pr_err(DRVNAME": invalid register: %s\n", reg_name);
        return ret;
    }

    ret = kstrtouint(val, 0, &i);
    if (ret) {
        pr_err(DRVNAME": invalid value: %s\n", val);
        return ret;
    }

    pr_info(DRVNAME": writing 0x%04X into register 0x%04X.\n", i, reg);
    write_reg(par, reg, i);

    return count;
}

static DEVICE_ATTR(register, 0220, itdb28fb_register_show, itdb28fb_register_store);

static struct attribute *itdb28fb_attributes[] = {
	&dev_attr_register.attr,
	NULL,
};

static struct attribute_group itdb28fb_attr_group = {
	.attrs = itdb28fb_attributes,
};


struct fbtft_display itdb28fb_display = {
	.bpp = BPP,
	.fps = FPS,
};

static int __devinit itdb28fb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;
    int num_profiles = sizeof(gamma_profiles)/sizeof(gamma_profiles[0]);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &pdev->dev, "%s()\n", __func__);

	if (gamma >= num_profiles) {
		dev_warn(&pdev->dev, "module parameter 'gamma' illegal value: %d. Can only be 0-%d. Setting it to 1.\n", gamma, num_profiles-1);
		gamma = 1;
	}

	if (rotate > 3) {
		dev_warn(&pdev->dev, "module parameter 'rotate' illegal value: %d. Can only be 0,1,2,3. Setting it to 0.\n", rotate);
		rotate = 0;
	}
	switch (rotate) {
	case 0:
	case 2:
		itdb28fb_display.width = WIDTH;
		itdb28fb_display.height = HEIGHT;
		break;
	case 1:
	case 3:
		itdb28fb_display.width = HEIGHT;
		itdb28fb_display.height = WIDTH;
		break;
	}

	info = fbtft_framebuffer_alloc(&itdb28fb_display, &pdev->dev);
	if (!info)
		return -ENOMEM;

	info->var.rotate = rotate;
	par = info->par;
	par->pdev = pdev;
	fbtft_debug_init(par);
	par->fbtftops.init_display = itdb28fb_init_display;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.write = fbtft_write_gpio8_wr;
	par->fbtftops.write_reg = fbtft_write_reg16_bus8;
	par->fbtftops.set_addr_win = itdb28fb_set_addr_win;
	par->fbtftops.verify_gpios = itdb28fb_verify_gpios;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

    ret = sysfs_create_group(&pdev->dev.kobj, &itdb28fb_attr_group);
    if (ret)
		goto out_release;

	dev_set_drvdata(&pdev->dev, par);
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

	sysfs_remove_group(&pdev->dev.kobj, &itdb28fb_attr_group);

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
MODULE_AUTHOR("Richard Hull");
MODULE_LICENSE("GPL");
