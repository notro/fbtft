/*
 * FB driver for the ILI9341 LCD display controller
 *
 * This display uses 9-bit SPI: Data/Command bit + 8 data bits
 * For platforms that doesn't support 9-bit, the driver is capable of emulating this using 8-bit transfer.
 * This is done by transfering eight 9-bit words in 9 bytes.
 *
 * Copyright (C) 2013 Christian Vogelgsang
 * Based on adafruit22fb.c by Noralf Tronnes
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
#include <linux/vmalloc.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include "fbtft.h"

#define DRVNAME        "ili9341fb"
#define WIDTH       320
#define HEIGHT      240
#define BPP         16
#define FPS         10
#define TXBUFLEN    4*PAGE_SIZE

/* write_cmd and write_data transfers need to be buffered so we can, if needed, do 9-bit emulation */
#undef write_cmd
#undef write_data

static void ili9341fb_write_flush(struct fbtft_par *par, int count)
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

#define write_data(par, val)  p[i++] = 0x100 | val;
#define write_cmd(par, val)  p[i++] = val;
#define write_flush(par) { ili9341fb_write_flush(par, i); i=0; } while(0)

#define MEM_Y   (7) //MY row address order
#define MEM_X   (6) //MX column address order 
#define MEM_V   (5) //MV row / column exchange 
#define MEM_L   (4) //ML vertical refresh order
#define MEM_H   (2) //MH horizontal refresh order
#define MEM_BGR (3) //RGB-BGR Order 

#define LCD_CMD_RESET                  0x01
#define LCD_CMD_SLEEPOUT               0x11
#define LCD_CMD_DISPLAY_OFF            0x28
#define LCD_CMD_DISPLAY_ON             0x29
#define LCD_CMD_MEMACCESS_CTRL         0x36
#define LCD_CMD_PIXEL_FORMAT           0x3A
#define LCD_CMD_FRAME_CTRL             0xB1 //normal mode
#define LCD_CMD_DISPLAY_CTRL           0xB6
#define LCD_CMD_ENTRY_MODE             0xB7
#define LCD_CMD_POWER_CTRL1            0xC0
#define LCD_CMD_POWER_CTRL2            0xC1
#define LCD_CMD_VCOM_CTRL1             0xC5
#define LCD_CMD_VCOM_CTRL2             0xC7
#define LCD_CMD_POWER_CTRLA            0xCB
#define LCD_CMD_POWER_CTRLB            0xCF
#define LCD_CMD_POWERON_SEQ_CTRL       0xED
#define LCD_CMD_DRV_TIMING_CTRLA       0xE8
#define LCD_CMD_DRV_TIMING_CTRLB       0xEA
#define LCD_CMD_PUMP_RATIO_CTRL        0xF7

static int ili9341fb_init_display(struct fbtft_par *par)
{
    u16 *p = (u16 *)par->buf;
    int i = 0;

    dev_dbg(par->info->device, "%s()\n", __func__);

    par->fbtftops.reset(par);

    /* startup sequence for Watterott's MI0283QT9 */

#if 0
    // not needed
    write_cmd(par, LCD_CMD_RESET);
    write_flush(par);
    mdelay(120);
#endif
    
    write_cmd(par, LCD_CMD_DISPLAY_OFF);
    write_flush(par);
    mdelay(20);

    //send init commands
    write_cmd(par, LCD_CMD_POWER_CTRLB);
    write_data(par, 0x00);
    write_data(par, 0x83); //83 81 AA
    write_data(par, 0x30);

    write_cmd(par, LCD_CMD_POWERON_SEQ_CTRL);
    write_data(par, 0x64); //64 67
    write_data(par, 0x03);
    write_data(par, 0x12);
    write_data(par, 0x81);

    write_cmd(par, LCD_CMD_DRV_TIMING_CTRLA);
    write_data(par, 0x85);
    write_data(par, 0x01);
    write_data(par, 0x79); //79 78

    write_cmd(par, LCD_CMD_POWER_CTRLA);
    write_data(par, 0x39);
    write_data(par, 0X2C);
    write_data(par, 0x00);
    write_data(par, 0x34);
    write_data(par, 0x02);

    write_cmd(par, LCD_CMD_PUMP_RATIO_CTRL);
    write_data(par, 0x20);

    write_cmd(par, LCD_CMD_DRV_TIMING_CTRLB);
    write_data(par, 0x00);
    write_data(par, 0x00);

    write_cmd(par, LCD_CMD_POWER_CTRL1);
    write_data(par, 0x26); //26 25
  
    write_cmd(par, LCD_CMD_POWER_CTRL2);
    write_data(par, 0x11);

    write_cmd(par, LCD_CMD_VCOM_CTRL1);
    write_data(par, 0x35);
    write_data(par, 0x3E);

    write_cmd(par, LCD_CMD_VCOM_CTRL2);
    write_data(par, 0xBE); //BE 94

    write_cmd(par, LCD_CMD_FRAME_CTRL);
    write_data(par, 0x00);
    write_data(par, 0x1B); //1B 70

    write_cmd(par, LCD_CMD_DISPLAY_CTRL);
    write_data(par, 0x0A);
    write_data(par, 0x82);
    write_data(par, 0x27);
    write_data(par, 0x00);

    write_cmd(par, LCD_CMD_ENTRY_MODE);
    write_data(par, 0x07);

    write_cmd(par, LCD_CMD_PIXEL_FORMAT);
    write_data(par, 0x55); //16bit

    // orientation
    write_cmd(par, LCD_CMD_MEMACCESS_CTRL);
    write_data(par, (1<<MEM_BGR) | (1<<MEM_X) | (1<<MEM_Y) | (1<<MEM_V));

    write_cmd(par, LCD_CMD_SLEEPOUT);
    write_flush(par);
    mdelay(120);
    
    write_cmd(par, LCD_CMD_DISPLAY_ON);
    write_flush(par);
    mdelay(20);

    return 0;
}

void ili9341fb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
    uint8_t xsl, xsh, xel, xeh, ysl, ysh, yel, yeh;
    u16 *p = (u16 *)par->buf;
    int i = 0;

    dev_dbg(par->info->device, "%s(%d, %d, %d, %d)\n", __func__, xs, ys, xe, ye);

    xsl = (uint8_t)(xs & 0xff);
    xsh = (uint8_t)((xs >> 8) & 0xff);
    xel = (uint8_t)(xe & 0xff);
    xeh = (uint8_t)((xe >> 8) & 0xff);

    ysl = (uint8_t)(ys & 0xff);
    ysh = (uint8_t)((ys >> 8) & 0xff);
    yel = (uint8_t)(ye & 0xff);
    yeh = (uint8_t)((ye >> 8) & 0xff);

    write_cmd(par, FBTFT_CASET);
    write_data(par, xsh);
    write_data(par, xsl);
    write_data(par, xeh);
    write_data(par, xel);

    write_cmd(par, FBTFT_RASET);
    write_data(par, ysh);
    write_data(par, ysl);
    write_data(par, yeh);
    write_data(par, yel);

    write_cmd(par, FBTFT_RAMWR);

    write_flush(par);
}

static int ili9341fb_write_emulate_9bit(struct fbtft_par *par, void *buf, size_t len)
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

static unsigned long ili9341fb_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
    if (strcasecmp(gpio->name, "led") == 0) {
        par->gpio.led[0] = gpio->gpio;
        return GPIOF_OUT_INIT_LOW;
    }

    return FBTFT_GPIO_NO_MATCH;
}

int ili9341fb_blank(struct fbtft_par *par, bool on)
{
    if (par->gpio.led[0] == -1)
        return -EINVAL;

    dev_dbg(par->info->device, "%s(%s)\n", __func__, on ? "on" : "off");
    
    if (on)
        /* Turn off backlight */
        gpio_set_value(par->gpio.led[0], 0);
    else
        /* Turn on backlight */
        gpio_set_value(par->gpio.led[0], 1);

    return 0;
}

struct fbtft_display adafruit22_display = {
    .width = WIDTH,
    .height = HEIGHT,
    .bpp = BPP,
    .fps = FPS,
    .txbuflen = TXBUFLEN,
};

static int __devinit ili9341fb_probe(struct spi_device *spi)
{
    struct fb_info *info;
    struct fbtft_par *par;
    int ret;

    dev_dbg(&spi->dev, "%s()\n", __func__);

    info = fbtft_framebuffer_alloc(&adafruit22_display, &spi->dev);
    if (!info)
        return -ENOMEM;

    par = info->par;
    par->spi = spi;
    par->fbtftops.init_display = ili9341fb_init_display;
    par->fbtftops.request_gpios_match = ili9341fb_request_gpios_match;
    par->fbtftops.blank = ili9341fb_blank;
    par->fbtftops.write_data_command = fbtft_write_data_command8_bus9;
    par->fbtftops.write_vmem = fbtft_write_vmem16_bus9;
    par->fbtftops.set_addr_win = ili9341fb_set_addr_win;

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

        par->fbtftops.write = ili9341fb_write_emulate_9bit;
    }

    ret = fbtft_register_framebuffer(info);
    if (ret < 0)
        goto fbreg_fail;

    /* turn on backlight */
    ili9341fb_blank(par, false);

    return 0;

fbreg_fail:
    if (par->extra)
        vfree(par->extra);
    fbtft_framebuffer_release(info);

    return ret;
}

static int __devexit ili9341fb_remove(struct spi_device *spi)
{
    struct fb_info *info = spi_get_drvdata(spi);
    struct fbtft_par *par;

    dev_dbg(&spi->dev, "%s()\n", __func__);

    if (info) {
        fbtft_unregister_framebuffer(info);
        par = info->par;
        if (par->extra)
            vfree(par->extra);
        fbtft_framebuffer_release(info);
    }

    return 0;
}

static struct spi_driver ili9341fb_driver = {
    .driver = {
        .name   = DRVNAME,
        .owner  = THIS_MODULE,
    },
    .probe  = ili9341fb_probe,
    .remove = __devexit_p(ili9341fb_remove),
};

static int __init ili9341fb_init(void)
{
    pr_debug("\n\n"DRVNAME": %s()\n", __func__);
    return spi_register_driver(&ili9341fb_driver);
}

static void __exit ili9341fb_exit(void)
{
    pr_debug(DRVNAME": %s()\n", __func__);
    spi_unregister_driver(&ili9341fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(ili9341fb_init);
module_exit(ili9341fb_exit);

MODULE_DESCRIPTION("FB driver for the ILI 9341 LCD display controller");
MODULE_AUTHOR("Christian Vogelgsang");
MODULE_LICENSE("GPL");
