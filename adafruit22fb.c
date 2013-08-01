/*
 * FB driver for the Adafruit 2.2" LCD display
 *
 * This display uses 9-bit SPI: Data/Command bit + 8 data bits
 * For platforms that doesn't support 9-bit, the driver is capable of emulating this using 8-bit transfer.
 * This is done by transfering eight 9-bit words in 9 bytes.
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
#include <linux/vmalloc.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "adafruit22fb"
#define WIDTH       176
#define HEIGHT      220
#define TXBUFLEN	4*PAGE_SIZE


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

/* write_cmd and write_data transfers need to be buffered so we can, if needed, do 9-bit emulation */
#undef write_cmd
#undef write_data

static void adafruit22fb_write_flush(struct fbtft_par *par, int count)
{
	u16 *p = (u16 *)par->buf;
	int missing = count % 8;
	int i;

	missing = (count % 8) ? 8-(count % 8) : 0;

	if (missing) {
		/* expand buffer to be divisible by 8, and add no-ops to the beginning. The buffer must be big enough */
		/* for now this is also done when not emulating */
		for (i=(count/8+1)*8-1;i>=missing;i--) {
			p[i] = p[i - missing];
		}
		for (i=0;i<missing;i++) {
			p[i] = 0x00;
			count++;
		}
	}

	par->fbtftops.write(par, par->buf, count*2);
}

#define write_cmd(par, val)  p[i++] = val;
#define write_data(par, val)  p[i++] = 0x0100 | val;
#define write_flush(par) { adafruit22fb_write_flush(par, i); i=0; } while(0)

static int adafruit22fb_init_display(struct fbtft_par *par)
{
	u16 *p = (u16 *)par->buf;
	int i = 0;

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

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

	write_flush(par);
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

	write_flush(par);
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

	write_flush(par);
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

	write_flush(par);
	mdelay(10);

	/* MADCTL - Memory data access control */
	/*   Mode select pin SRGB: RGB direction select H/W pin for Color filter default setting: 0=BGR, 1=RGB   */
	/*   MADCTL RGB bit: RGB-BGR ORDER: 0=RGB color filter panel, 1=BGR color filter panel              */
	#define MY (1 << 7)
	#define MX (1 << 6)
	#define MV (1 << 5)
	write_cmd(par, 0x36);
	switch (par->info->var.rotate) {
	case 0:
		write_data(par, (!par->bgr << 3));
		break;
	case 1:
		write_data(par, MX | MV | (!par->bgr << 3));
		break;
	case 2:
		write_data(par, MX | MY | (!par->bgr << 3));
		break;
	case 3:
		write_data(par, MY | MV | (!par->bgr << 3));
		break;
	}

	/*	Interface Pixel Format (3Ah)
		This command is used to define the format of RGB picture data, 
		which is to be transfer via the system and RGB interface.		*/
	write_cmd(par, 0x3A);
	write_data(par, 0x05);	/* RGB interface: 16 Bit/Pixel	*/

	/*	Display on (29h)
		This command is used to recover from DISPLAY OFF mode. 
		Output from the Frame Memory is enabled.				*/
	write_cmd(par, 0x29);

	write_flush(par);
	mdelay(10);

	return 0;
}

void adafruit22fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	u16 *p = (u16 *)par->buf;
	int i = 0;

	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	write_cmd(par, FBTFT_CASET);
	write_data(par, 0x00);
	write_data(par, xs);
	write_data(par, 0x00);
	write_data(par, xe);

	write_cmd(par, FBTFT_RASET);
	write_data(par, 0x00);
	write_data(par, ys);
	write_data(par, 0x00);
	write_data(par, ye);

	write_cmd(par, FBTFT_RAMWR);

	write_flush(par);
}

static int adafruit22fb_write_emulate_9bit(struct fbtft_par *par, void *buf, size_t len)
{
	u16 *src = buf;
	u8 *dst = par->extra;
	size_t size = len / 2;
	size_t added = 0;
	int bits, i, j;
	u64 val, dc, tmp;

	for (i=0;i<size;i+=8) {
		tmp = 0;
		bits = 63;
		for (j=0;j<7;j++) {
			dc = (*src & 0x0100) ? 1 : 0;
			val = *src & 0x00FF;
			tmp |= dc << bits;
			bits -= 8;
			tmp |= val << bits--;
			src++;
		}
		tmp |= ((*src & 0x0100) ? 1 : 0);
		*(u64 *)dst = cpu_to_be64(tmp);
		dst += 8;
		*dst++ = (u8 )(*src++ & 0x00FF);
		added++;
	}

	return spi_write(par->spi, par->extra, size + added);
}

struct fbtft_display adafruit22_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
};

static int adafruit22fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&adafruit22_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	fbtft_debug_init(par);
	par->fbtftops.init_display = adafruit22fb_init_display;
	par->fbtftops.register_backlight = fbtft_register_backlight;
	par->fbtftops.write_data_command = fbtft_write_data_command8_bus9;
	par->fbtftops.write_vmem = fbtft_write_vmem16_bus9;
	par->fbtftops.set_addr_win = adafruit22fb_set_addr_win;

	spi->bits_per_word=9;
	ret = spi->master->setup(spi);
	if (ret) {
		dev_warn(&spi->dev, "9-bit SPI not available, emulating using 8-bit.\n");
		spi->bits_per_word=8;
		ret = spi->master->setup(spi);
		if (ret)
			goto fbreg_fail;

		/* allocate buffer with room for dc bits */
		par->extra = vzalloc(par->txbuf.len + (par->txbuf.len / 8));
		if (!par->extra) {
			ret = -ENOMEM;
			goto fbreg_fail;
		}

		par->fbtftops.write = adafruit22fb_write_emulate_9bit;
	}

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto fbreg_fail;

	return 0;

fbreg_fail:
	if (par->extra)
		vfree(par->extra);
	fbtft_framebuffer_release(info);

	return ret;
}

static int adafruit22fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);
	struct fbtft_par *par;

	fbtft_init_dbg(&spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		par = info->par;
		if (par->extra)
			vfree(par->extra);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver adafruit22fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = adafruit22fb_probe,
	.remove = adafruit22fb_remove,
};

static int __init adafruit22fb_init(void)
{
	return spi_register_driver(&adafruit22fb_driver);
}

static void __exit adafruit22fb_exit(void)
{
	spi_unregister_driver(&adafruit22fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(adafruit22fb_init);
module_exit(adafruit22fb_exit);

MODULE_DESCRIPTION("FB driver for the Adafruit 2.2 inch LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
