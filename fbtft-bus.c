#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include "fbtft.h"




/**********************************************************************************
 *
 *   void (*write_reg)(struct fbtft_par *par, int len, ...);
 *
 **********************************************************************************/

#define define_fbtft_write_reg(func, type, modifier)                                                             \
void func(struct fbtft_par *par, int len, ...)                                                                   \
{                                                                                                                \
    va_list args;                                                                                                \
    int i, ret;                                                                                                  \
    int offset = 0;                                                                                              \
    type *buf = (type *)par->buf;                                                                                \
                                                                                                                 \
    if (*par->debug & DEBUG_WRITE_DATA_COMMAND) {                                                                \
        va_start(args, len);                                                                                     \
        for (i=0;i<len;i++) {                                                                                    \
            buf[i] = (type)va_arg(args, unsigned int);                                                           \
        }                                                                                                        \
        va_end(args);                                                                                            \
        fbtft_dev_dbg_hex(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, type, buf, len, "%s: ", __func__);   \
    }                                                                                                            \
                                                                                                                 \
    va_start(args, len);                                                                                         \
                                                                                                                 \
    if(par->startbyte) {                                                                                         \
        *(u8 *)par->buf = par->startbyte;                                                                        \
        buf = (type *)(par->buf + 1);                                                                            \
        offset = 1;                                                                                              \
    }                                                                                                            \
                                                                                                                 \
    *buf = modifier((type)va_arg(args, unsigned int));                                                           \
    if (par->gpio.dc != -1)                                                                                      \
        gpio_set_value(par->gpio.dc, 0);                                                                         \
    ret = par->fbtftops.write(par, par->buf, sizeof(type)+offset);                                               \
    if (ret < 0) {                                                                                               \
        va_end(args);                                                                                            \
        dev_err(par->info->device, "%s: write() failed and returned %d\n", __func__, ret);                       \
        return;                                                                                                  \
    }                                                                                                            \
    len--;                                                                                                       \
                                                                                                                 \
    if(par->startbyte)                                                                                           \
        *(u8 *)par->buf = par->startbyte | 0x2;                                                                  \
                                                                                                                 \
    if (len) {                                                                                                   \
        i = len;                                                                                                 \
        while (i--) {                                                                                            \
            *buf++ = modifier((type)va_arg(args, unsigned int));                                                 \
        }                                                                                                        \
        if (par->gpio.dc != -1)                                                                                  \
            gpio_set_value(par->gpio.dc, 1);                                                                     \
        ret = par->fbtftops.write(par, par->buf, len * (sizeof(type)+offset));                                   \
        if (ret < 0) {                                                                                           \
            va_end(args);                                                                                        \
            dev_err(par->info->device, "%s: write() failed and returned %d\n", __func__, ret);                   \
            return;                                                                                              \
        }                                                                                                        \
    }                                                                                                            \
    va_end(args);                                                                                                \
}                                                                                                                \
EXPORT_SYMBOL(func);

define_fbtft_write_reg(fbtft_write_reg8_bus8, u8, )
define_fbtft_write_reg(fbtft_write_reg16_bus8, u16, cpu_to_be16)
define_fbtft_write_reg(fbtft_write_reg16_bus16, u16, )




/**********************************************************************************
 *
 *   void (*write_data_command)(struct fbtft_par *par, unsigned dc, u32 val);
 *
 **********************************************************************************/

/* 8-bit register over 8-bit databus */
void fbtft_write_data_command8_bus8(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, dc);

	*par->buf = (u8)val;

	ret = par->fbtftops.write(par, par->buf, 1);
	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}
EXPORT_SYMBOL(fbtft_write_data_command8_bus8);

/* 8-bit register/data over 9-bit SPI: dc + 8-bit */
void fbtft_write_data_command8_bus9(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	*(u16 *)par->buf = dc ? 0x100 | (u8)val : (u8)val;

	ret = par->fbtftops.write(par, par->buf, 2);
	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}
EXPORT_SYMBOL(fbtft_write_data_command8_bus9);

void fbtft_write_data_command16_bus16(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, dc);

	*(u16 *)par->buf = (u16)val;

	ret = par->fbtftops.write(par, par->buf, 2);
	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}
EXPORT_SYMBOL(fbtft_write_data_command16_bus16);

void fbtft_write_data_command16_bus8(struct fbtft_par *par, unsigned dc, u32 val)
{
	int ret;
	unsigned offset = 0;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_DATA_COMMAND, par, par->info->device, "%s: dc=%d, val=0x%X\n", __func__, dc, val);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, dc);

	if (par->startbyte) {
		if (dc)
			*(u8 *)par->buf = par->startbyte | 0x2;
		else
			*(u8 *)par->buf = par->startbyte;
		offset = 1;
	}

	*(u16 *)(par->buf + offset) = cpu_to_be16((u16)val);
	ret = par->fbtftops.write(par, par->buf, 2 + offset);
	if (ret < 0)
		dev_err(par->info->device, "%s: dc=%d, val=0x%X, failed with status %d\n", __func__, dc, val, ret);
}
EXPORT_SYMBOL(fbtft_write_data_command16_bus8);




/**********************************************************************************
 *
 *   int (*write_vmem)(struct fbtft_par *par);
 *
 **********************************************************************************/

/* 16 bit pixel over 8-bit databus */
int fbtft_write_vmem16_bus8(struct fbtft_par *par)
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

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 1);

	// non buffered write
	if (!par->txbuf.buf)
		return par->fbtftops.write(par, vmem8, len);

	// buffered write
	tx_array_size = par->txbuf.len;

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);

#ifdef __LITTLE_ENDIAN
		for (i=0;i<to_copy;i+=2) {
			txbuf8[i]    = vmem8[i+1];
			txbuf8[i+1]  = vmem8[i];
		}
#else
		for (i=0;i<to_copy;i++)
			txbuf8[i]    = vmem8[i];
#endif
		vmem8 = vmem8 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}
EXPORT_SYMBOL(fbtft_write_vmem16_bus8);

/* 16 bit pixel over 9-bit SPI bus: dc + high byte, dc + low byte */
int fbtft_write_vmem16_bus9(struct fbtft_par *par)
{
	u8 *vmem8;
	u16 *txbuf16 = par->txbuf.buf;
    size_t remain;
	size_t to_copy;
	size_t tx_array_size;
	int i;
	int ret = 0;
	size_t offset, len;

	if (!par->txbuf.buf) {
		dev_err(par->info->device, "%s: txbuf.buf is NULL\n", __func__);
		return -1;
	}

	offset = par->dirty_lines_start * par->info->fix.line_length;
	len = (par->dirty_lines_end - par->dirty_lines_start + 1) * par->info->fix.line_length;
	remain = len;
	vmem8 = par->info->screen_base + offset;

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_VMEM, par, par->info->device, "%s: offset=%d, len=%d\n", __func__, offset, len);

	tx_array_size = par->txbuf.len / 2;

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);

#ifdef __LITTLE_ENDIAN
		for (i=0;i<to_copy;i+=2) {
			txbuf16[i]   = 0x0100 | vmem8[i+1];
			txbuf16[i+1] = 0x0100 | vmem8[i];
		}
#else
		for (i=0;i<to_copy;i++)
			txbuf16[i]   = 0x0100 | vmem8[i];
#endif
		vmem8 = vmem8 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy*2);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}
EXPORT_SYMBOL(fbtft_write_vmem16_bus9);

int fbtft_write_vmem8_bus8(struct fbtft_par *par)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}
EXPORT_SYMBOL(fbtft_write_vmem8_bus8);

/* 16 bit pixel over 16-bit databus */
int fbtft_write_vmem16_bus16(struct fbtft_par *par)
{
	u16 *vmem16;
	u16 *txbuf16 = par->txbuf.buf;
	size_t remain16;
	size_t to_copy;
	size_t tx_array_size16;
	int i;
	int ret = 0;
	size_t offset, len;

	offset = par->dirty_lines_start * par->info->fix.line_length;
	len = (par->dirty_lines_end - par->dirty_lines_start + 1) * par->info->fix.line_length;
	remain16 = len / 2;
	vmem16 = (u16 *)(par->info->screen_base + offset);

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_VMEM, par, par->info->device, "%s: offset=%d, len=%d\n", __func__, offset, len);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 1);

	// non buffered write
	if (!par->txbuf.buf)
		return par->fbtftops.write(par, vmem16, len);

	// buffered write
	tx_array_size16 = par->txbuf.len / 2;

	while (remain16) {
		to_copy = remain16 > tx_array_size16 ? tx_array_size16 : remain16;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain16 - to_copy);

		for (i=0;i<to_copy;i++)
			txbuf16[i] = vmem16[i];

		vmem16 = vmem16 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy * 2);
		if (ret < 0)
			return ret;
		remain16 -= to_copy;
	}

	return ret;
}
EXPORT_SYMBOL(fbtft_write_vmem16_bus16);
