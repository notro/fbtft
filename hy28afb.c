/*
 * FB driver for the HY28A SPI LCD display
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * Based on: https://github.com/topogigio/HY28A-LCDB-Drivers
 *



!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

The driver doesn't work correctly. Red and Blue color is swapped.
I have tried to set/clear bit 12 BGR in register Entry Mode (R03h), without luck.

And I can't swap the pixel bytes before transfer, like I do with all other SPI displays.
The register bytes is swapped though.

I haven't been able to make rotation work either.

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



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
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "hy28afb"
#define WIDTH       240
#define HEIGHT      320


#define SPI_START (0x70)   /* Start byte for SPI transfer */
#define SPI_RD (0x01)      /* WR bit 1 within start */
#define SPI_WR (0x00)      /* WR bit 0 within start */
#define SPI_DATA (0x02)    /* RS bit 1 within start byte */
#define SPI_INDEX (0x00)   /* RS bit 0 within start byte */


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;


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




static int read_spi(struct fbtft_par *par, void *txbuf, void *rxbuf, size_t len)
{
	int ret;
	struct spi_transfer	t = {
			.speed_hz = 2000000,
			.tx_buf		= txbuf,
			.rx_buf		= rxbuf,
			.len		= len,
		};
	struct spi_message	m;

	if (!par->spi) {
		dev_err(par->info->device, "%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}
	if (!rxbuf) {
		dev_err(par->info->device, "%s: rxbuf can't be NULL\n", __func__);
		return -1;
	}

	if (txbuf)
		fbtft_dev_dbg_hex(DEBUG_READ, par, par->info->device, u8, txbuf, len, "%s(len=%d) txbuf => ", __func__, len);

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(par->spi, &m);
	fbtft_dev_dbg_hex(DEBUG_READ, par, par->info->device, u8, rxbuf, len, "%s(len=%d) rxbuf <= ", __func__, len);
	return ret;
}


static unsigned read_devicecode(struct fbtft_par *par)
{
	int ret;
	u8 txbuf[8] = { SPI_START | SPI_RD | SPI_DATA, 0, 0, 0, };
	u8 rxbuf[8] = {0, };

	write_reg(par, 0x0000);

	ret = read_spi(par, txbuf, rxbuf, 4);

//	printk("Device code: 0x%02X, 0x%02X, 0x%02X, 0x%02X,  ret=%d\n", rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], ret);

	return (rxbuf[2] << 8) | rxbuf[3];
}



/*
 ILI9320 Datasheet
-------------------

7.3. Serial Peripheral Interface (SPI)


8.2.3. Start Oscillation (R00h) 
The device code “9320”h is read out when read this register

*/


static int hy28afb_init_display(struct fbtft_par *par)
{
	unsigned devcode;
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	devcode = read_devicecode(par);
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "Device code: 0x%04X\n", devcode);

    write_reg(par, 0x00,0x0000);
    write_reg(par, 0x01,0x0100); /* Driver Output Contral */
    write_reg(par, 0x02,0x0700); /* LCD Driver Waveform Contral */

	write_reg(par, 0x36, 0x1030);
//	write_reg(par, 0x36, 0x1008);

	/* Entry mode R03h */
/*
    switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x36, (!par->bgr << 12) | 0x30);
		break;
	case 1:
		write_reg(par, 0x36, (!par->bgr << 12) | 0x08);
		break;
	case 2:
		write_reg(par, 0x36, (!par->bgr << 12) | 0x00);
		break;
	case 3:
		write_reg(par, 0x36, (!par->bgr << 12) | 0x18);
		break;
	}
*/

    write_reg(par, 0x04,0x0000); /* Scalling Contral */
    write_reg(par, 0x08,0x0202); /* Display Contral 2 */
    write_reg(par, 0x09,0x0000); /* Display Contral 3 */
    write_reg(par, 0x0a,0x0000); /* Frame Cycle Contal */
    write_reg(par, 0x0c,(1<<0)); /* Extern Display Interface Contral 1 */
    write_reg(par, 0x0d,0x0000); /* Frame Maker Position */
    write_reg(par, 0x0f,0x0000); /* Extern Display Interface Contral 2 */
    mdelay(50);
    write_reg(par, 0x07,0x0101); /* Display Contral */
    mdelay(50);
    write_reg(par, 0x10,(1<<12)|(0<<8)|(1<<7)|(1<<6)|(0<<4)); /* Power Control 1 */
    write_reg(par, 0x11,0x0007);                              /* Power Control 2 */
    write_reg(par, 0x12,(1<<8)|(1<<4)|(0<<0));                /* Power Control 3 */
    write_reg(par, 0x13,0x0b00);                              /* Power Control 4 */
    write_reg(par, 0x29,0x0000);                              /* Power Control 7 */
    write_reg(par, 0x2b,(1<<14)|(1<<4));

    write_reg(par, 0x50,0);       /* Set X Start */
    write_reg(par, 0x51,239);     /* Set X End */
    write_reg(par, 0x52,0);       /* Set Y Start */
    write_reg(par, 0x53,319);     /* Set Y End */
    mdelay(50);

    write_reg(par, 0x60,0x2700); /* Driver Output Control */
    write_reg(par, 0x61,0x0001); /* Driver Output Control */ 
    write_reg(par, 0x6a,0x0000); /* Vertical Scroll Control */

    write_reg(par, 0x80,0x0000); /* Display Position? Partial Display 1 */
    write_reg(par, 0x81,0x0000); /* RAM Address Start? Partial Display 1 */
    write_reg(par, 0x82,0x0000); /* RAM Address End-Partial Display 1 */
    write_reg(par, 0x83,0x0000); /* Display Position? Partial Display 2 */
    write_reg(par, 0x84,0x0000); /* RAM Address Start? Partial Display 2 */
    write_reg(par, 0x85,0x0000); /* RAM Address End? Partial Display 2 */

    write_reg(par, 0x90,(0<<7)|(16<<0)); /* Frame Cycle Contral */
    write_reg(par, 0x92,0x0000);         /* Panel Interface Contral 2 */
    write_reg(par, 0x93,0x0001);         /* Panel Interface Contral 3 */
    write_reg(par, 0x95,0x0110);         /* Frame Cycle Contral */
    write_reg(par, 0x97,(0<<8));
    write_reg(par, 0x98,0x0000);         /* Frame Cycle Contral */
    write_reg(par, 0x07,0x0133);

    mdelay(100);   /* mdelay 50 ms */

	return 0;
}

static void hy28afb_write_reg_data_command(struct fbtft_par *par, int len, ...)                                               \
{
	va_list args;
	int i;
	u16 *buf = (u16 *)par->buf;

	if (unlikely(*par->debug & DEBUG_WRITE_DATA_COMMAND)) {
		va_start(args, len);
		for (i=0;i<len;i++) {
			buf[i] = (u16)va_arg(args, unsigned int);
		}
		va_end(args);
		fbtft_dev_dbg_hex(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, u16, buf, len, "%s: ", __func__);
	}

	va_start(args, len);

	write_cmd(par, (u16)va_arg(args, unsigned int));
	len--;

	if (len) {
		i = len;
		while (i--) {
			write_data(par, (u16)va_arg(args, unsigned int));
		}
	}
	va_end(args);
}

void hy28afb_write_data_command16_bus8(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	if (dc)
		*(u8 *)par->buf = SPI_START | SPI_WR | SPI_DATA;
	else
		*(u8 *)par->buf = SPI_START | SPI_WR | SPI_INDEX;

	*(u16 *)(par->buf + 1) = cpu_to_be16((u16)val);

	ret = par->fbtftops.write(par, par->buf, 3);
	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}

int hy28afb_write_vmem16_bus8(struct fbtft_par *par)
{
	u8 *vmem8;
	u8  *txbuf8  = par->txbuf.buf;
    size_t remain;
	size_t to_copy;
	size_t tx_array_size;
	int i;
	int ret = 0;
	size_t offset, len;

	offset = par->dirty_lines_start * par->info->fix.line_length;
	len = (par->dirty_lines_end - par->dirty_lines_start + 1) * par->info->fix.line_length;
	remain = len;
	vmem8 = par->info->screen_base + offset;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_VMEM, par, par->info->device, "%s: offset=%d, len=%d\n", __func__, offset, len);

	// sanity check
	if (!par->txbuf.buf) {
		dev_err(par->info->device, "Missing transmit buffer.\n");
		return -1;
	}

	// buffered write
	tx_array_size = par->txbuf.len - 2;

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);

		/* Start byte */
		txbuf8[0] = SPI_START | SPI_WR | SPI_DATA;

//#ifdef __LITTLE_ENDIAN
//		for (i=1;i<(to_copy+1);i+=2) {
//			txbuf8[i]    = vmem8[i+1];
//			txbuf8[i+1]  = vmem8[i];
//		}
//#else
		for (i=1;i<(to_copy+1);i++)
			txbuf8[i]    = vmem8[i];
//#endif
		vmem8 = vmem8 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy+1);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}

static void hy28afb_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_dev_dbg(DEBUG_SET_ADDR_WIN, par->info->device, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	write_reg(par, 0x0020, xs);
	write_reg(par, 0x0021, ys);
	write_reg(par, 0x0022); /* Write Data to GRAM */
}


struct fbtft_display hy28afb_display = {
	.width = WIDTH,
	.height = HEIGHT,
//	.width = HEIGHT,
//	.height = WIDTH,
};

static int hy28afb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&hy28afb_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	fbtft_debug_init(par);
	par->fbtftops.init_display = hy28afb_init_display;
	par->fbtftops.write_reg = hy28afb_write_reg_data_command;
	par->fbtftops.write_data_command = hy28afb_write_data_command16_bus8;
	par->fbtftops.write_vmem = hy28afb_write_vmem16_bus8;
	par->fbtftops.set_addr_win = hy28afb_set_addr_win;
	par->fbtftops.register_backlight = fbtft_register_backlight;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int hy28afb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver hy28afb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = hy28afb_probe,
	.remove = hy28afb_remove,
};

static int __init hy28afb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);
	return spi_register_driver(&hy28afb_driver);
}

static void __exit hy28afb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);
	spi_unregister_driver(&hy28afb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(hy28afb_init);
module_exit(hy28afb_exit);

MODULE_DESCRIPTION("FB driver for the HY28A SPI LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
