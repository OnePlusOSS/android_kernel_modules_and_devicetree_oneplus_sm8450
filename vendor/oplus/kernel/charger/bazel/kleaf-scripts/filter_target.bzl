load(":kleaf-scripts/defconfig.bzl", "oplus_modules_get_config")


def get_deps_data(target, select, value):
    tmp = []
    if type(value) == "dict":
        if select in value:
            value = value[select]
        else:
            return tmp
    else:
        if select == False:
            return tmp

    if type(value) == "list":
        for item in value:
            tmp.append(item.replace("{target_variant}", "{}".format(target)))
    else:
        tmp.append(value.replace("{target_variant}", "{}".format(target)))

    return tmp


def filter_deps_map(target, deps):
    config = oplus_modules_get_config(target)
    if config == None:
        fail("target: \"{}\" not support".format(target))
        return []

    data = []
    for k, v in deps.items():
        if k not in config:
            data += get_deps_data(target, False, v)
            continue

        if config[k] == "y" or config[k] == "m":
            data += get_deps_data(target, True, v)
        elif config[k] == "n":
            data += get_deps_data(target, False, v)
        else:
            fail("config: \"{}\" value is not support".format(k))

    return data
