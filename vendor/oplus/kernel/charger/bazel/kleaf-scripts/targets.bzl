load("//build/kernel/oplus:oplus_modules_define.bzl",
    "oplus_ddk_get_target",
    "oplus_ddk_get_variant")

def oplus_modules_get_target_variant():
    ddk_target = oplus_ddk_get_target()
    ddk_variant = oplus_ddk_get_variant()

    return "{}_{}".format(ddk_target, ddk_variant)
