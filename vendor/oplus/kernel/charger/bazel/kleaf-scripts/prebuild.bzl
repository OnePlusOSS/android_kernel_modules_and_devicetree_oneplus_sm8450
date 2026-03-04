load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load(":kleaf-scripts/gen_oplus_chg_ic_cfg.bzl", "oplus_chg_ic_cfg")

def oplus_chg_ic_prebuild(name):
    oplus_chg_ic_cfg(
        name = name + "_ic_cfg",
        input_file = native.glob([
            "v2/config/oplus_chg_ic.json"
        ]),
        ic_def_file = native.glob([
            "v2/config/ic/*.json"
        ]),
        header = True,
        auto_source = True,
        auto_debug = True,
        markdown = False,
        merge = False
    )

    ddk_headers(
        name = "{}_ic_cfg_headers".format(name),
        hdrs  = [
            ":{}_ic_cfg".format(name)
        ],
        includes = [
            "oplus_chg_ic_cfg",
        ]
    )
