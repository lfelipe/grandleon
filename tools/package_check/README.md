# Package check

`grandleon_package_check` opens a compiled `.gpk` and says whether the container
is sound.

```sh
cmake --build build --target grandleon_package_check
./build/grandleon_package_check build/grandleon-demo.gpk
```

```text
valid package: sections=11 revision=1024
```

| Exit code | Meaning |
|---|---|
| `0` | the package loads |
| `64` | wrong number of arguments |
| `65` | the package is invalid; the reason is printed by name |
| `66` | the file cannot be opened |

Forty-odd lines of `main.cpp` around one call to
`package_format::load_mock_package`. It is deliberately the smallest possible
consumer of that function, which is most of its value: if this tool accepts a
package, the envelope is readable by anything that links `package_format`, with
no other code in the way to have helped.

## What it checks, and what it does not

It checks the **container**: the `GLPK` magic, the container version, header and
total sizes, the engine-version compatibility range, target-profile
compatibility, required feature bits, required and unsupported sections, section
schema versions, the directory, duplicate sections, section bounds, the envelope
and per-section checksums, record validity, and duplicate record identifiers.

It does **not** decode payloads. No encounter is built, no campaign flow is
walked, no dialogue is read, and no presentation choice is resolved. Those live
in `engine/package_runtime`, and a package that passes here can still be refused
there.

The compatibility question is asked from one fixed position: engine version
`0.1.0`, `TargetProfile::desktop`, no feature bits required, at most 1,024
sections and a million records per section. So a "yes" from this tool is
specifically *this desktop build can open it*, not *every target can*. A package
built for a profile a console requires will fail here, correctly, and that is
the tool working.

## Where it fits

`grandleon_content_compile` writes a package, this reads one back, and
`grandleon_play` plays it. All three install together, so a game project that
depends on the SDK gets a way to check its own build output without linking
anything.
