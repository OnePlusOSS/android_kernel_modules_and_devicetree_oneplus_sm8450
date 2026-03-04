load("//build/kernel/oplus:oplus_modules_define.bzl",
    "bazel_support_platform")
load(":kleaf-scripts/kconfig.bzl", "define_oplus_chg_kconfig")
load(":kleaf-scripts/defconfig.bzl", "define_oplus_chg_defconfig")
load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")

def global_define():
    define_oplus_chg_defconfig(
        name = "oplus_chg",
        target = oplus_modules_get_target_variant()
    )
    define_oplus_chg_kconfig(name = "oplus_chg")
