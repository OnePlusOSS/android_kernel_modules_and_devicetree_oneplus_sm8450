load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module",
    "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")

load(":kleaf-scripts/defconfig_fragment.bzl", "define_defconfig_fragment")
#load(":defconfig/config_define.bzl", "oplus_config")


def oplus_modules_get_qcom_config(pre_target, config_data):
    data = pre_target.split('_')
    if len(data) != 2:
        fail("target: \"{}\" format error".format(pre_target))
        return {}
    target = data[0]
    variant = data[1]

    if target not in config_data:
        print("target: \"{}\" not support".format(target))
        return {}

    config = config_data[target]
    if variant not in config:
        print("variant: \"{}\" not support".format(variant))
        return {}
    return config[variant]

def oplus_modules_get_mtk_config(pre_target, config_data):
    data = pre_target.split('_')
    if len(data) != 3:
        print("target data len: \"{}\" format error".format(pre_target))
        return {}
    target = data[0]
    variant = data[2]

    if target not in config_data:
        print("target: \"{}\" not support".format(target))
        return {}

    config = config_data[target]
    if variant not in config:
        print("variant: \"{}\" not support".format(variant))
        return {}
    return config[variant]

def oplus_modules_get_config(target):
    if bazel_support_platform == "qcom":
        return oplus_modules_get_qcom_config(target, oplus_config["qcom"])
    elif bazel_support_platform == "mtk":
        return oplus_modules_get_mtk_config(target, oplus_config["mtk"])
    else:
        fail("\"{}\" platform is not support".format(bazel_support_platform))
        return {}

def define_oplus_chg_defconfig(name, target):

    configs = oplus_modules_get_config(target)

    define_defconfig_fragment(
        name = "{}_{}_defconfig".format(name, target),
        out = "{}_{}.config".format(name, target),
        config = configs,
    )
