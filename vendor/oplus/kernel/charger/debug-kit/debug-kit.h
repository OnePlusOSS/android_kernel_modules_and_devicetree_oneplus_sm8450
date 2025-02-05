// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef __DEBUG_KIT_H__
#define __DEBUG_KIT_H__

#include <linux/regmap.h>

struct oplus_device_bus;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)

struct oplus_device_bus *oplus_device_bus_register(struct device *parent, struct
	regmap_config *config, const char *name);
void oplus_device_bus_unregister(struct oplus_device_bus *odb);
struct oplus_device_bus *devm_oplus_device_bus_register(struct device *parent,
	struct regmap_config *config, const char *name);
void devm_oplus_device_bus_unregister(struct oplus_device_bus *odb);

int oplus_dev_bus_write(struct oplus_device_bus *odb, unsigned int reg,
			unsigned int val);
int oplus_dev_bus_raw_write(struct oplus_device_bus *odb, unsigned int reg,
			    const void *val, size_t val_len);
int oplus_dev_bus_raw_write_async(struct oplus_device_bus *odb, unsigned int reg,
				  const void *val, size_t val_len);
int oplus_dev_bus_bulk_write(struct oplus_device_bus *odb, unsigned int reg,
			     const void *val, size_t val_count);
int oplus_dev_bus_read(struct oplus_device_bus *odb, unsigned int reg,
		       unsigned int *val);
int oplus_dev_bus_raw_read(struct oplus_device_bus *odb, unsigned int reg,
			   void *val, size_t val_len);
int oplus_dev_bus_bulk_read(struct oplus_device_bus *odb, unsigned int reg,
			    void *val, size_t val_count);

#else /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

static inline struct oplus_device_bus *oplus_device_bus_register(
	struct device *parent, struct regmap_config *config, const char *name)
{
	return PTR_ERR(-EINVAL);
}

static inline void oplus_device_bus_unregister(struct oplus_device_bus *odb)
{
}

static inline struct oplus_device_bus *devm_oplus_device_bus_register(
	struct device *parent, struct regmap_config *config, const char *name)
{
	return PTR_ERR(-EINVAL);
}

static inline void devm_oplus_device_bus_unregister(struct oplus_device_bus *odb)
{
}

static inline int oplus_dev_bus_write(struct oplus_device_bus *odb,
				      unsigned int reg, unsigned int val)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_raw_write(struct oplus_device_bus *odb,
					  unsigned int reg,
					  const void *val, size_t val_len)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_raw_write_async(struct oplus_device_bus *odb,
						unsigned int reg,
						const void *val, size_t val_len)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_bulk_write(struct oplus_device_bus *odb,
					   unsigned int reg,
					   const void *val, size_t val_count)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_read(struct oplus_device_bus *odb,
				     unsigned int reg,
				     unsigned int *val)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_raw_read(struct oplus_device_bus *odb,
					 unsigned int reg,
					 void *val, size_t val_len)
{
	return -EINVAL;
}

static inline int oplus_dev_bus_bulk_read(struct oplus_device_bus *odb,
					  unsigned int reg,
					  void *val, size_t val_count)
{
	return -EINVAL;
}

#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

#endif /* __DEBUG_KIT_H__ */
