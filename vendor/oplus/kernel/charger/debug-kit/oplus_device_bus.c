// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[DEBUG-KIT]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/regmap.h>
#include <linux/hashtable.h>
#include <linux/string.h>

#include "debug-kit.h"

#define ALL_REG_ADDR	(-1)

enum bus_fault_type {
	BUS_FAULT_READ_ERROR = 0,
	BUS_FAULT_WRITE_ERROR,
	BUS_FAULT_READ_DATA_ERROR,
	BUS_FAULT_WRITE_DATA_ERROR,
	BUS_FAULT_MAX,
};

static const char * const bus_fault_type_str[BUS_FAULT_MAX] = {
	"read", "write", "read_data", "write_data"
};

struct bus_fault_data {
	int addr;
	unsigned int type;
	int data[BUS_FAULT_MAX];
	struct hlist_node node;
};

struct bus_all_reg_fault_data {
	bool enable;
	unsigned int type;
	int data[BUS_FAULT_MAX];
};

struct oplus_device_bus {
	struct device dev;
	struct device *parent;
	struct regmap *regmap;
	const struct regmap_config *config;

	struct bus_all_reg_fault_data all_fault;

	DECLARE_HASHTABLE(hash_table, 8);
};

static struct class oplus_dev_bus_class;

static void oplus_device_bus_clean_all_fault(struct oplus_device_bus *odb)
{
	struct bus_fault_data *data;
	struct hlist_node *tmp;
	int i;

	odb->all_fault.enable = false;
	hash_for_each_safe(odb->hash_table, i, tmp, data, node) {
		hash_del(&data->node);
		devm_kfree(&odb->dev, data);
	}
}

static bool split_string(char *str, char *split_parts[]) {
	int i = 0;
	char *part, *tmp;

	tmp = str;
	part = strsep(&tmp, ":");
	while (part != NULL && i < 3) {
		split_parts[i] = part;
		i++;
		part = strsep(&tmp, ":");
	}

	if (i < 3)
		return false;
	return true;
}

static int split_data_string(char *str, char *split_parts[]) {
	int i = 0;
	char *part, *tmp;

	tmp = str;
	part = strsep(&tmp, ",");
	while (part != NULL && i < BUS_FAULT_MAX) {
		split_parts[i] = part;
		i++;
		part = strsep(&tmp, ",");
	}

	return i;
}

/* format: addr:type:data/- */
static int parse_inject_data(char *buf, int *addr, unsigned int *type, int *data, bool *remove)
{
	char *info[3] = { 0 };
	char *data_info[BUS_FAULT_MAX];
	long int num;
	int data_num;
	int i, n;
	int rc;

	if (!split_string(buf, info)) {
		pr_err("data format error\n");
		return -EINVAL;
	}
	pr_info("addr[%s]:type[%s]:data[%s]\n", info[0], info[1], info[2]);

	/* parse addr */
	if (strcmp(info[0], "all") == 0) {
		*addr = ALL_REG_ADDR;
	} else {
		rc = kstrtol(info[0], 0, &num);
		if (rc != 0) {
			pr_err("addr field format error");
			return -EINVAL;
		}
		*addr = (int)num;
	}

	/* parse type */
	rc = kstrtol(info[1], 0, &num);
	if (rc != 0) {
		pr_err("type field format error");
		return -EINVAL;
	}
	*type = (unsigned int)num;

	if (strcmp(info[2], "-") == 0) {
		*remove = true;
		return 0;
	}

	num = __builtin_popcount(*type);
	if ((num > BUS_FAULT_MAX) || (__fls(*type) >= BUS_FAULT_MAX)) {
		pr_err("type info error\n");
		return -EINVAL;
	}
	data_num = split_data_string(info[2], data_info);
	if (data_num != num) {
		pr_err("the number of data and the number of types do not match\n");
		return -EINVAL;
	}

	for (i = 0, n = 0; i < BUS_FAULT_MAX; i++) {
		if (!(*type & BIT(i)))
			continue;
		rc = kstrtol(data_info[n], 0, &num);
		if (rc != 0) {
			pr_err("data field format error");
			return -EINVAL;
		}
		data[i] = (int)num;
		n++;
	}

	return 0;
}

static void oplus_device_bus_remove_fault(struct oplus_device_bus *odb, int addr, unsigned int type)
{
	struct bus_fault_data *tmp;
	struct bus_fault_data *fault_data = NULL;

	if (addr == ALL_REG_ADDR) {
		odb->all_fault.type &= ~type;
		if (odb->all_fault.type == 0)
			odb->all_fault.enable = false;
		return;
	}

	hash_for_each_possible(odb->hash_table, tmp, node, addr) {
		if (tmp->addr == addr) {
			fault_data = tmp;
			break;
		}
	}
	if (fault_data == NULL)
		return;

	fault_data->type &= ~type;
	if (fault_data->type == 0) {
		hash_del(&fault_data->node);
		devm_kfree(&odb->dev, fault_data);
	}
}

static ssize_t fault_info_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct oplus_device_bus *odb = dev_get_drvdata(dev);
	struct bus_fault_data *data;
	struct hlist_node *tmp;
	int i, n;
	int size = 0;

	if (odb->all_fault.enable) {
		size += snprintf(buf + size, PAGE_SIZE - size, "[all]:[");

		/* type */
		for (n = 0; n < BUS_FAULT_MAX; n++) {
			if (!(odb->all_fault.type & BIT(n)))
				continue;
			size += snprintf(buf + size, PAGE_SIZE - size, "%s,", bus_fault_type_str[n]);
		}
		size--;
		size += snprintf(buf + size, PAGE_SIZE - size, "]:[");

		/* data */
		for (n = 0; n < BUS_FAULT_MAX; n++) {
			if (!(odb->all_fault.type & BIT(n)))
				continue;
			size += snprintf(buf + size, PAGE_SIZE - size, "%d,", odb->all_fault.data[n]);
		}
		size--;
		size += snprintf(buf + size, PAGE_SIZE - size, "]\n");
	}

	hash_for_each_safe(odb->hash_table, i, tmp, data, node) {
		size += snprintf(buf + size, PAGE_SIZE - size, "[0x%x]:[", data->addr);

		/* type */
		for (n = 0; n < BUS_FAULT_MAX; n++) {
			if (!(data->type & BIT(n)))
				continue;
			size += snprintf(buf + size, PAGE_SIZE - size, "%s,", bus_fault_type_str[n]);
		}
		size--;
		size += snprintf(buf + size, PAGE_SIZE - size, "]:[");

		/* data */
		for (n = 0; n < BUS_FAULT_MAX; n++) {
			if (!(data->type & BIT(n)))
				continue;
			size += snprintf(buf + size, PAGE_SIZE - size, "%d,", data->data[n]);
		}
		size--;
		size += snprintf(buf + size, PAGE_SIZE - size, "]\n");
	}

	return size;
}

static ssize_t fault_info_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct oplus_device_bus *odb = dev_get_drvdata(dev);
	bool remove;
	struct bus_fault_data *fault_data;
	struct bus_fault_data *tmp;
	char *tmp_buf;
	int rc;

	fault_data = devm_kzalloc(dev, sizeof(struct bus_fault_data), GFP_KERNEL);
	if (fault_data == NULL) {
		pr_err("alloc fault_data buf error\n");
		return -ENOMEM;
	}

	tmp_buf = devm_kzalloc(dev, count + 1, GFP_KERNEL);
	if (tmp_buf == NULL) {
		pr_err("alloc tmp_buf error\n");
		devm_kfree(dev, fault_data);
		return -ENOMEM;
	}
	memcpy(tmp_buf, buf, count);
	if (tmp_buf[count - 1] == '\n')
		tmp_buf[count - 1] = 0;

	rc = parse_inject_data(tmp_buf, &fault_data->addr, &fault_data->type, fault_data->data, &remove);
	if (rc < 0)
		goto err;

	if (remove) {
		oplus_device_bus_remove_fault(odb, fault_data->addr, fault_data->type);
		goto out;
	}
	if (fault_data->addr == ALL_REG_ADDR) {
		odb->all_fault.enable = true;
		odb->all_fault.type = fault_data->type;
		memcpy(odb->all_fault.data, fault_data->data, sizeof(int) * BUS_FAULT_MAX);
		goto out;
	}

	hash_for_each_possible(odb->hash_table, tmp, node, fault_data->addr) {
		if (tmp->addr == fault_data->addr) {
			tmp->type = fault_data->type;
			memcpy(tmp->data, fault_data->data, sizeof(int) * BUS_FAULT_MAX);
			goto out;
		}
	}

	INIT_HLIST_NODE(&fault_data->node);
	hash_add(odb->hash_table, &fault_data->node, fault_data->addr);

out:
	devm_kfree(dev, tmp_buf);
	devm_kfree(dev, fault_data);
	return count;

err:
	devm_kfree(dev, tmp_buf);
	devm_kfree(dev, fault_data);
	return rc;
}

static DEVICE_ATTR_RW(fault_info);

static struct device_attribute *oplus_device_bus_attributes[] = {
	&dev_attr_fault_info,
	NULL
};

static void oplus_device_bus_dev_release(struct device *dev)
{
	struct oplus_device_bus *odb = container_of(dev, struct oplus_device_bus, dev);
	kfree(odb);
}

struct oplus_device_bus *__oplus_device_bus_register(struct device *parent,
	struct regmap_config *config, const char *name)
{
	struct oplus_device_bus *odb;
	struct device *dev;
	struct device_attribute **attrs;
	struct device_attribute *attr;
	int rc;

	if (parent == NULL) {
		pr_err("parent is NULL\n");
		return ERR_PTR(-EINVAL);
	}
	if (config == NULL) {
		pr_err("config is NULL\n");
		return ERR_PTR(-EINVAL);
	}
	if (name == NULL) {
		pr_err("name is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	odb = kzalloc(sizeof(struct oplus_device_bus), GFP_KERNEL);
	if (odb == NULL) {
		pr_err("alloc oplus_device_bus struct buf error\n");
		return ERR_PTR(-ENOMEM);
	}
	odb->parent = parent;
	dev = &odb->dev;

	hash_init(odb->hash_table);

	odb->regmap = dev_get_regmap(parent, NULL);
	if (IS_ERR_OR_NULL(odb->regmap)) {
		pr_err("%s: regmap not fount\n", parent->kobj.name);
		rc = -EFAULT;
		goto oplus_dev_bus_err;
	}
	odb->config = config;

	device_initialize(dev);
	dev->parent = parent;
	dev->release = oplus_device_bus_dev_release;
	dev->class = &oplus_dev_bus_class;
	dev_set_drvdata(dev, odb);
	rc = dev_set_name(dev, "%s", name);
	if (rc < 0) {
		pr_err("set device name error, rc=%d\n", rc);
		goto set_dev_name_err;
	}

	rc = device_add(dev);
	if (rc) {
		pr_err("device add error, rc=%d\n", rc);
		goto device_add_failed;
	}

	attrs = oplus_device_bus_attributes;
	while ((attr = *attrs++)) {
		rc = device_create_file(&odb->dev, attr);
		if (rc) {
			pr_err("device create file fail, rc=%d\n", rc);
			goto device_create_file_err;
		}
	}

	return odb;

device_create_file_err:
	device_unregister(&odb->dev);
device_add_failed:
set_dev_name_err:
oplus_dev_bus_err:
	kfree(odb);
	return ERR_PTR(rc);
}

struct oplus_device_bus *oplus_device_bus_register(struct device *parent,
	struct regmap_config *config, const char *name)
{
	return __oplus_device_bus_register(parent, config, name);
}
EXPORT_SYMBOL(oplus_device_bus_register);

void oplus_device_bus_unregister(struct oplus_device_bus *odb)
{
	if (IS_ERR_OR_NULL(odb))
		return;
	oplus_device_bus_clean_all_fault(odb);
	device_unregister(&odb->dev);
}
EXPORT_SYMBOL(oplus_device_bus_unregister);

static void devm_oplus_device_bus_release(struct device *dev, void *res)
{
	struct oplus_device_bus **odb = res;

	oplus_device_bus_unregister(*odb);
}

static int devm_oplus_device_bus_match(struct device *dev, void *res, void *data)
{
	struct oplus_device_bus **this = res, *match = data;

	return (*this)->parent == match->parent;
}

struct oplus_device_bus *devm_oplus_device_bus_register(struct device *parent,
	struct regmap_config *config, const char *name)
{
	struct oplus_device_bus **ptr, *odb;

	ptr = devres_alloc(devm_oplus_device_bus_release, sizeof(*ptr), GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	odb = __oplus_device_bus_register(parent, config, name);
	if (IS_ERR_OR_NULL(odb)) {
		devres_free(ptr);
	} else {
		*ptr = odb;
		devres_add(parent, ptr);
	}
	return odb;
}
EXPORT_SYMBOL(devm_oplus_device_bus_register);

void devm_oplus_device_bus_unregister(struct oplus_device_bus *odb)
{
	if (IS_ERR_OR_NULL(odb))
		return;

	WARN_ON(devres_destroy(odb->parent, devm_oplus_device_bus_release,
			       devm_oplus_device_bus_match, odb));
}
EXPORT_SYMBOL(devm_oplus_device_bus_unregister);

static struct bus_fault_data *get_fault_data(struct oplus_device_bus *odb, int addr)
{
	struct bus_fault_data *tmp;

	hash_for_each_possible(odb->hash_table, tmp, node, addr) {
		if (tmp->addr == addr)
			return tmp;
	}

	return NULL;
}

int oplus_dev_bus_write(struct oplus_device_bus *odb, unsigned int reg,
			unsigned int val)
{
	struct bus_fault_data *fault_data;

	if (odb == NULL)
		return -EINVAL;

	if (odb->all_fault.enable) {
		if (odb->all_fault.type & BIT(BUS_FAULT_WRITE_ERROR))
			return odb->all_fault.data[BUS_FAULT_WRITE_ERROR];
		if (odb->all_fault.type & BIT(BUS_FAULT_WRITE_DATA_ERROR))
			return regmap_write(odb->regmap, reg, odb->all_fault.data[BUS_FAULT_WRITE_DATA_ERROR]);
	}

	fault_data = get_fault_data(odb, reg);
	if (fault_data == NULL)
		return regmap_write(odb->regmap, reg, val);

	if (fault_data->type & BIT(BUS_FAULT_WRITE_ERROR))
		return fault_data->data[BUS_FAULT_WRITE_ERROR];
	if (fault_data->type & BIT(BUS_FAULT_WRITE_DATA_ERROR))
		return regmap_write(odb->regmap, reg, fault_data->data[BUS_FAULT_WRITE_DATA_ERROR]);
	return regmap_write(odb->regmap, reg, val);
}
EXPORT_SYMBOL(oplus_dev_bus_write);

int oplus_dev_bus_raw_write(struct oplus_device_bus *odb, unsigned int reg,
				   const void *val, size_t val_len)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };
	int val_bits, val_bytes;
	int i;
	void *val_buf;
	int num;
	unsigned int reg_tmp = reg;
	int rc = 0;

	if (odb == NULL)
		return -EINVAL;
	val_bits = odb->config->val_bits;
	if (val_len % val_bits) {
		pr_err("val_len error, It must be an integer multiple of %d\n", val_bits);
		return -EINVAL;
	}
	num = val_len / val_bits;
	val_bytes = val_bits / 8;

	val_buf = devm_kzalloc(&odb->dev, val_len, GFP_KERNEL);
	if (val_buf == NULL) {
		pr_err("alloc val buf error\n");
		return -ENOMEM;
	}
	memcpy(val_buf, val, val_len);

	for (i = 0; i < num; i++, reg_tmp++) {
		if (odb->all_fault.enable) {
			tmp_fault_data.type = odb->all_fault.type;
			memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
			fault_data = &tmp_fault_data;
		} else {
			fault_data = get_fault_data(odb, reg_tmp);
		}
		if (fault_data == NULL)
			continue;
		if (fault_data->type & BIT(BUS_FAULT_WRITE_ERROR)) {
			rc = fault_data->data[BUS_FAULT_WRITE_ERROR];
			goto out;
		}
		if (fault_data->type & BIT(BUS_FAULT_WRITE_DATA_ERROR)) {
			switch (val_bytes) {
			case 1:
				*(u8 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 2:
				*(u16 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 4:
				*(u32 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#ifdef CONFIG_64BIT
			case 8:
				*(u64 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#endif
			default:
				rc = -EINVAL;
				goto out;
			}
		}
	}

	rc = regmap_raw_write(odb->regmap, reg, val_buf, val_len);
out:
	devm_kfree(&odb->dev, val_buf);
	return rc;
}
EXPORT_SYMBOL(oplus_dev_bus_raw_write);

int oplus_dev_bus_raw_write_async(struct oplus_device_bus *odb, unsigned int reg,
					 const void *val, size_t val_len)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };
	int val_bits, val_bytes;
	int i;
	void *val_buf;
	int num;
	unsigned int reg_tmp = reg;
	int rc = 0;

	if (odb == NULL)
		return -EINVAL;
	val_bits = odb->config->val_bits;
	if (val_len % val_bits) {
		pr_err("val_len error, It must be an integer multiple of %d\n", val_bits);
		return -EINVAL;
	}
	num = val_len / val_bits;
	val_bytes = val_bits / 8;

	val_buf = devm_kzalloc(&odb->dev, val_len, GFP_KERNEL);
	if (val_buf == NULL) {
		pr_err("alloc val buf error\n");
		return -ENOMEM;
	}
	memcpy(val_buf, val, val_len);

	for (i = 0; i < num; i++, reg_tmp++) {
		if (odb->all_fault.enable) {
			tmp_fault_data.type = odb->all_fault.type;
			memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
			fault_data = &tmp_fault_data;
		} else {
			fault_data = get_fault_data(odb, reg_tmp);
		}
		if (fault_data == NULL)
			continue;
		if (fault_data->type & BIT(BUS_FAULT_WRITE_ERROR)) {
			rc = fault_data->data[BUS_FAULT_WRITE_ERROR];
			goto out;
		}
		if (fault_data->type & BIT(BUS_FAULT_WRITE_DATA_ERROR)) {
			switch (val_bytes) {
			case 1:
				*(u8 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 2:
				*(u16 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 4:
				*(u32 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#ifdef CONFIG_64BIT
			case 8:
				*(u64 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#endif
			default:
				rc = -EINVAL;
				goto out;
			}
		}
	}

	rc = regmap_raw_write_async(odb->regmap, reg, val_buf, val_len);
out:
	devm_kfree(&odb->dev, val_buf);
	return rc;
}
EXPORT_SYMBOL(oplus_dev_bus_raw_write_async);

int oplus_dev_bus_bulk_write(struct oplus_device_bus *odb, unsigned int reg,
				    const void *val, size_t val_count)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };
	int val_bits, val_bytes;
	int i;
	void *val_buf;
	unsigned int reg_tmp = reg;
	int rc = 0;

	if (odb == NULL)
		return -EINVAL;
	val_bits = odb->config->val_bits;
	val_bytes = val_bits / 8;

	val_buf = devm_kzalloc(&odb->dev, val_count * val_bytes, GFP_KERNEL);
	if (val_buf == NULL) {
		pr_err("alloc val buf error\n");
		return -ENOMEM;
	}
	memcpy(val_buf, val, val_count * val_bytes);

	for (i = 0; i < val_count; i++, reg_tmp++) {
		if (odb->all_fault.enable) {
			tmp_fault_data.type = odb->all_fault.type;
			memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
			fault_data = &tmp_fault_data;
		} else {
			fault_data = get_fault_data(odb, reg_tmp);
		}
		if (fault_data == NULL)
			continue;
		if (fault_data->type & BIT(BUS_FAULT_WRITE_ERROR)) {
			rc = fault_data->data[BUS_FAULT_WRITE_ERROR];
			goto out;
		}
		if (fault_data->type & BIT(BUS_FAULT_WRITE_DATA_ERROR)) {
			switch (val_bytes) {
			case 1:
				*(u8 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 2:
				*(u16 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
			case 4:
				*(u32 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#ifdef CONFIG_64BIT
			case 8:
				*(u64 *)(val_buf + (i * val_bytes)) = fault_data->data[BUS_FAULT_WRITE_DATA_ERROR];
				break;
#endif
			default:
				rc = -EINVAL;
				goto out;
			}
		}
	}

	rc = regmap_bulk_write(odb->regmap, reg, val_buf, val_count);
out:
	devm_kfree(&odb->dev, val_buf);
	return rc;
}
EXPORT_SYMBOL(oplus_dev_bus_bulk_write);

int oplus_dev_bus_read(struct oplus_device_bus *odb, unsigned int reg,
			      unsigned int *val)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };

	if (odb == NULL)
		return -EINVAL;

	if (odb->all_fault.enable) {
		tmp_fault_data.type = odb->all_fault.type;
		memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
		fault_data = &tmp_fault_data;
	} else {
		fault_data = get_fault_data(odb, reg);
	}
	if (fault_data == NULL)
		return regmap_read(odb->regmap, reg, val);

	if (fault_data->type & BIT(BUS_FAULT_READ_ERROR))
		return fault_data->data[BUS_FAULT_READ_ERROR];
	if (fault_data->type & BIT(BUS_FAULT_READ_DATA_ERROR)) {
		*val = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
		return 0;
	}
	return regmap_read(odb->regmap, reg, val);
}
EXPORT_SYMBOL(oplus_dev_bus_read);

int oplus_dev_bus_raw_read(struct oplus_device_bus *odb, unsigned int reg,
				  void *val, size_t val_len)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };
	int val_bits, val_bytes;
	int i;
	int num;
	unsigned int reg_tmp = reg;
	int rc = 0;

	if (odb == NULL)
		return -EINVAL;
	val_bits = odb->config->val_bits;
	if (val_len % val_bits) {
		pr_err("val_len error, It must be an integer multiple of %d\n", val_bits);
		return -EINVAL;
	}
	num = val_len / val_bits;
	val_bytes = val_bits / 8;

	rc = regmap_raw_read(odb->regmap, reg, val, val_len);
	if (rc < 0)
		return rc;

	for (i = 0; i < num; i++, reg_tmp++) {
		if (odb->all_fault.enable) {
			tmp_fault_data.type = odb->all_fault.type;
			memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
			fault_data = &tmp_fault_data;
		} else {
			fault_data = get_fault_data(odb, reg_tmp);
		}
		if (fault_data == NULL)
			continue;
		if (fault_data->type & BIT(BUS_FAULT_READ_ERROR)) {
			rc = fault_data->data[BUS_FAULT_READ_ERROR];
			return rc;
		}
		if (fault_data->type & BIT(BUS_FAULT_READ_DATA_ERROR)) {
			switch (val_bytes) {
			case 1:
				*(u8 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
			case 2:
				*(u16 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
			case 4:
				*(u32 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
#ifdef CONFIG_64BIT
			case 8:
				*(u64 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
#endif
			default:
				return -EINVAL;
			}
		}
	}

	return rc;
}
EXPORT_SYMBOL(oplus_dev_bus_raw_read);

int oplus_dev_bus_bulk_read(struct oplus_device_bus *odb, unsigned int reg,
				   void *val, size_t val_count)
{
	struct bus_fault_data *fault_data;
	struct bus_fault_data tmp_fault_data = { 0 };
	int val_bits, val_bytes;
	int i;
	unsigned int reg_tmp = reg;
	int rc = 0;

	if (odb == NULL)
		return -EINVAL;
	val_bits = odb->config->val_bits;
	val_bytes = val_bits / 8;

	rc = regmap_bulk_read(odb->regmap, reg, val, val_count);
	if (rc < 0)
		return rc;

	for (i = 0; i < val_count; i++, reg_tmp++) {
		if (odb->all_fault.enable) {
			tmp_fault_data.type = odb->all_fault.type;
			memcpy(tmp_fault_data.data, odb->all_fault.data, sizeof(int) * BUS_FAULT_MAX);
			fault_data = &tmp_fault_data;
		} else {
			fault_data = get_fault_data(odb, reg_tmp);
		}
		if (fault_data == NULL)
			continue;
		if (fault_data->type & BIT(BUS_FAULT_READ_ERROR)) {
			rc = fault_data->data[BUS_FAULT_READ_ERROR];
			return rc;
		}
		if (fault_data->type & BIT(BUS_FAULT_READ_DATA_ERROR)) {
			switch (val_bytes) {
			case 1:
				*(u8 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
			case 2:
				*(u16 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
			case 4:
				*(u32 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
#ifdef CONFIG_64BIT
			case 8:
				*(u64 *)(val + (i * val_bytes)) = fault_data->data[BUS_FAULT_READ_DATA_ERROR];
				break;
#endif
			default:
				return -EINVAL;
			}
		}
	}

	return rc;
}
EXPORT_SYMBOL(oplus_dev_bus_bulk_read);

static struct attribute *oplus_dev_bus_class_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(oplus_dev_bus_class);

static __init int oplus_dev_bus_init(void)
{
	int rc;

	oplus_dev_bus_class.name = "oplus_dev_bus";
	oplus_dev_bus_class.class_groups = oplus_dev_bus_class_groups;
	rc = class_register(&oplus_dev_bus_class);
	if (rc < 0) {
		pr_err("Failed to create oplus_dev_bus_class, rc=%d\n", rc);
		return rc;
	}

	return 0;
}
module_init(oplus_dev_bus_init);

static __exit void oplus_dev_bus_exit(void)
{
	class_unregister(&oplus_dev_bus_class);
}
module_exit(oplus_dev_bus_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Oplus");
