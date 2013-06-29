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
	unsigned long tmp_curves[FBTFT_GAMMA_MAX_VALUES_TOTAL];
	int ret = 0;
	int curve_counter, value_counter;

	if (!str || !curves)
		return -EINVAL;

	tmp = kmalloc(size+1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	memcpy(tmp, str, size+1);
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
			tmp_curves[curve_counter * par->gamma.num_values + value_counter] = val;
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

	mutex_lock(&par->gamma.lock);
	memcpy(curves, tmp_curves,
		par->gamma.num_curves * par->gamma.num_values * sizeof(tmp_curves[0]));
	mutex_unlock(&par->gamma.lock);

out:
	kfree(tmp);
	return ret;
}

unsigned long fbtft_gamma_get(struct fbtft_par *par, unsigned curve_index, unsigned value_index)
{
	unsigned long val;

	if (curve_index >= par->gamma.num_curves) {
		printk("curve_index=%d exceeds num_curves=%d\n", curve_index, par->gamma.num_curves);
		return 0;
	}
	if (value_index >= par->gamma.num_values) {
		printk("value_index=%d exceeds num_curves=%d\n", value_index, par->gamma.num_values);
		return 0;
	}
	mutex_lock(&par->gamma.lock);
	val = par->gamma.curves[curve_index * par->gamma.num_values + value_index];
	mutex_unlock(&par->gamma.lock);

	return val;
}
EXPORT_SYMBOL(fbtft_gamma_get);

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

void fbtft_gamma_apply_mask(struct fbtft_par *par)
{
	int i,j;

	mutex_lock(&par->gamma.lock);
	for (i=0;i<par->gamma.num_curves;i++) {
		for (j=0;j<par->gamma.num_values;j++) {
			par->gamma.curves[i*par->gamma.num_values + j] &= par->gamma.mask[i*par->gamma.num_values + j];
		}
	}
	mutex_unlock(&par->gamma.lock);
}

static ssize_t store_gamma_curve(struct device *device,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;
	int ret;

	ret = fbtft_gamma_parse_str(par, par->gamma.curves, buf, count);
	if (ret)
		return ret;
	fbtft_gamma_apply_mask(par);
	par->fbtftops.set_gamma(par);

	return count;
}

static ssize_t show_gamma_curve(struct device *device,
                                struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	return sprintf_gamma(par, par->gamma.curves, buf);
}

static ssize_t show_gamma_mask(struct device *device,
                               struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	return sprintf_gamma(par, par->gamma.mask, buf);
}

static struct device_attribute gamma_device_attrs[] = {
        __ATTR(gamma, S_IRUGO | S_IWUGO, show_gamma_curve, store_gamma_curve),
        __ATTR(gamma_mask, S_IRUGO, show_gamma_mask, NULL),
};


void fbtft_sysfs_init(struct fbtft_par *par)
{
	if (par->gamma.curves && par->fbtftops.set_gamma) {
		device_create_file(par->info->dev, &gamma_device_attrs[0]);
		device_create_file(par->info->dev, &gamma_device_attrs[1]);
	}
}

void fbtft_sysfs_exit(struct fbtft_par *par)
{
	if (par->gamma.curves && par->fbtftops.set_gamma) {
		device_remove_file(par->info->dev, &gamma_device_attrs[0]);
		device_remove_file(par->info->dev, &gamma_device_attrs[1]);
	}
}
