load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    define_oplus_ddk_module(
        name = "oplus_mdmrst",
        srcs = native.glob([
            "src/oplus_mdmrst.c",
        ]),
        includes = ["."],
        local_defines = ["OPLUS_FEATURE_RECORD_MDMRST"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_mdmrst",
        module_list = [
            "oplus_mdmrst",
        ],
    )
