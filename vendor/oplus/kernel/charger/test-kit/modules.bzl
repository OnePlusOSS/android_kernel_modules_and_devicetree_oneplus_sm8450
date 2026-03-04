load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module",
    "oplus_ddk_get_kernel_version",
    "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")
load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")
load(":kleaf-scripts/filter_target.bzl", "filter_deps_map")
load(":kleaf-scripts/version.bzl", "version_compare")

conditional_ko_deps = {
    "CONFIG_OPLUS_CHARGER_MTK": {
        True: [
            "//kernel_device_modules-{}/drivers/pinctrl/mediatek:pinctrl-mtk-v2".format(oplus_ddk_get_kernel_version()),
            "//kernel_device_modules-{}/drivers/pinctrl/mediatek:pinctrl-mtk-common-v2_debug".format(oplus_ddk_get_kernel_version()),
            "//kernel_device_modules-{}/drivers/pinctrl/mediatek:pinctrl-mt6993".format(oplus_ddk_get_kernel_version()),
        ],
    },
}

def define_test_kit_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    ko_deps = filter_deps_map(target, conditional_ko_deps)
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        ddk_header_deps = []
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12") :
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        ddk_header_deps = [
            "//kernel_device_modules-{}/drivers/pinctrl/mediatek:pinctrl_mtk_header".format(oplus_ddk_get_kernel_version()),
        ]
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12") :
            ddk_config = None

    if version_compare(kernel_version, "6.12") :
        define_oplus_ddk_module(
            name = "{}_test-kit".format(target),
            out = "test-kit.ko",
            srcs = native.glob([
                "test-kit/**/*.h",
                "test-kit/test-kit.c"
            ]),
            includes = [
                "test-kit"
            ],
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            copts = [
                "-I$(srctree)/drivers/gpio"
            ],
            hdrs = [
                ":oplus_chg_v2_headers"
            ],
            header_deps = ddk_header_deps,
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_test-kit".format(target),
            out = "test-kit.ko",
            srcs = native.glob([
                "test-kit/**/*.h",
                "test-kit/test-kit.c"
            ]),
            includes = [
                "test-kit"
            ],
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            copts = [
                "-I$(srctree)/drivers/gpio"
            ],
            hdrs = [
                ":oplus_chg_v2_headers"
            ],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_OPLUS_CHG_TEST_KIT": "{}_test-kit".format(target)
    }))

    ddk_headers(
        name = "test_kit_headers",
        hdrs  = native.glob([
            "test-kit/*.h"
        ]),
        includes = [
            "test-kit",
        ]
    )

    return module_list
