# Console engine patches

Grandleon builds Nintendo 64 games with Pyrite and PlayStation games with
Mipsync. Each engine directory records the upstream repository and exact
revision in `upstream.env`. Its `series` file lists the patches in application
order.

The build applies the Mipsync series to a clean source checkout. The Nintendo
64 build also applies its patch to the engine source that the pinned Pyrite
CLI puts in the generated game project. Game-specific code lives under
`platform/`.

To check the PlayStation series against a local upstream checkout, run:

```sh
tools/upstream_patches/manage.sh verify mipsync
tools/upstream_patches/manage.sh check mipsync /path/to/Mipsync
```

Use `pyrite` for Nintendo 64. The check applies the series in a temporary
checkout and leaves the supplied checkout intact. See
[Setting up](../../docs/SETUP.md#console-builds) for the full build and test
commands.
