/*
 * FB driver for the Beneq EL320.240.36-HB SPI Display
 *
 * Based on the HX8347D FB driver
 * Copyright (C) 2013 Christian Vogelgsang
 *
 * Based on driver code found here: https://github.com/watterott/r61505u-Adapter
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "fbtft.h"
#include "fb_el32024036.h"

#define DRVNAME	"fb_el32024036"
#define FPS 10

u8 xor = 0;
u8 sof = 0xff;				// start of frame cmd
u8 eof = 0x55;				// end of frame cmd
u8 comPeriodCMD = 0x62;		// period cmd
u8 comPeriodDATA = 0x7f;	// period data cmd
u8 clearCMD = 0x11;			// clear screen cmd
u8 startCMD = 0x01;			// start cmd

void xor_calc(unsigned char c)
{
	xor ^= c;
}

void xor_reset(void)
{
	xor = 0;
}

#if defined(HARDWARE_REV_1)
static int init_display(struct fbtft_par *par)
{
	int ret = 0;

	par->fbtftops.write_vmem = el32024036_write_vmem;

	ret = par->fbtftops.write(par, &clearCMD, 1);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	return 0;
}

static int el32024036_write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u8 *sourceSPI = (u8 *)(par->info->screen_buffer + offset);
	int ret = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s(offset=%zu, len=%zu)\n", __func__, offset, len);

	ret = par->fbtftops.write(par, &startCMD, 1);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	ret = par->fbtftops.write(par, sourceSPI, TOTAL_BYTES);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	return 0;
}
#elif defined(HARDWARE_REV_A)
static int init_display(struct fbtft_par *par)
{
    int ret = 0, i = 0;

	par->fbtftops.write_vmem = el32024036_write_vmem;

	xor_reset();
	xor_calc(comPeriodCMD);
	xor_calc(comPeriodDATA);

	((u8 *)par->txbuf.buf)[i++] = sof;
	((u8 *)par->txbuf.buf)[i++] = comPeriodCMD;
	((u8 *)par->txbuf.buf)[i++] = comPeriodDATA;
	((u8 *)par->txbuf.buf)[i++] = xor;
	((u8 *)par->txbuf.buf)[i++] = eof;

	ret = par->fbtftops.write(par, &(((u8 *)par->txbuf.buf)[0]), i);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	//mdelay(500);

	i = 0;
	xor_reset();
	xor_calc(clearCMD);

	((u8 *)par->txbuf.buf)[i++] = sof;
	((u8 *)par->txbuf.buf)[i++] = clearCMD;
	((u8 *)par->txbuf.buf)[i++] = xor;
	((u8 *)par->txbuf.buf)[i++] = eof;

	ret = par->fbtftops.write(par, &(((u8 *)par->txbuf.buf)[0]), i);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	return 0;
}

static int el32024036_write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u8 *sourceSPI = (u8 *)(par->info->screen_buffer + offset);
	u8 spibytes = 0, remainderbytes = 0;
	int ret = 0, xcount = 0, ycount = 0, bcount = 0, remainder = 0, i = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s(offset=%zu, len=%zu)\n", __func__, offset, len);

	xor_reset();
	xor_calc(comPeriodCMD);
	xor_calc(comPeriodDATA);

	((u8 *)par->txbuf.buf)[i++] = sof;
	((u8 *)par->txbuf.buf)[i++] = comPeriodCMD;
	((u8 *)par->txbuf.buf)[i++] = comPeriodDATA;
	((u8 *)par->txbuf.buf)[i++] = xor;
	((u8 *)par->txbuf.buf)[i++] = eof;

	ret = par->fbtftops.write(par, &(((u8 *)par->txbuf.buf)[0]), i);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	i = 0;
	xor_reset();
	xor_calc(startCMD);

	((u8 *)par->txbuf.buf)[i++] = sof;
	((u8 *)par->txbuf.buf)[i++] = startCMD;

	for (xcount = 0; xcount < 240; xcount++)
	{
		for (ycount = 0;;)
		{
			spibytes = 0;

			if (ycount == 44) {
				spibytes = remainderbytes | (*sourceSPI & 0xff) >> 5;
				remainderbytes = 0;
				remainderbytes |= (*sourceSPI++ & 0x1f) << 2;
				((u8 *)par->txbuf.buf)[i++] = spibytes;
				xor_calc(spibytes);
				((u8 *)par->txbuf.buf)[i++] = remainderbytes;
				xor_calc(remainderbytes);
				bcount = 0;
				break;
			}

			switch (bcount) {
				case 0:
					spibytes |= (*sourceSPI & 0xff) >> 1;
					remainderbytes = 0;
					remainderbytes |= (*sourceSPI++ & 0x01) << 6;
					bcount = 1;
					((u8 *)par->txbuf.buf)[i++] = spibytes;
					xor_calc(spibytes);
					ycount++;
					break;
				case 1:
				case 2:
				case 3:
				case 4:
				case 5:
					spibytes = remainderbytes | (*sourceSPI & 0xff) >> (bcount + 1);
					remainderbytes = 0;
					remainderbytes |= (*sourceSPI++ & (0xff >> (7-bcount))) << (6 - bcount);
					bcount++;
					((u8 *)par->txbuf.buf)[i++] = spibytes;
					xor_calc(spibytes);
					ycount++;
					break;
				case 6:
					spibytes = remainderbytes | (*sourceSPI & 0xff) >> 7;
					remainderbytes = 0;
					remainderbytes |= (*sourceSPI++ & 0x7f) << 0;
					bcount = 0;
					((u8 *)par->txbuf.buf)[i++] = spibytes;
					xor_calc(spibytes);
					ycount++;
					((u8 *)par->txbuf.buf)[i++] = remainderbytes;
					xor_calc(remainderbytes);
					ycount++;
					break;
			}
		}
	}

	((u8 *)par->txbuf.buf)[i++] = xor;
	((u8 *)par->txbuf.buf)[i++] = eof;

	ret = par->fbtftops.write(par, &(((u8 *)par->txbuf.buf)[0]), TOTAL_BYTES);
	if (ret < 0) {
		dev_err(par->info->device, "display spi write failed and returned: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye) { }

static int verify_gpios(struct fbtft_par *par)
{
	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.txbuflen = TOTAL_BYTES,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.verify_gpios = verify_gpios,
		.set_var = NULL,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "beneq,el32024036", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:el32024036");
MODULE_ALIAS("platform:el32024036");

MODULE_DESCRIPTION("FB driver for the Beneq EL320.240.36-HB SPI Display");
MODULE_AUTHOR("Antonio Jenkins <antonioj@rugged-controls.com>");
MODULE_LICENSE("GPL");
