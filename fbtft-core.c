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
#include <linux/backlight.h>

#include "fbtft.h"


// This will be used if the driver doesn't provide debug support
#ifdef DEBUG
static unsigned long dummy_debug = 0xFFFFFFFF;
#else
static unsigned long dummy_debug = 0;
#endif


void _fbtft_dev_dbg_hex(const struct device *dev, int groupsize, void *buf, size_t len, const char *fmt, ...)
{
	va_list args;
	static char textbuf[512];
	char *text = textbuf;
	size_t text_len;

	va_start(args, fmt);
	text_len = vscnprintf(text, sizeof(textbuf), fmt, args);
	va_end(args);

	hex_dump_to_buffer(buf, len, 32, groupsize, text + text_len, 512 - text_len, false);

	if (len > 32)
		dev_info(dev, "%s ...\n", text);
	else
		dev_info(dev, "%s\n", text);
}

unsigned long fbtft_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	int ret;
	long val;

	fbtft_fbtft_dev_dbg(DEBUG_REQUEST_GPIOS_MATCH, par, par->info->device, "%s('%s')\n", __func__, gpio->name);

	if (strcasecmp(gpio->name, "reset") == 0) {
		par->gpio.reset = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}
	else if (strcasecmp(gpio->name, "dc") == 0) {
		par->gpio.dc = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}
	else if (strcasecmp(gpio->name, "cs") == 0) {
		par->gpio.cs = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}
	else if (strcasecmp(gpio->name, "wr") == 0) {
		par->gpio.wr = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}
	else if (strcasecmp(gpio->name, "rd") == 0) {
		par->gpio.rd = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}
	else if (gpio->name[0] == 'd' && gpio->name[1] == 'b') {
		ret = kstrtol(&gpio->name[2], 10, &val);
		if (ret == 0 && val < 16) {
			par->gpio.db[val] = gpio->gpio;
			return GPIOF_OUT_INIT_LOW;
		}
	}
	else if (strcasecmp(gpio->name, "led") == 0) {
		par->gpio.led[0] = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

int fbtft_request_gpios(struct fbtft_par *par)
{
	struct fbtft_platform_data *pdata = par->pdata;
	const struct fbtft_gpio *gpio;
	unsigned long flags;
	int i;
	int ret;

	/* Initialize gpios to disabled */
	par->gpio.reset = -1;
	par->gpio.dc = -1;
	par->gpio.rd = -1;
	par->gpio.wr = -1;
	par->gpio.cs = -1;
	for (i=0;i<16;i++) {
		par->gpio.db[i] = -1;
		par->gpio.led[i] = -1;
		par->gpio.aux[i] = -1;
	}

	if (pdata && pdata->gpios) {
		gpio = pdata->gpios;
		while (gpio->name[0]) {
			flags = FBTFT_GPIO_NO_MATCH;
			/* if driver provides match function, try it first, if no match use our own */
			if (par->fbtftops.request_gpios_match)
				flags = par->fbtftops.request_gpios_match(par, gpio);
			if (flags == FBTFT_GPIO_NO_MATCH)
				flags = fbtft_request_gpios_match(par, gpio);
			if (flags != FBTFT_GPIO_NO_MATCH) {
				ret = gpio_request_one(gpio->gpio, flags, par->info->device->driver->name);
				if (ret < 0) {
					dev_err(par->info->device, "%s: no match for '%s' GPIO%d\n", __func__, gpio->name, gpio->gpio);
					return ret;
				}
				fbtft_fbtft_dev_dbg(DEBUG_REQUEST_GPIOS, par, par->info->device, "%s: '%s' = GPIO%d\n", __func__, gpio->name, gpio->gpio);
			}
			gpio++;
		}
	}

	return 0;
}

void fbtft_free_gpios(struct fbtft_par *par)
{
	struct fbtft_platform_data *pdata = NULL;
	const struct fbtft_gpio *gpio;

	fbtft_fbtft_dev_dbg(DEBUG_FREE_GPIOS, par, par->info->device, "%s()\n", __func__);

	if(par->spi)
		pdata = par->spi->dev.platform_data;
	if (par->pdev)
		pdata = par->pdev->dev.platform_data;

	if (pdata && pdata->gpios) {
		gpio = pdata->gpios;
		while (gpio->name[0]) {
			dev_dbg(par->info->device, "fbtft_free_gpios: freeing '%s'\n", gpio->name);
			gpio_direction_input(gpio->gpio);  /* if the gpio wasn't recognized by request_gpios, WARN() will protest */
			gpio_free(gpio->gpio);
			gpio++;
		}
	}
}

int fbtft_backlight_update_status(struct backlight_device *bd)
{
	struct fbtft_par *par = bl_get_data(bd);
	bool polarity = !!(bd->props.state & BL_CORE_DRIVER1);

	fbtft_fbtft_dev_dbg(DEBUG_BACKLIGHT, par, par->info->device, "%s: polarity=%d, power=%d\n", __func__, polarity, bd->props.power);

	if (bd->props.power == FB_BLANK_UNBLANK)
		gpio_set_value(par->gpio.led[0], polarity);
	else
		gpio_set_value(par->gpio.led[0], !polarity);

	return 0;
}

int fbtft_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

void fbtft_unregister_backlight(struct fbtft_par *par)
{
	const struct backlight_ops *bl_ops;

	fbtft_fbtft_dev_dbg(DEBUG_BACKLIGHT, par, par->info->device, "%s()\n", __func__);

	if (par->info->bl_dev) {
		bl_ops = par->info->bl_dev->ops;
		backlight_device_unregister(par->info->bl_dev);
		par->info->bl_dev = NULL;
		kfree(bl_ops);
	}
}
EXPORT_SYMBOL(fbtft_unregister_backlight);

void fbtft_register_backlight(struct fbtft_par *par)
{
	struct backlight_device *bd;
	struct backlight_properties bl_props = { 0, };
	struct backlight_ops *bl_ops;

	fbtft_fbtft_dev_dbg(DEBUG_BACKLIGHT, par, par->info->device, "%s()\n", __func__);

	if (par->gpio.led[0] == -1) {
		fbtft_fbtft_dev_dbg(DEBUG_BACKLIGHT, par, par->info->device, "%s(): led pin not set, exiting.\n", __func__);
		return;
	}

	bl_ops = kzalloc(sizeof(struct backlight_ops), GFP_KERNEL);
	if (!bl_ops) {
		dev_err(par->info->device, "%s: could not allocate memeory for backlight operations.\n", __func__);
		return;
	}

	bl_ops->get_brightness = fbtft_backlight_get_brightness;
	bl_ops->update_status = fbtft_backlight_update_status;
	bl_props.type = BACKLIGHT_RAW;
	/* Assume backlight is off, get polarity from current state of pin */
	bl_props.power = FB_BLANK_POWERDOWN;
	if (!gpio_get_value(par->gpio.led[0]))
		bl_props.state |= BL_CORE_DRIVER1;

	bd = backlight_device_register(dev_driver_string(par->info->device), par->info->device, par, bl_ops, &bl_props);
	if (IS_ERR(bd)) {
		dev_err(par->info->device, "cannot register backlight device (%ld)\n", PTR_ERR(bd));
		goto failed;
	}
	par->info->bl_dev = bd;

	if (!par->fbtftops.unregister_backlight)
		par->fbtftops.unregister_backlight = fbtft_unregister_backlight;

	return;

failed:
	if (bl_ops)
		kfree(bl_ops);
}
EXPORT_SYMBOL(fbtft_register_backlight);

void fbtft_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

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
}


void fbtft_reset(struct fbtft_par *par)
{
	if (par->gpio.reset == -1)
		return;
	fbtft_fbtft_dev_dbg(DEBUG_RESET, par, par->info->device, "%s()\n", __func__);
	gpio_set_value(par->gpio.reset, 0);
	udelay(20);
	gpio_set_value(par->gpio.reset, 1);
	mdelay(120);
}


void fbtft_update_display(struct fbtft_par *par)
{
	struct timespec ts_start,ts_end,test_of_time;
	long ms, us, ns;
	bool timeit = false;
	int ret = 0;

	if (unlikely(*par->debug & (DEBUG_TIME_FIRST_UPDATE | DEBUG_TIME_EACH_UPDATE))) {
		if ( (*par->debug & DEBUG_TIME_EACH_UPDATE) || \
			 ((*par->debug & DEBUG_TIME_FIRST_UPDATE) && !par->first_update_done) )
		{
			getnstimeofday(&ts_start);
			timeit = true;
		}
	}

	// Sanity checks
	if (par->dirty_lines_start > par->dirty_lines_end) {
		dev_warn(par->info->device, 
			"%s: dirty_lines_start=%d is larger than dirty_lines_end=%d. Shouldn't happen, will do full display update\n", 
			__func__, par->dirty_lines_start, par->dirty_lines_end);
		par->dirty_lines_start = 0;
		par->dirty_lines_end = par->info->var.yres - 1;
	}
	if (par->dirty_lines_start > par->info->var.yres - 1 || par->dirty_lines_end > par->info->var.yres - 1) {
		dev_warn(par->info->device, 
			"%s: dirty_lines_start=%d or dirty_lines_end=%d larger than max=%d. Shouldn't happen, will do full display update\n", 
			__func__, par->dirty_lines_start, par->dirty_lines_end, par->info->var.yres - 1);
		par->dirty_lines_start = 0;
		par->dirty_lines_end = par->info->var.yres - 1;
	}

	fbtft_fbtft_dev_dbg(DEBUG_UPDATE_DISPLAY, par, par->info->device, "%s: dirty_lines_start=%d dirty_lines_end=%d\n", __func__, par->dirty_lines_start, par->dirty_lines_end);

	if (par->fbtftops.set_addr_win)
		par->fbtftops.set_addr_win(par, 0, par->dirty_lines_start, par->info->var.xres-1, par->dirty_lines_end);

	ret = par->fbtftops.write_vmem(par);
	if (ret < 0)
		dev_err(par->info->device, "%s: write_vmem failed to update display buffer\n", __func__);

	if (unlikely(timeit)) {
		getnstimeofday(&ts_end);
		test_of_time = timespec_sub(ts_end,ts_start);
		us = (test_of_time.tv_nsec / 1000) % 1000;
		ms = (test_of_time.tv_sec * 1000) + ((test_of_time.tv_nsec / 1000000) % 1000);
		ns = test_of_time.tv_nsec % 1000;
		dev_info(par->info->device, "Elapsed time for display update: %4lu.%.3lu%.3lu ms (fps: %2lu, lines=%u)\n", 
		                            ms, us, ns, test_of_time.tv_nsec ? 1000000000 / test_of_time.tv_nsec : 0, par->dirty_lines_end - par->dirty_lines_start + 1);
		par->first_update_done = true;
	}

	// set display line markers as clean
	par->dirty_lines_start = par->info->var.yres - 1;
	par->dirty_lines_end = 0;
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
	if (y < par->dirty_lines_start)
		par->dirty_lines_start = y;
	if (y + height - 1 > par->dirty_lines_end)
		par->dirty_lines_end = y + height - 1;

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

	/* debug can be changed via sysfs */
	fbtft_debug_sync_value(par);

	/* Mark display lines as dirty */
	list_for_each_entry(page, pagelist, lru) {
		count++;
		index = page->index << PAGE_SHIFT;
		y_low = index / info->fix.line_length;
		y_high = (index + PAGE_SIZE - 1) / info->fix.line_length;
		fbtft_fbtft_dev_dbg(DEBUG_DEFERRED_IO, par, info->device, "page->index=%lu y_low=%d y_high=%d\n", page->index, y_low, y_high);
		if (y_high > info->var.yres - 1)
			y_high = info->var.yres - 1;
		if (y_low < par->dirty_lines_start)
			par->dirty_lines_start = y_low;
		if (y_high > par->dirty_lines_end)
			par->dirty_lines_end = y_high;
	}

	par->fbtftops.update_display(info->par);
}


void fbtft_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct fbtft_par *par = info->par;

	fbtft_fbtft_dev_dbg(DEBUG_FB_FILLRECT, par, info->dev, "%s: dx=%d, dy=%d, width=%d, height=%d\n", __func__, rect->dx, rect->dy, rect->width, rect->height);
	sys_fillrect(info, rect);

	par->fbtftops.mkdirty(info, rect->dy, rect->height);
}

void fbtft_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area) 
{
	struct fbtft_par *par = info->par;

	fbtft_fbtft_dev_dbg(DEBUG_FB_COPYAREA, par, info->dev, "%s: dx=%d, dy=%d, width=%d, height=%d\n", __func__,  area->dx, area->dy, area->width, area->height);
	sys_copyarea(info, area);

	par->fbtftops.mkdirty(info, area->dy, area->height);
}

void fbtft_fb_imageblit(struct fb_info *info, const struct fb_image *image) 
{
	struct fbtft_par *par = info->par;

	fbtft_fbtft_dev_dbg(DEBUG_FB_IMAGEBLIT, par, info->dev, "%s: dx=%d, dy=%d, width=%d, height=%d\n", __func__,  image->dx, image->dy, image->width, image->height);
	sys_imageblit(info, image);

	par->fbtftops.mkdirty(info, image->dy, image->height);
}

ssize_t fbtft_fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
	struct fbtft_par *par = info->par;
	ssize_t res;

	fbtft_fbtft_dev_dbg(DEBUG_FB_WRITE, par, info->dev, "%s: count=%zd, ppos=%llu\n", __func__,  count, *ppos);
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
	struct fbtft_par *par = info->par;
	unsigned val;
	int ret = 1;

	fbtft_fbtft_dev_dbg(DEBUG_FB_SETCOLREG, par, info->dev, "%s(regno=%u, red=0x%X, green=0x%X, blue=0x%X, trans=0x%X)\n", __func__, regno, red, green, blue, transp);

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

	fbtft_fbtft_dev_dbg(DEBUG_FB_BLANK, par, info->dev, "%s(blank=%d)\n", __func__, blank);

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
	struct fbtft_platform_data *pdata = dev->platform_data;
	u8 *vmem = NULL;
	void *txbuf = NULL;
	void *buf = NULL;
	int txbuflen = display->txbuflen;
	unsigned fps = display->fps;
	int vmem_size = display->width*display->height*display->bpp/8;

	// sanity checks
	if (!fps)
		fps = 1;

	/* platform_data override ? */
	if (pdata) {
		if (pdata->fps)
			fps = pdata->fps;
		if (pdata->txbuflen)
			txbuflen = pdata->txbuflen;
	}

	vmem = vzalloc(vmem_size);
	if (!vmem)
		goto alloc_fail;

	fbops = kzalloc(sizeof(struct fb_ops), GFP_KERNEL);
	if (!fbops)
		goto alloc_fail;

	fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);
	if (!fbdefio)
		goto alloc_fail;

	buf = vzalloc(128);
	if (!buf)
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

	fbdefio->delay =           HZ/fps;
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
	par->buf = buf;
	// Set display line markers as dirty for all. Ensures first update to update all of the display.
	par->dirty_lines_start = 0;
	par->dirty_lines_end = par->info->var.yres - 1;

    info->pseudo_palette = par->pseudo_palette;

	// Transmit buffer
	if (txbuflen == -1)
		txbuflen = vmem_size;

#ifdef __LITTLE_ENDIAN
	if ((!txbuflen) && (display->bpp > 8))
		txbuflen = PAGE_SIZE; /* need buffer for byteswapping */
#endif

	if (txbuflen) {
		txbuf = vzalloc(txbuflen);
		if (!txbuf)
			goto alloc_fail;
		par->txbuf.buf = txbuf;
	}
	par->txbuf.len = txbuflen;

	// default fbtft operations
	par->fbtftops.write = fbtft_write_spi;
	par->fbtftops.write_vmem = fbtft_write_vmem16_bus8;
	par->fbtftops.write_data_command = fbtft_write_data_command8_bus8;
	par->fbtftops.write_reg = fbtft_write_reg8_bus8;
	par->fbtftops.set_addr_win = fbtft_set_addr_win;
	par->fbtftops.reset = fbtft_reset;
	par->fbtftops.mkdirty = fbtft_mkdirty;
	par->fbtftops.update_display = fbtft_update_display;
	par->fbtftops.request_gpios = fbtft_request_gpios;
	par->fbtftops.free_gpios = fbtft_free_gpios;

	return info;

alloc_fail:
	if (vmem)
		vfree(vmem);
	if (txbuf)
		vfree(txbuf);
	if (buf)
		vfree(buf);
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
	vfree(par->buf);
	kfree(info->fbops);
	kfree(info->fbdefio);
	framebuffer_release(info);
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
	struct fbtft_par *par = fb_info->par;
	struct spi_device *spi = par->spi;

	// sanity checks
	if (!par->fbtftops.init_display) {
		dev_err(fb_info->device, "missing fbtftops.init_display()\n");
		return -EINVAL;
	}
	if (!par->debug)
		par->debug = &dummy_debug;

	if (spi)
		spi_set_drvdata(spi, fb_info);
	if (par->pdev)
		platform_set_drvdata(par->pdev, fb_info);

	ret = par->fbtftops.request_gpios(par);
	if (ret < 0)
		goto reg_fail;

	if (par->fbtftops.verify_gpios) {
		ret = par->fbtftops.verify_gpios(par);
		if (ret < 0)
			goto reg_fail;
	}

	ret = par->fbtftops.init_display(par);
	if (ret < 0)
		goto reg_fail;

	par->fbtftops.update_display(par);

	if (par->fbtftops.register_backlight)
		par->fbtftops.register_backlight(par);

	ret = register_framebuffer(fb_info);
	if (ret < 0)
		goto reg_fail;

	// [Tue Jan  8 19:36:41 2013] graphics fb1: hx8340fb frame buffer, 75 KiB video memory, 151 KiB buffer memory, spi0.0 at 32 MHz
	if (par->txbuf.buf)
		sprintf(text1, ", %d KiB buffer memory", par->txbuf.len >> 10);
	if (spi)
		sprintf(text2, ", spi%d.%d at %d MHz", spi->master->bus_num, spi->chip_select, spi->max_speed_hz/1000000);
	dev_info(fb_info->dev, "%s frame buffer, %d KiB video memory%s, fps=%lu%s\n",
		fb_info->fix.id, fb_info->fix.smem_len >> 10, text1, HZ/fb_info->fbdefio->delay, text2);

	/* Turn on backlight if available */
	if (fb_info->bl_dev) {
		fb_info->bl_dev->props.power = FB_BLANK_UNBLANK;
		fb_info->bl_dev->ops->update_status(fb_info->bl_dev);
	}

	return 0;

reg_fail:
	if (spi)
		spi_set_drvdata(spi, NULL);
	if (par->pdev)
		platform_set_drvdata(par->pdev, NULL);
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
	int ret;

	if (spi)
		spi_set_drvdata(spi, NULL);
	if (par->pdev)
		platform_set_drvdata(par->pdev, NULL);
	par->fbtftops.free_gpios(par);
	ret = unregister_framebuffer(fb_info);
	if (par->fbtftops.unregister_backlight)
		par->fbtftops.unregister_backlight(par);
	return ret;
}
EXPORT_SYMBOL(fbtft_unregister_framebuffer);

/* fbtft-io.c */
EXPORT_SYMBOL(fbtft_write_spi);
EXPORT_SYMBOL(fbtft_write_gpio8_wr);
EXPORT_SYMBOL(fbtft_write_gpio16_wr);

/* fbtft-bus.c */
EXPORT_SYMBOL(fbtft_write_vmem8_bus8);
EXPORT_SYMBOL(fbtft_write_vmem16_bus16);
EXPORT_SYMBOL(fbtft_write_vmem16_bus8);
EXPORT_SYMBOL(fbtft_write_vmem16_bus9);
EXPORT_SYMBOL(fbtft_write_data_command8_bus8);
EXPORT_SYMBOL(fbtft_write_data_command8_bus9);
EXPORT_SYMBOL(fbtft_write_data_command16_bus16);
EXPORT_SYMBOL(fbtft_write_data_command16_bus8);


MODULE_LICENSE("GPL");
