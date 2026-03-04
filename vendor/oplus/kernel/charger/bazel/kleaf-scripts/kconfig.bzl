load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_genrule")

def define_oplus_chg_kconfig(name):
    hermetic_genrule(
        name = "kconfig.{}.generated".format(name),
        srcs = native.glob(["**/Kconfig*"]),
        outs = ["Kconfig.ext"],
        cmd = "KCONFIG_EXT_PREFIX=vendor/oplus/kernel/charger/bazel/ $(location kleaf-scripts/flatten_kconfig.sh) $(location Kconfig.ddk) >$@",
        tools = ["kleaf-scripts/flatten_kconfig.sh"],
    )
