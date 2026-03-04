load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl",
    "define_oplus_ddk_module", "oplus_ddk_get_kernel_version",
    "bazel_support_platform")
load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")
load(":kleaf-scripts/filter_target.bzl", "filter_deps_map")
load(":kleaf-scripts/version.bzl", "version_compare")

def define_ufcs_class_module():
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

    ddk_srcs = native.glob([
        "v2/ufcs/include/**/*.h",
        "v2/ufcs/ufcs_core.c",
        "v2/ufcs/ufcs_event.c",
        "v2/ufcs/ufcs_event_ctrl.c",
        "v2/ufcs/ufcs_event_data.c",
        "v2/ufcs/ufcs_event_vendor.c",
        "v2/ufcs/ufcs_intf.c",
        "v2/ufcs/ufcs_notify.c",
        "v2/ufcs/ufcs_policy_engine.c",
        "v2/ufcs/ufcs_pe_cable.c",
        "v2/ufcs/ufcs_pe_vendor.c",
        "v2/ufcs/ufcs_pe_test.c",
        "v2/ufcs/ufcs_sha256.c",
        "v2/ufcs/ufcs_timer.c",
    ])

    ddk_conditional_srcs = {
        "CONFIG_OPLUS_UFCS_CLASS_DEBUG": {
            True: [
                "v2/ufcs/ufcs_debug.c",
            ],
        },
    }

    ddk_includes = [
        "v2/ufcs/include",
        "v2/ufcs/include/internal"
    ]

    if version_compare(kernel_version, "6.12") :
        define_oplus_ddk_module(
            name = "{}_ufcs_class".format(target),
            out = "ufcs_class.ko",
            srcs = ddk_srcs,
            conditional_srcs = ddk_conditional_srcs,
            includes = ddk_includes,
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
            name = "{}_ufcs_class".format(target),
            out = "ufcs_class.ko",
            srcs = ddk_srcs,
            conditional_srcs = ddk_conditional_srcs,
            includes = ddk_includes,
            ko_deps = [],
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_OPLUS_UFCS_CLASS": "{}_ufcs_class".format(target)
    }))

    ddk_headers(
        name = "ufcs_class_headers",
        hdrs  = native.glob([
            "v2/ufcs/include/*.h"
        ]),
        includes = [
            "v2/ufcs/include",
        ]
    )

    return module_list
