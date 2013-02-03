/*
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

#ifndef __LINUX_FBTFT_H
#define __LINUX_FBTFT_H

#include <linux/fb.h>


#define FBTFT_NOP		0x00
#define FBTFT_SWRESET	0x01
#define FBTFT_RDDID		0x04
#define FBTFT_RDDST		0x09
#define FBTFT_CASET		0x2A
#define FBTFT_RASET		0x2B
#define FBTFT_RAMWR		0x2C


#define FBTFT_GPIO_NO_MATCH		0xFFFF
#define FBTFT_GPIO_NAME_SIZE	32

struct fbtft_gpio {
	char name[FBTFT_GPIO_NAME_SIZE];
	unsigned gpio;
};

struct fbtft_platform_data {
	const struct fbtft_gpio *gpios;
	unsigned fps;
	int txbuflen;
	void *extra;
};

struct fbtft_par;

typedef void (*fbtft_write_dataDef)(struct fbtft_par *par, u8 data);
typedef void (*fbtft_write_cmdDef)(struct fbtft_par *par, u8 data);

struct fbtft_ops {
	fbtft_write_dataDef write_data;
	fbtft_write_cmdDef write_cmd;

	int (*write)(struct fbtft_par *par, void *buf, size_t len);
	int (*write_vmem)(struct fbtft_par *par, size_t offset, size_t len);

	void (*set_addr_win)(struct fbtft_par *par, int xs, int ys, int xe, int ye);
	void (*reset)(struct fbtft_par *par);
	void (*mkdirty)(struct fb_info *info, int from, int to);
	void (*update_display)(struct fbtft_par *par);
	int (*init_display)(struct fbtft_par *par);
	int (*blank)(struct fbtft_par *par, bool on);

	unsigned long (*request_gpios_match)(struct fbtft_par *par, const struct fbtft_gpio *gpio);
	int (*request_gpios)(struct fbtft_par *par);
	void (*free_gpios)(struct fbtft_par *par);
};

struct fbtft_display {
	unsigned width;
	unsigned height;
	unsigned bpp;
	unsigned fps;
	int txbuflen;
	unsigned txwordsize;
	unsigned txdatabitmask;
};

struct fbtft_par {
	struct fbtft_display *display;
	struct spi_device *spi;
	struct fb_info *info;
	struct fbtft_platform_data *pdata;
	u16 *ssbuf;
    u32 pseudo_palette[16];
	struct {
		void *buf;
		size_t len;
		unsigned wordsize;
		unsigned databitmask;
	} txbuf;
	u8 *buf;  /* small buffer used when writing init data over SPI */
	struct fbtft_ops fbtftops;
	unsigned dirty_low;
	unsigned dirty_high;
	u32 throttle_speed;
	struct {
		int reset;
		int dc;
		/* the following is not used or requested by core */
		int rd;
		int wr;
		int cs;
		int db[16];
		int led[16];
		int aux[16];
	} gpio;
	void *extra;
};

extern struct fb_info *fbtft_framebuffer_alloc(struct fbtft_display *display, struct device *dev);
extern void fbtft_framebuffer_release(struct fb_info *info);
extern int fbtft_register_framebuffer(struct fb_info *fb_info);
extern int fbtft_unregister_framebuffer(struct fb_info *fb_info);

#endif /* __LINUX_FBTFT_H */
