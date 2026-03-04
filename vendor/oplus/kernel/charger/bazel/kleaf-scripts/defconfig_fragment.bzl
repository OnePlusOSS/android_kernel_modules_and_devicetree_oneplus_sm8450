load("@bazel_skylib//rules:write_file.bzl", "write_file")

def define_defconfig_fragment(name, out, config):
    """Generate a defconfig fragment from the given configuration.

    This rule generates a defconfig fragment from a target/variant's
    configuration. Bazel needs to be mostly aware of the target/variant
    configuration in order to know how to generate module dependency graph
    as some modules will not exist in certain configurations. Thus, we let
    Bazel be the source of truth for the defconfig fragment which is passed
    to Kbuild during compilation.

    Note that this rule does not actually compile anything; it only writes
    files.

    Args:
      name: A unique name for this rule.
      out: The file to generate.
      config: A dictionary of key/value pairs that will be written as lines in the output file.
    """

    content = []
    for k, v in config.items():
        if v == "n":
            content.append("# {} is not set".format(k))
        else:
            content.append("{}={}".format(k, v))

    write_file(
        name,
        out,
        content,
    )
