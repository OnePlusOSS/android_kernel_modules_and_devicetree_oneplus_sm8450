load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl",
    "define_oplus_ddk_module", "oplus_ddk_get_kernel_version",
    "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")
load(":kleaf-scripts/filter_target.bzl", "filter_deps_map")
load(":kleaf-scripts/version.bzl", "version_compare")

def define_oplus_cfg_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12") :
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12") :
            ddk_config = None

    if version_compare(kernel_version, "6.12") :
        define_oplus_ddk_module(
            name = "{}_oplus_cfg".format(target),
            out = "oplus_cfg.ko",
            srcs = native.glob([
                "config/**/*.h",
                "config/oplus_cfg.c"
            ]),
            includes = [
                "config"
            ],
            ko_deps = [],
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_oplus_cfg".format(target),
            out = "oplus_cfg.ko",
            srcs = native.glob([
                "config/**/*.h",
                "config/oplus_cfg.c"
            ]),
            includes = [
                "config"
            ],
            ko_deps = [],
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_OPLUS_DYNAMIC_CONFIG": "{}_oplus_cfg".format(target)
    }))

    ddk_headers(
        name = "oplus_cfg_headers",
        hdrs  = native.glob([
            "config/*.h"
        ]),
        includes = [
            "config",
        ]
    )

    return module_list
