#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG; // module now applying "debug" parameter

#define DRVNAME		"fb_agm1264k-fl"
#define WIDTH		64
#define HEIGHT		64
#define TOTALWIDTH	(WIDTH * 2)	 // becouse 2 ks0108 in one display
#define FPS			5

static int init_display(struct fbtft_par *par)
{
    fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);
	
	
}

void reset(struct fbtft_par *par)
{
    if (par->gpio.reset == -1)
        return;
        
    fbtft_fbtft_dev_dbg(DEBUG_RESET, par, par->info->device, "%s()\n", __func__);
    
    gpio_set_value(par->gpio.reset, 0);
    udelay(20);
    gpio_set_value(par->gpio.reset, 1);
    mdelay(120);
}

// Check if all necessary GPIOS defined
static int verify_gpios(struct fbtft_par *par)
{
	int i;
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

    if (par->gpio.wr < 0) {
        dev_err(par->info->device, "Missing info about 'wr' gpio. Aborting.\n");
        return -EINVAL;
    }
    for (i = 0; i < 8; ++i)
	    if (par->gpio.db[i] < 0) {
    	    dev_err(par->info->device, "Missing info about 'db[%i]' gpio. Aborting.\n", i);
    	    return -EINVAL;
    	}
    if (par->gpio.aux[0] < 0) {
        dev_err(par->info->device, "Missing info about 'cs0' gpio. Aborting.\n");
        return -EINVAL;
    }
    if (par->gpio.aux[1] < 0) {
        dev_err(par->info->device, "Missing info about 'cs1' gpio. Aborting.\n");
        return -EINVAL;
    }
    if (par->gpio.aux[2] < 0) {
        dev_err(par->info->device, "Missing info about 'E' gpio. Aborting.\n");
        return -EINVAL;
    }
    if (par->gpio.aux[3] < 0) {
        dev_err(par->info->device, "Missing info about 'rs' gpio. Aborting.\n");
        return -EINVAL;
    }

    return 0;
}

static unsigned long 
fbtft_request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	int ret;
    long val;

    fbtft_fbtft_dev_dbg(DEBUG_REQUEST_GPIOS_MATCH, par, par->info->device, 
    	"%s('%s')\n", __func__, gpio->name);
    	
    if (strcasecmp(gpio->name, "cs0") == 0) { // left ks0108 controller pin
        par->gpio.aux[0] = gpio->gpio;
        return GPIOF_OUT_INIT_HIGH;
    }
    else if (strcasecmp(gpio->name, "cs1") == 0) { // right ks0108 controller pin
        par->gpio.aux[1] = gpio->gpio;
        return GPIOF_OUT_INIT_HIGH;
    }
    /* if write (rw = 0) e(1->0) perform write */
    /* if read (rw = 1) e(0->1) set data on D0-7*/
    else if (strcasecmp(gpio->name, "E") == 0) { 
        par->gpio.aux[2] = gpio->gpio;
        return GPIOF_OUT_INIT_LOW;
    }
    else if (strcasecmp(gpio->name, "rs") == 0) {
        par->gpio.aux[3] = gpio->gpio;
        return GPIOF_OUT_INIT_HIGH;
    }
	
	return FBTFT_GPIO_NO_MATCH;
}

/* эта штука используется для ввода команд
 * для упрощения используется макрос write_reg(par, ...)
 * первый байт будет значить для какого монитора команда (0 / 1)
 * второй и далее байты - команда/список команд
 * устанавливает rs = 0
 * управляет csx
 */
static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = (u8 *)par->buf;

	if (unlikely(par->debug & DEBUG_WRITE_REGISTER)) {
		va_start(args, len);
		for (i = 0; i < len; i++) {
			buf[i] = (u8)va_arg(args, unsigned int);
		}
		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device, u8, 
			buf, len, "%s: ", __func__);
	}

	va_start(args, len);

	*buf = (u8)va_arg(args, unsigned int);

	if (*buf > 1)
	{
		dev_err(par->info->device, "%s: Incorrect chip sellect request (%d)\n",
			__func__, *buf);
		goto _end_write_reg8_bus8;
	}
	
	gpio_set_value(par->gpio.aux[*buf], 0); // select chip
	gpio_set_value(par->gpio.aux[3], 0); // RS->0 (command mode)
	len--;

	if (len) {
		i = len;
		while (i--) {
			*buf++ = (u8)va_arg(args, unsigned int);
		}
		ret = par->fbtftops.write(par, par->buf, len * (sizeof(u8)));
		if (ret < 0) {
			va_end(args);
			dev_err(par->info->device, "%s: write() failed and returned %d\n",
				__func__, ret);
			return;
		}
	}
	
	/* csx off */
_end_write_reg8_bus8:
	gpio_set_value(par->gpio.aux[0], 1);
	gpio_set_value(par->gpio.aux[1], 1);

	va_end(args);
}

// сбросить (видимо тут данные влезают все в 1 старницу) указатели записи
static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	u8 i;
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	for (i = 0; i < 2; ++i)
	{
		write_reg(par, i, (1 << 6) | 0); // Sets the Y address at the Y address counter.
		write_reg(par, i, (0b10111 << 3) | 0); // Sets the X address at the X address register.
		write_reg(par, i, (0b11 << 6) | 0); // Indicates the display data RAM displayed at the top of the screen.
	}
}

int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int x, y, i;
	int ret = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s()\n", __func__);

	for (x = 0; x < par->info->var.xres; x++) {
		for (y = 0; y < par->info->var.yres/8; y++) {
			*buf = 0x00;
			// прегоняем пиксели в битовую маску
			for (i = 0; i < 8; i++)
				if (vmem16[(y * 8 + i) * par->info->var.xres + x])
					*buf |= 1 << i;
			buf++;
		}
	}

	/* Write data */
	gpio_set_value(par->gpio.dc, 1);
	ret = par->fbtftops.write(par, par->txbuf.buf,
				par->info->var.xres*par->info->var.yres/8);
	if (ret < 0)
		dev_err(par->info->device,
			"%s: write failed and returned: %d\n", __func__, ret);

	return ret;	
}

/* 
 * тупая запись, что пришло в массиве, то и записать
 * используется только шина par->gpio.db и par->gpio.E = latch
 * rs должна быть установлена до записи
 * CSx должна быть установлена до записи
 */
static int write(struct fbtft_par *par, void *buf, size_t len)
{
	u8 data;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	gpio_set_value(par->gpio.wr, 0); // set write mode

	while (len--) {
		u8 i;
	
		data = *(u8 *) buf;
		buf++;
		// set E
		gpio_set_value(par->gpio.aux[2], 1);
		
		// set data bus
		for (i = 0; i < 8; ++i)
			gpio_set_value(par->gpio.db[0], 1 << i);
		udelay(2);
		
		// unset e - write
		gpio_set_value(par->gpio.aux[2], 0);
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = TOTALWIDTH,
	.height = HEIGHT,
	.fps = FPS,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.verify_gpios = verify_gpios,
		.request_gpios_match = request_gpios_match,
		.reset = reset,
		.write = write,
		.write_register = write_reg8_bus8,
		.write_vmem = write_vmem,
	},
};
FBTFT_REGISTER_DRIVER(DRVNAME, "displaytronic,agm1264k-fl", &display);

module_init(easyfb_init);
module_exit(easyfb_exit);

MODULE_DESCRIPTION("Two KS0108 LCD controllers in AGM1264K-FL display");
MODULE_AUTHOR("ololoshka2871");
MODULE_LICENSE("GPL");

