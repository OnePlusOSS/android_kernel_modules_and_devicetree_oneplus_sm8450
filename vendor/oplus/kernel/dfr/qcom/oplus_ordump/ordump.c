/*===========================================================================
* Fulldump Back 2.0
*
* Store Full Ramdump To Pre-allocated File ONLY FOR QCOM Platform
*
* This file contains logical writing lbaooo to smem.
*
*
* Date:2023-2-17
============================================================================*/
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/soc/qcom/smem.h>

static int debug_magic = 0;
module_param(debug_magic, int, 0644);
MODULE_PARM_DESC(debug_magic, "debug magic number");

static unsigned long ordump_output_lbaooo = 0;

#define SMEM_OPLUS_RDUMP    124
#define ORDUMP_EN_MAGIC     69
#define ENABLE              1
#define DISABLE             0

struct dump_lbaooo {
    unsigned long lbaooo;
    unsigned int enable;
    int debug_magic;
};

static int __init ordump_sysfs_init(void)
{
    int ret = 0;
    struct dump_lbaooo *dump_lbaooo;
    size_t smem_size;
    unsigned int len = sizeof(struct dump_lbaooo);

    ret = qcom_smem_alloc(0, SMEM_OPLUS_RDUMP, len);
    if (ret < 0 && ret != -EEXIST) {
        pr_err("%s smem_alloc fail\n", __func__);
        return -EFAULT;
    }

    dump_lbaooo = (struct dump_lbaooo*)qcom_smem_get(0, SMEM_OPLUS_RDUMP, &smem_size);
    if (!IS_ERR(dump_lbaooo) && (dump_lbaooo->debug_magic == ORDUMP_EN_MAGIC)) {
        debug_magic = ENABLE;
        pr_info("%s: enable debug_magic .\n", __func__);
    }

    pr_info("%s: done.\n", __func__);
    return 0;
}

static int param_set_ordump_lbaooo(const char *val,
             const struct kernel_param *kp)
{
    int retval = 0;
    struct dump_lbaooo *dump_lbaooo;
    size_t smem_size;

    retval = param_set_ulong(val, kp);
    dump_lbaooo = (struct dump_lbaooo*)qcom_smem_get(0, SMEM_OPLUS_RDUMP, &smem_size);
    if (IS_ERR(dump_lbaooo)) {
        pr_err("%s smem_get fail\n", __func__);
        return -EFAULT;
    }

    if (ordump_output_lbaooo != 0) {
        pr_info("%s: OPLUS-RAMDUMP enabled,lbaooo = 0x%lx\n", __func__, ordump_output_lbaooo);
        dump_lbaooo->lbaooo = ordump_output_lbaooo;
        dump_lbaooo->enable = 1;
        dump_lbaooo->debug_magic = debug_magic;
    } else {
        pr_info("%s: OPLUS-RAMDUMP disabled!\n", __func__);
        dump_lbaooo->lbaooo = 0;
        dump_lbaooo->enable = 0;
        dump_lbaooo->debug_magic = 0;
    }
    return retval;
}

/**
 *        0644 : S_IRUGO | S_IWUSR
 *     IN TREE : /sys/module/ordump/parameters/lbaooo
 * OUT OF TREE : /sys/module/oplus_bsp_dfr_ordump/parameters/lbaooo
 */
struct kernel_param_ops param_ops_ordump_lbaooo = {
    .set = param_set_ordump_lbaooo,
    .get = param_get_ulong,
};

late_initcall(ordump_sysfs_init);

param_check_ulong(lbaooo, &ordump_output_lbaooo);
module_param_cb(lbaooo, &param_ops_ordump_lbaooo, &ordump_output_lbaooo, 0644);
__MODULE_PARM_TYPE(lbaooo, "unsigned long");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OPLUS RDUMP module");
MODULE_AUTHOR("Nick.Chen");
