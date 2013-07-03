#include "fbtft.h"


static int get_next_ulong(char **str_p, unsigned long *val, char *sep, int base)
{
	char *p_val;
	int ret;

	if (!str_p || !(*str_p))
		return -EINVAL;

	p_val = strsep(str_p, sep);

	if (!p_val)
		return -EINVAL;

	ret = kstrtoul(p_val, base, val);
	if (ret)
		return -EINVAL;

	return 0;
}

int fbtft_gamma_parse_str(struct fbtft_par *par, unsigned long *curves, const char *str, int size)
{
	char *str_p, *curve_p=NULL;
	char *tmp;
	unsigned long val=0;
	int ret = 0;
	int curve_counter, value_counter;

	if (par->debug)
		fbtft_fbtft_dev_dbg(DEBUG_SYSFS, par, par->info->device, "%s() str=\n", __func__);

	if (!str || !curves)
		return -EINVAL;

	if (par->debug && (*par->debug & DEBUG_SYSFS))
		printk("%s\n", str);

	tmp = kmalloc(size+1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	memcpy(tmp, str, size+1);

	/* replace optional separators */
	str_p = tmp;
	while (*str_p) {
		if (*str_p == ',')
			*str_p = ' ';
		if (*str_p == ';')
			*str_p = '\n';
		str_p++;
	}

	str_p = strim(tmp);

	curve_counter = 0;
	while (str_p) {
		if (curve_counter == par->gamma.num_curves) {
			printk("Gamma: Too many curves\n");
			ret = -EINVAL;
			goto out;
		}
		curve_p = strsep(&str_p, "\n");
		value_counter = 0;
		while (curve_p) {
			if (value_counter == par->gamma.num_values) {
				printk("Gamma: Too many values\n");
				ret = -EINVAL;
				goto out;
			}
			ret = get_next_ulong(&curve_p, &val, " ", 16);
			if (ret) {
				goto out;
			}
			curves[curve_counter * par->gamma.num_values + value_counter] = val;
			value_counter++;
		}
		if (value_counter != par->gamma.num_values) {
			printk("Gamma: Too few values\n");
			ret = -EINVAL;
			goto out;
		}
		curve_counter++;
	}
	if (curve_counter != par->gamma.num_curves) {
		printk("Gamma: Too few curves\n");
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(tmp);
	return ret;
}

static ssize_t sprintf_gamma(struct fbtft_par *par, unsigned long *curves, char *buf)
{
	ssize_t len = 0;
	unsigned int i, j;

	mutex_lock(&par->gamma.lock);
	for (i = 0; i < par->gamma.num_curves; i++) {
		for (j = 0; j < par->gamma.num_values; j++)
			len += scnprintf(&buf[len], PAGE_SIZE,
				"%04lx ", curves[i*par->gamma.num_values + j]);
		buf[len-1] = '\n';
	}
	mutex_unlock(&par->gamma.lock);

	return len;
}

static ssize_t store_gamma_curve(struct device *device,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;
	unsigned long tmp_curves[FBTFT_GAMMA_MAX_VALUES_TOTAL];
	int ret;

	ret = fbtft_gamma_parse_str(par, tmp_curves, buf, count);
	if (ret)
		return ret;

	ret = par->fbtftops.set_gamma(par, tmp_curves);
	if (ret)
		return ret;

	mutex_lock(&par->gamma.lock);
	memcpy(par->gamma.curves, tmp_curves,
		par->gamma.num_curves * par->gamma.num_values * sizeof(tmp_curves[0]));
	mutex_unlock(&par->gamma.lock);

	return count;
}

static ssize_t show_gamma_curve(struct device *device,
                                struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	return sprintf_gamma(par, par->gamma.curves, buf);
}

static struct device_attribute gamma_device_attrs[] = {
        __ATTR(gamma, S_IRUGO | S_IWUGO, show_gamma_curve, store_gamma_curve),
};


void fbtft_sysfs_init(struct fbtft_par *par)
{
	if (par->gamma.curves && par->fbtftops.set_gamma)
		device_create_file(par->info->dev, &gamma_device_attrs[0]);
}

void fbtft_sysfs_exit(struct fbtft_par *par)
{
	if (par->gamma.curves && par->fbtftops.set_gamma)
		device_remove_file(par->info->dev, &gamma_device_attrs[0]);
}
