load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//lib:sets.bzl", "sets")

visibility("//vendor/oplus/kernel/charger/bazel/...")

def _gen_oplus_chg_ic_cfg_impl(ctx):
    #output_path = ctx.actions.declare_directory("{}".format(ctx.attr.name))
    output_file = []

    args = ctx.actions.args()

    if ctx.attr.header:
        file = ctx.actions.declare_file("{}/oplus_chg_ic_cfg.h".format(ctx.attr.name))
        args.add("-hd", "{}".format(file.path))
        output_file.append(file)

    if ctx.attr.auto_source:
        file = ctx.actions.declare_file("{}/oplus_chg_ic_cfg_str.h".format(ctx.attr.name))
        args.add("-s", "{}".format(file.path))
        output_file.append(file)

    if ctx.attr.auto_debug:
        file = ctx.actions.declare_file("{}/oplus_chg_ic_auto_debug.h".format(ctx.attr.name))
        args.add("-df", "{}".format(file.path))
        output_file.append(file)

    if ctx.attr.markdown:
        file = ctx.actions.declare_file("{}/oplus_chg_ic_cfg.md".format(ctx.attr.name))
        args.add("-md", "{}".format(file.path))
        output_file.append(file)

    if ctx.attr.merge:
        file = ctx.actions.declare_file("{}/oplus_chg_ic_cfg.json".format(ctx.attr.name))
        args.add("-m", "{}".format(file.path))
        output_file.append(file)

    input_files = ctx.files.input_file
    ic_def_files = ctx.files.ic_def_file
    args.add(input_files[0])

    ctx.actions.run(
        inputs = input_files + ic_def_files,
        outputs = output_file,
        executable = ctx.executable.gen_script,
        arguments = [args],
        progress_message = "Generating oplus charge IC file",
    )

    return [
        DefaultInfo(files = depset(output_file)),
    ]

oplus_chg_ic_cfg = rule(
    implementation = _gen_oplus_chg_ic_cfg_impl,
    attrs = {
        "input_file": attr.label_list(allow_files=True),
        "ic_def_file": attr.label_list(allow_files=True),
        "header": attr.bool(),
        "auto_source": attr.bool(),
        "auto_debug": attr.bool(),
        "markdown": attr.bool(),
        "merge": attr.bool(),
        "gen_script": attr.label(
            default = ":v2/scripts/ic_cfg_parse",
            executable = True,
            cfg = "exec",
        ),
    },
)
