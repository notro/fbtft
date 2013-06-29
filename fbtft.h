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
#define FBTFT_GAMMA_MAX_VALUES_TOTAL 128

struct fbtft_gpio {
	char name[FBTFT_GPIO_NAME_SIZE];
	unsigned gpio;
};

struct fbtft_platform_data {
	const struct fbtft_gpio *gpios;
	unsigned rotate;
	bool bgr;          /* BlueGreenRed color format */
	unsigned fps;
	int txbuflen;
	u8 startbyte;
	char *gamma;       /* String representation of user provided Gamma curve(s) */
	void *extra;
};

struct fbtft_par;

struct fbtft_ops {
	int (*write)(struct fbtft_par *par, void *buf, size_t len);
	int (*read)(struct fbtft_par *par, void *buf, size_t len);
	int (*write_vmem)(struct fbtft_par *par);
	void (*write_data_command)(struct fbtft_par *par, unsigned dc, u32 val);
	void (*write_reg)(struct fbtft_par *par, int len, ...);

	void (*set_addr_win)(struct fbtft_par *par, int xs, int ys, int xe, int ye);
	void (*reset)(struct fbtft_par *par);
	void (*mkdirty)(struct fb_info *info, int from, int to);
	void (*update_display)(struct fbtft_par *par);
	int (*init_display)(struct fbtft_par *par);
	int (*blank)(struct fbtft_par *par, bool on);

	unsigned long (*request_gpios_match)(struct fbtft_par *par, const struct fbtft_gpio *gpio);
	int (*request_gpios)(struct fbtft_par *par);
	void (*free_gpios)(struct fbtft_par *par);
	int (*verify_gpios)(struct fbtft_par *par);

	void (*register_backlight)(struct fbtft_par *par);
	void (*unregister_backlight)(struct fbtft_par *par);

	void (*set_gamma)(struct fbtft_par *par);
};

struct fbtft_display {
	unsigned width;
	unsigned height;
	unsigned bpp;
	unsigned fps;
	int txbuflen;
	char *gamma;       /* String representation of the default Gamma curve(s) */
	char *gamma_mask;  /* String representation of the Gamma curve mask */
	int gamma_num;     /* Number of Gamma curves */
	int gamma_len;     /* Number of values per Gamma curve */
};

struct fbtft_par {
	struct fbtft_display *display;
	struct spi_device *spi;
	struct platform_device *pdev;
	struct fb_info *info;
	struct fbtft_platform_data *pdata;
	u16 *ssbuf;
    u32 pseudo_palette[16];
	struct {
		void *buf;
		size_t len;
	} txbuf;
	u8 *buf;  /* small buffer used when writing init data over SPI */
	u8 startbyte; /* used by some controllers when in SPI mode. Format: 6 bit Device id + RS bit + RW bit */
	struct fbtft_ops fbtftops;
	unsigned dirty_lines_start;
	unsigned dirty_lines_end;
	struct {
		int reset;
		int dc;
		int rd;
		int wr;
		int latch;
		int cs;
		int db[16];
		int led[16];
		int aux[16];
	} gpio;
	struct {
		struct mutex lock;
		unsigned long *curves;
		unsigned long *mask;
		int num_values;
		int num_curves;
	} gamma;
	unsigned long *debug;
	unsigned long current_debug;
	bool first_update_done;
	bool bgr;
	void *extra;
};

#define NUMARGS(...)  (sizeof((int[]){__VA_ARGS__})/sizeof(int))
#define write_reg(par, ...)  par->fbtftops.write_reg(par, NUMARGS(__VA_ARGS__), __VA_ARGS__)
#define write_cmd(par, val)  par->fbtftops.write_data_command(par, 0, val)
#define write_data(par, val) par->fbtftops.write_data_command(par, 1, val)

/* fbtft-core.c */
extern struct fb_info *fbtft_framebuffer_alloc(struct fbtft_display *display, struct device *dev);
extern void fbtft_framebuffer_release(struct fb_info *info);
extern int fbtft_register_framebuffer(struct fb_info *fb_info);
extern int fbtft_unregister_framebuffer(struct fb_info *fb_info);
extern void fbtft_register_backlight(struct fbtft_par *par);
extern void fbtft_unregister_backlight(struct fbtft_par *par);

/* fbtft-sysfs.c */
extern unsigned long fbtft_gamma_get(struct fbtft_par *par, unsigned curve_index, unsigned value_index);

/* fbtft-io.c */
extern int fbtft_write_spi(struct fbtft_par *par, void *buf, size_t len);
extern int fbtft_read_spi(struct fbtft_par *par, void *buf, size_t len);
extern int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len);
extern int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len);
extern int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len);

/* fbtft-bus.c */
extern int fbtft_write_vmem8_bus8(struct fbtft_par *par);
extern int fbtft_write_vmem16_bus16(struct fbtft_par *par);
extern int fbtft_write_vmem16_bus8(struct fbtft_par *par);
extern int fbtft_write_vmem16_bus9(struct fbtft_par *par);
extern void fbtft_write_reg8_bus8(struct fbtft_par *par, int len, ...);
extern void fbtft_write_reg16_bus8(struct fbtft_par *par, int len, ...);
extern void fbtft_write_reg16_bus16(struct fbtft_par *par, int len, ...);
extern void fbtft_write_data_command8_bus8(struct fbtft_par *par, unsigned dc, u32 val);
extern void fbtft_write_data_command8_bus9(struct fbtft_par *par, unsigned dc, u32 val);
extern void fbtft_write_data_command16_bus16(struct fbtft_par *par, unsigned dc, u32 val);
extern void fbtft_write_data_command16_bus8(struct fbtft_par *par, unsigned dc, u32 val);


/* Debug macros */

/* shorthand debug levels */
#define DEBUG_LEVEL_1               DEBUG_REQUEST_GPIOS
#define DEBUG_LEVEL_2               (DEBUG_LEVEL_1 | DEBUG_DRIVER_INIT_FUNCTIONS | DEBUG_TIME_FIRST_UPDATE)
#define DEBUG_LEVEL_3               (DEBUG_LEVEL_2 | DEBUG_RESET | DEBUG_INIT_DISPLAY | DEBUG_BLANK | DEBUG_FREE_GPIOS | DEBUG_VERIFY_GPIOS | DEBUG_BACKLIGHT | DEBUG_SYSFS)
#define DEBUG_LEVEL_4               (DEBUG_LEVEL_2 | DEBUG_FB_READ | DEBUG_FB_WRITE | DEBUG_FB_FILLRECT | DEBUG_FB_COPYAREA | DEBUG_FB_IMAGEBLIT | DEBUG_FB_BLANK)
#define DEBUG_LEVEL_5               (DEBUG_LEVEL_3 | DEBUG_UPDATE_DISPLAY)
#define DEBUG_LEVEL_6               (DEBUG_LEVEL_4 | DEBUG_LEVEL_5)
#define DEBUG_LEVEL_7               0xFFFFFFFF

#define DEBUG_DRIVER_INIT_FUNCTIONS (1<<3)
#define DEBUG_TIME_FIRST_UPDATE     (1<<4)
#define DEBUG_TIME_EACH_UPDATE      (1<<5)
#define DEBUG_DEFERRED_IO           (1<<6)
#define DEBUG_FBTFT_INIT_FUNCTIONS  (1<<7)

/* fbops */
#define DEBUG_FB_READ               (1<<8)   /* not in use */
#define DEBUG_FB_WRITE              (1<<9)
#define DEBUG_FB_FILLRECT           (1<<10)
#define DEBUG_FB_COPYAREA           (1<<11)
#define DEBUG_FB_IMAGEBLIT          (1<<12)
#define DEBUG_FB_SETCOLREG          (1<<13)
#define DEBUG_FB_BLANK              (1<<14)

#define DEBUG_SYSFS                 (1<<16)

/* fbtftops */
#define DEBUG_BACKLIGHT             (1<<17)
#define DEBUG_READ                  (1<<18)  /* not in use */
#define DEBUG_WRITE                 (1<<19)
#define DEBUG_WRITE_VMEM            (1<<20)
#define DEBUG_WRITE_DATA_COMMAND    (1<<21)
#define DEBUG_SET_ADDR_WIN          (1<<22)
#define DEBUG_RESET                 (1<<23)
#define DEBUG_MKDIRTY               (1<<24)  /* not in use */
#define DEBUG_UPDATE_DISPLAY        (1<<25)
#define DEBUG_INIT_DISPLAY          (1<<26)
#define DEBUG_BLANK                 (1<<27)
#define DEBUG_REQUEST_GPIOS         (1<<28)
#define DEBUG_FREE_GPIOS            (1<<29)
#define DEBUG_REQUEST_GPIOS_MATCH   (1<<30)
#define DEBUG_VERIFY_GPIOS          (1<<31)


#define MODULE_PARM_DEBUG                                                                  \
        static unsigned long debug = 0;                                                    \
        module_param(debug, ulong , 0664);                                                 \
        MODULE_PARM_DESC(debug,"level: 0-7 (the remaining 29 bits is for advanced usage)");

#define fbtft_debug_init(par) \
        par->debug = &debug

#define fbtft_debug_expand_shorthand(debug)       \
		switch (*debug & 0b111) {                 \
		case 1:  *debug |= DEBUG_LEVEL_1; break;  \
		case 2:  *debug |= DEBUG_LEVEL_2; break;  \
		case 3:  *debug |= DEBUG_LEVEL_3; break;  \
		case 4:  *debug |= DEBUG_LEVEL_4; break;  \
		case 5:  *debug |= DEBUG_LEVEL_5; break;  \
		case 6:  *debug |= DEBUG_LEVEL_6; break;  \
		case 7:  *debug = 0xFFFFFFFF; break;      \
		}

#define fbtft_pr_debug(fmt, ...) \
        fbtft_debug_expand_shorthand(&debug); \
        if (debug & DEBUG_DRIVER_INIT_FUNCTIONS) { pr_info(fmt, ##__VA_ARGS__); }

/* used in drivers */
#define fbtft_dev_dbg(level, dev, format, arg...) \
        if (debug & level) { dev_info(dev, format, ##arg); }

/* used in the fbtft module */
#define fbtft_fbtft_dev_dbg(level, par, dev, format, arg...) \
        if (*par->debug & level) { dev_info(dev, format, ##arg); }

/* only used in the fbtft module */
extern void _fbtft_dev_dbg_hex(const struct device *dev, int groupsize, void *buf, size_t len, const char *fmt, ...);
#define fbtft_dev_dbg_hex(level, par, dev, type, buf, num, format, arg...) \
        if (*par->debug & level) { _fbtft_dev_dbg_hex(dev, sizeof(type), buf, num * sizeof(type), format, ##arg); }

#define fbtft_debug_sync_value(par)                        \
        if (*par->debug != par->current_debug) {           \
            fbtft_debug_expand_shorthand(par->debug)       \
            par->current_debug = *par->debug;              \
        }

#endif /* __LINUX_FBTFT_H */
