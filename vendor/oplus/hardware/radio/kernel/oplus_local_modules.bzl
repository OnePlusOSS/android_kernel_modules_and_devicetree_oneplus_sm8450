load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target", "oplus_ddk_get_variant")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    bazel_support_target = oplus_ddk_get_target()
    if bazel_support_target == "canoe" :
        smem_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/smem".format(kernel_build_variant),
        ]
    else :
        smem_ko_deps = []

    define_oplus_ddk_module(
        name = "oplus_mdmfeature",
        srcs = native.glob([
            "mdmfeature/oplus_mdmfeature.h",
            "mdmfeature/oplus_mdmfeature.c",
        ]),
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_SYSTEM_KERNEL_MTK"],
            "qcom": ["CONFIG_OPLUS_SYSTEM_KERNEL_QCOM"],
        },
        copts = ["-DCONFIG_QCOM_SMEM"],
        includes = ["."],
        ko_deps = smem_ko_deps,
        local_defines = ["CONFIG_OPLUS_FEATURE_MDMFEATURE"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_hardware_radio",
        module_list = [
            "oplus_mdmfeature",
        ]
    )
