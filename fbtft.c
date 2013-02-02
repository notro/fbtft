/*
 * Copyright (C) 2013 Noralf Tronnes
 *
 * This driver is inspired by:
 *   st7735fb.c, Copyright (C) 2011, Matt Porter
 *   broadsheetfb.c, Copyright (C) 2008, Jaya Kumar
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

#include "fbtft.h"



unsigned long fbtft_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	if (strcasecmp(gpio->name, "reset") == 0) {
		par->gpio.reset = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}
	else if (strcasecmp(gpio->name, "dc") == 0) {
		par->gpio.dc = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}
	else if (strcasecmp(gpio->name, "blank") == 0) {
		par->gpio.blank = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

int fbtft_request_gpios(struct fbtft_par *par)
{
	struct fbtft_platform_data *pdata = par->pdata;
	const struct fbtft_gpio *gpio;
	unsigned long flags;
	int ret;

	if (pdata && pdata->gpios) {
		gpio = pdata->gpios;
		while (gpio->name[0]) {


			flags = par->fbtftops.request_gpios_match(par, gpio);

			if (flags != FBTFT_GPIO_NO_MATCH) {
				ret = gpio_request_one(gpio->gpio, flags, par->info->device->driver->name);
				if (ret < 0) {
					dev_err(par->info->device, "fbtft_request_gpios: could not acquire '%s' GPIO%d\n", gpio->name, gpio->gpio);
					return ret;
				}
				dev_dbg(par->info->device, "fbtft_request_gpios: acquired '%s' GPIO%d\n", gpio->name, gpio->gpio);
			}
			gpio++;
		}
	}

	return 0;
}

void fbtft_free_gpios(struct fbtft_par *par)
{
	struct spi_device *spi = par->spi;
	struct fbtft_platform_data *pdata = spi->dev.platform_data;
	const struct fbtft_gpio *gpio;

	if (pdata && pdata->gpios) {
		gpio = pdata->gpios;
		while (gpio->name[0]) {
			gpio_direction_input(gpio->gpio);
			gpio_free(gpio->gpio);
			gpio++;
		}
	}
}

int fbtft_write(struct fbtft_par *par, void *buf, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= buf,
			.len		= len,
			.speed_hz	= par->throttle_speed,
		};
	struct spi_message	m;

//dev_dbg(par->info->device, "fbtft_write: buf=%p, len=%d%s\n", buf, len, par->throttle_speed ? ", throttled" : "");

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(par->spi, &m);
}

/*
int fbtft_write(struct fbtft_par *par, void *buf, size_t len)
{
//	dev_dbg(par->info->device, "        fbtft_write: len=%d\n", len);
	return spi_write(par->spi, buf, len);
}
*/

void fbtft_write_data_command(struct fbtft_par *par, unsigned dc, u8 val)
{
	size_t len = 1;
	void *tx = &val;
	u16 tx16 = val;
	u32 tx32 = val;
	int ret;

	dev_dbg(par->info->device, "fbtft_write_data_command: dc=%d, val=0x%X\n", dc, val);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, dc);

	if (par->txbuf.buf) {
		len = par->txbuf.wordsize;
		if (len == 2) {
			if (dc)
				tx16 |= par->txbuf.databitmask;
			tx = &tx16;
		} else if (len == 4) {
			if (dc)
				tx32 |= par->txbuf.databitmask;
			tx = &tx32;
		}
	}

	ret = par->fbtftops.write(par, tx, len);
	if (ret < 0)
		dev_err(par->info->device, "fbtft_write_data_command: dc=%d, val=0x%X, failed with status %d\n", dc, val, ret);
}

void fbtft_write_data(struct fbtft_par *par, u8 data)
{
	fbtft_write_data_command(par, 1, data);
}

void fbtft_write_cmd(struct fbtft_par *par, u8 data)
{
	fbtft_write_data_command(par, 0, data);
}

int fbtft_write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u8 *vmem8 = par->info->screen_base + offset;
	u8  *txbuf8  = par->txbuf.buf;
	u16 *txbuf16 = par->txbuf.buf;
	u32 *txbuf32 = par->txbuf.buf;
    size_t remain = len;
	size_t to_copy;
	size_t tx_array_size;
	int i;
	int ret = 0;

	dev_dbg(par->info->device, "fbtft_write_vmem: offset=%d, len=%d\n", offset, len);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 1);

	// non buffered write
	if (!par->txbuf.buf)
		return par->fbtftops.write(par, vmem8, len);

	// sanity checks
	if (!(par->txbuf.wordsize == 1 || par->txbuf.wordsize == 2 || par->txbuf.wordsize == 4)) {
		dev_err(par->info->device, "fbtft_write_vmem: txbuf.wordsize=%d not supported, must be 1, 2 or 4\n", par->txbuf.wordsize);
		return -1;
	}

	// buffered write
	tx_array_size = par->txbuf.len / par->txbuf.wordsize;

	dev_dbg(par->info->device, "  tx_array_size=%d, wordsize=%d\n", tx_array_size, par->txbuf.wordsize);

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);

		if (par->txbuf.wordsize == 1) {
#ifdef __LITTLE_ENDIAN
			for (i=0;i<to_copy;i+=2) {
				txbuf8[i]    = vmem8[i+1];
				txbuf8[i+1]  = vmem8[i];
			}
#else
			for (i=0;i<to_copy;i++)
				txbuf8[i]    = vmem8[i];
#endif
		}
		else if (par->txbuf.wordsize == 2) {
#ifdef __LITTLE_ENDIAN
			for (i=0;i<to_copy;i+=2) {
				txbuf16[i]   = par->txbuf.databitmask | vmem8[i+1];
				txbuf16[i+1] = par->txbuf.databitmask | vmem8[i];
			}
#else
			for (i=0;i<to_copy;i++)
				txbuf16[i]   = par->txbuf.databitmask | vmem8[i];
#endif
		}
		else if (par->txbuf.wordsize == 4) {
#ifdef __LITTLE_ENDIAN
			for (i=0;i<to_copy;i+=2) {
				txbuf32[i]   = par->txbuf.databitmask | vmem8[i+1];
				txbuf32[i+1] = par->txbuf.databitmask | vmem8[i];
			}
#else
			for (i=0;i<to_copy;i++)
				txbuf32[i]   = par->txbuf.databitmask | vmem8[i];
#endif
		}
		vmem8 = vmem8 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy*par->txbuf.wordsize);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}

void fbtft_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	dev_dbg(par->info->device, "fbtft_set_addr_win(%d, %d, %d, %d)\n", xs, ys, xe, ye);

	par->fbtftops.write_cmd(par, FBTFT_CASET);
	par->fbtftops.write_data(par, 0x00);
	par->fbtftops.write_data(par, xs);
	par->fbtftops.write_data(par, 0x00);
	par->fbtftops.write_data(par, xe);

	par->fbtftops.write_cmd(par, FBTFT_RASET);
	par->fbtftops.write_data(par, 0x00);
	par->fbtftops.write_data(par, ys);
	par->fbtftops.write_data(par, 0x00);
	par->fbtftops.write_data(par, ye);

	par->fbtftops.write_cmd(par, FBTFT_RAMWR);
}


void fbtft_reset(struct fbtft_par *par)
{
	if (par->gpio.reset == -1)
		return;
	dev_dbg(par->info->device, "fbtft_reset()\n");
	gpio_set_value(par->gpio.reset, 0);
	udelay(20);
	gpio_set_value(par->gpio.reset, 1);
	mdelay(120);
}


void fbtft_update_display(struct fbtft_par *par)
{
	size_t offset, len;
	int ret = 0;

	// Sanity checks
	if (par->dirty_low > par->dirty_high) {
		dev_warn(par->info->device, 
			"update_display: dirty_low=%d is larger than dirty_high=%d. Shouldn't happen, will do full display update\n", 
			par->dirty_low, par->dirty_high);
		par->dirty_low = 0;
		par->dirty_high = par->info->var.yres - 1;
	}
	if (par->dirty_low > par->info->var.yres - 1 || par->dirty_high > par->info->var.yres - 1) {
		dev_warn(par->info->device, 
			"update_display: dirty_low=%d or dirty_high=%d larger than max=%d. Shouldn't happen, will do full display update\n", 
			par->dirty_low, par->dirty_high, par->info->var.yres - 1);
		par->dirty_low = 0;
		par->dirty_high = par->info->var.yres - 1;
	}

	dev_dbg(par->info->device, "update_display dirty_low=%d dirty_high=%d\n", par->dirty_low, par->dirty_high);

	// set display area where update goes
	par->fbtftops.set_addr_win(par, 0, par->dirty_low, par->info->var.xres-1, par->dirty_high);

	offset = par->dirty_low * par->info->fix.line_length;
	len = (par->dirty_high - par->dirty_low + 1) * par->info->fix.line_length;

	ret = par->fbtftops.write_vmem(par, offset, len);
	if (ret < 0)
		dev_err(par->info->device, "spi_write failed to update display buffer\n");

	// set display line markers as clean
	par->dirty_low = par->info->var.yres - 1;
	par->dirty_high = 0;

	dev_dbg(par->info->device, "\n");
}


void fbtft_mkdirty(struct fb_info *info, int y, int height)
{
	struct fbtft_par *par = info->par;
	struct fb_deferred_io *fbdefio = info->fbdefio;

	// special case, needed ?
	if (y == -1) {
		y = 0;
		height = info->var.yres - 1;
	}

	// Mark display lines/area as dirty
	if (y < par->dirty_low)
		par->dirty_low = y;
	if (y + height - 1 > par->dirty_high)
		par->dirty_high = y + height - 1;

	// Schedule deferred_io to update display (no-op if already on queue)
	schedule_delayed_work(&info->deferred_work, fbdefio->delay);
}

void fbtft_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct fbtft_par *par = info->par;
	struct page *page;
	unsigned long index;
	unsigned y_low=0, y_high=0;
	int count = 0;

	// Mark display lines as dirty
	list_for_each_entry(page, pagelist, lru) {
		count++;
		index = page->index << PAGE_SHIFT;
		y_low = index / info->fix.line_length;
		y_high = (index + PAGE_SIZE - 1) / info->fix.line_length;
dev_dbg(info->device, "page->index=%lu y_low=%d y_high=%d\n", page->index, y_low, y_high);
		if (y_high > info->var.yres - 1)
			y_high = info->var.yres - 1;
		if (y_low < par->dirty_low)
			par->dirty_low = y_low;
		if (y_high > par->dirty_high)
			par->dirty_high = y_high;
	}

//dev_err(info->device, "deferred_io count=%d\n", count);

	par->fbtftops.update_display(info->par);
}


void fbtft_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct fbtft_par *par = info->par;

	dev_dbg(info->dev, "fillrect dx=%d, dy=%d, width=%d, height=%d\n", rect->dx, rect->dy, rect->width, rect->height);
	sys_fillrect(info, rect);

	par->fbtftops.mkdirty(info, rect->dy, rect->height);
}

void fbtft_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area) 
{
	struct fbtft_par *par = info->par;

	dev_dbg(info->dev, "copyarea dx=%d, dy=%d, width=%d, height=%d\n", area->dx, area->dy, area->width, area->height);
	sys_copyarea(info, area);

	par->fbtftops.mkdirty(info, area->dy, area->height);
}

void fbtft_fb_imageblit(struct fb_info *info, const struct fb_image *image) 
{
	struct fbtft_par *par = info->par;

	dev_dbg(info->dev, "imageblit dx=%d, dy=%d, width=%d, height=%d\n", image->dx, image->dy, image->width, image->height);
	sys_imageblit(info, image);

	par->fbtftops.mkdirty(info, image->dy, image->height);
}

ssize_t fbtft_fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
	struct fbtft_par *par = info->par;
	ssize_t res;

	dev_dbg(info->dev, "write count=%zd, ppos=%llu)\n", count, *ppos);
	res = fb_sys_write(info, buf, count, ppos);

	// TODO: only mark changed area
	// update all for now
	par->fbtftops.mkdirty(info, -1, 0);

	return res;
}

// from pxafb.c
unsigned int chan_to_field(unsigned chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

int fbtft_fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned val;
	int ret = 1;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	}
	return ret;
}

int fbtft_fb_blank(int blank, struct fb_info *info)
{
	struct fbtft_par *par = info->par;
	int ret = -EINVAL;

	if (!par->fbtftops.blank)
		return ret;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		ret = par->fbtftops.blank(par, true);
		break;
	case FB_BLANK_UNBLANK:
		ret = par->fbtftops.blank(par, false);
		break;
	}
	return ret;
}

/**
 * fbtft_framebuffer_alloc - creates a new frame buffer info structure
 *
 * @display: pointer to structure describing the display
 * @dev: pointer to the device for this fb, this can be NULL
 *
 * Creates a new frame buffer info structure.
 *
 * Also creates and populates the following structures:
 *   info->fbops
 *   info->fbdefio
 *   info->pseudo_palette
 *   par->fbtftops
 *   par->txbuf
 *
 * Returns the new structure, or NULL if an error occurred.
 *
 */
struct fb_info *fbtft_framebuffer_alloc(struct fbtft_display *display, struct device *dev)
{
	struct fb_info *info;
	struct fbtft_par *par;
	struct fb_ops *fbops = NULL;
	struct fb_deferred_io *fbdefio = NULL;
	u8 *vmem = NULL;
	void *txbuf = NULL;
	unsigned txwordsize = display->txwordsize;
	int vmem_size = display->width*display->height*display->bpp/8;

	// sanity checks
	if (display->bpp != 16) {
		dev_err(dev, "only 16bpp supported.\n");
		return NULL;
	}
	if (!txwordsize)
		txwordsize = 1;

	vmem = vzalloc(vmem_size);
	if (!vmem)
		goto alloc_fail;

	fbops = kzalloc(sizeof(struct fb_ops), GFP_KERNEL);
	if (!fbops)
		goto alloc_fail;

	fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);
	if (!fbdefio)
		goto alloc_fail;

	info = framebuffer_alloc(sizeof(struct fbtft_par), dev);
	if (!info)
		goto alloc_fail;

	info->screen_base = (u8 __force __iomem *)vmem;
	info->fbops = fbops;
	info->fbdefio = fbdefio;

	fbops->owner        =      dev->driver->owner;
	fbops->fb_read      =      fb_sys_read;
	fbops->fb_write     =      fbtft_fb_write;
	fbops->fb_fillrect  =      fbtft_fb_fillrect;
	fbops->fb_copyarea  =      fbtft_fb_copyarea;
	fbops->fb_imageblit =      fbtft_fb_imageblit;
	fbops->fb_setcolreg =      fbtft_fb_setcolreg;
	fbops->fb_blank     =      fbtft_fb_blank;

	fbdefio->delay =           HZ/display->fps;
	fbdefio->deferred_io =     fbtft_deferred_io;
	fb_deferred_io_init(info);

	strncpy(info->fix.id, dev->driver->name, 16);
	info->fix.type =           FB_TYPE_PACKED_PIXELS;
	info->fix.visual =         FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep =	   0;
	info->fix.ypanstep =	   0;
	info->fix.ywrapstep =	   0;
	info->fix.line_length =    display->width*display->bpp/8;
	info->fix.accel =          FB_ACCEL_NONE;
	info->fix.smem_len =       vmem_size;

	info->var.xres =           display->width;
	info->var.yres =           display->height;
	info->var.xres_virtual =   display->width;
	info->var.yres_virtual =   display->height;
	info->var.bits_per_pixel = display->bpp;
	info->var.nonstd =         1;

	// RGB565
	info->var.red.offset =     11;
	info->var.red.length =     5;
	info->var.green.offset =   5;
	info->var.green.length =   6;
	info->var.blue.offset =    0;
	info->var.blue.length =    5;
	info->var.transp.offset =  0;
	info->var.transp.length =  0;

	info->flags =              FBINFO_FLAG_DEFAULT | FBINFO_VIRTFB;

	par = info->par;
	par->info = info;
	par->display = display;
	par->pdata = dev->platform_data;
	par->gpio.reset = -1;
	par->gpio.dc = -1;
	par->gpio.blank = -1;
	// Set display line markers as dirty for all. Ensures first update to update all of the display.
	par->dirty_low = 0;
	par->dirty_high = par->info->var.yres - 1;

    info->pseudo_palette = par->pseudo_palette;

	// Transmit buffer
	par->txbuf.wordsize = txwordsize;
	if (display->txbuflen == -1)
		par->txbuf.len = vmem_size * par->txbuf.wordsize;
	else if (display->txbuflen)
		par->txbuf.len = display->txbuflen;
	else
		par->txbuf.len = 0;

	par->txbuf.databitmask = display->txdatabitmask;

#ifdef __LITTLE_ENDIAN
	// need buffer for byteswapping
	if (!par->txbuf.len)
		par->txbuf.len = PAGE_SIZE;
#endif

	if (par->txbuf.len) {
		txbuf = vzalloc(par->txbuf.len);
		if (!txbuf)
			goto alloc_fail;
		par->txbuf.buf = txbuf;
	}

	// default fbtft operations
	par->fbtftops.write = fbtft_write;
	par->fbtftops.write_data = fbtft_write_data;
	par->fbtftops.write_cmd = fbtft_write_cmd;
	par->fbtftops.write_vmem = fbtft_write_vmem;
	par->fbtftops.set_addr_win = fbtft_set_addr_win;
	par->fbtftops.reset = fbtft_reset;
	par->fbtftops.mkdirty = fbtft_mkdirty;
	par->fbtftops.update_display = fbtft_update_display;

	par->fbtftops.request_gpios_match = fbtft_request_gpios_match;
	par->fbtftops.request_gpios = fbtft_request_gpios;
	par->fbtftops.free_gpios = fbtft_free_gpios;

	return info;

alloc_fail:
	if (vmem)
		vfree(vmem);
	if (txbuf)
		vfree(txbuf);
	if (fbops)
		kfree(fbops);
	if (fbdefio)
		kfree(fbdefio);

	return NULL;
}
EXPORT_SYMBOL(fbtft_framebuffer_alloc);

/**
 * fbtft_framebuffer_release - frees up all memory used by the framebuffer
 *
 * @info: frame buffer info structure
 *
 */
void fbtft_framebuffer_release(struct fb_info *info)
{
	struct fbtft_par *par = info->par;

	fb_deferred_io_cleanup(info);
	vfree(info->screen_base);
	if (par->txbuf.buf)
		vfree(par->txbuf.buf);
	kfree(info->fbops);
	kfree(info->fbdefio);
	framebuffer_release(info);
	gpio_free(par->gpio.reset);
}
EXPORT_SYMBOL(fbtft_framebuffer_release);

/**
 *	fbtft_register_framebuffer - registers a tft frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *  Sets SPI driverdata if needed
 *  Requests needed gpios.
 *  Initializes display
 *  Updates display.
 *	Registers a frame buffer device @fb_info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */
int fbtft_register_framebuffer(struct fb_info *fb_info)
{
	int ret;
	char text1[50] = "";
	char text2[50] = "";
	char text3[50] = "";
	char text4[50] = "";
	struct fbtft_par *par = fb_info->par;
	struct spi_device *spi = par->spi;

	// sanity check
	if (!par->fbtftops.init_display) {
		dev_err(fb_info->device, "missing init_display()\n");
		return -EINVAL;
	}

	if (spi)
		spi_set_drvdata(spi, fb_info);

	ret = par->fbtftops.request_gpios(par);
	if (ret < 0)
		goto reg_fail;

	ret = par->fbtftops.init_display(par);
	if (ret < 0)
		goto reg_fail;

	par->fbtftops.update_display(par);

	ret = register_framebuffer(fb_info);
	if (ret < 0)
		goto reg_fail;

	// [Tue Jan  8 19:36:41 2013] graphics fb1: hx8340fb frame buffer, 75 KiB video memory, 151 KiB buffer memory, spi0.0 at 32 MHz, GPIO25 for reset
	if (par->txbuf.buf)
		sprintf(text1, ", %d KiB buffer memory", par->txbuf.len >> 10);
	if (spi)
		sprintf(text2, ", spi%d.%d at %d MHz", spi->master->bus_num, spi->chip_select, spi->max_speed_hz/1000000);
	if (par->gpio.reset != -1)
		sprintf(text3, ", GPIO%d for reset", par->gpio.reset);
	if (par->gpio.dc != -1)
		sprintf(text4, ", GPIO%d for D/C", par->gpio.dc);
	dev_info(fb_info->dev, "%s frame buffer, %d KiB video memory%s%s%s%s\n",
		fb_info->fix.id, fb_info->fix.smem_len >> 10, text1, text2, text3, text4);

	return 0;

reg_fail:
	if (spi)
		spi_set_drvdata(spi, NULL);
	par->fbtftops.free_gpios(par);

	return ret;
}
EXPORT_SYMBOL(fbtft_register_framebuffer);

/**
 *	fbtft_unregister_framebuffer - releases a tft frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *  Frees SPI driverdata if needed
 *  Frees gpios.
 *	Unregisters frame buffer device.
 *
 */
int fbtft_unregister_framebuffer(struct fb_info *fb_info)
{
	struct fbtft_par *par = fb_info->par;
	struct spi_device *spi = par->spi;

	if (spi)
		spi_set_drvdata(spi, NULL);
	par->fbtftops.free_gpios(par);
	return unregister_framebuffer(fb_info);
}
EXPORT_SYMBOL(fbtft_unregister_framebuffer);


MODULE_LICENSE("GPL");
