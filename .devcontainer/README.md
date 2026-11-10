# Dev container

Ubuntu 26.04 with GCC and Clang, CMake, Ninja, Node.js 22, Python 3, SDL2, and
the container CLI. On create it runs `scripts/setup.sh`, so the checkout comes
out gate-ready.

It is a **convenience, not the definition of the build**. The gate runs on a
plain machine against the versions listed in [../CODING.md](../CODING.md), and
the build must keep working there. If the two ever disagree, the plain machine
is right. [../docs/SETUP.md](../docs/SETUP.md) is the same environment built by
hand.

## Using it

Open the repository in any editor that understands `devcontainer.json`, or build
it directly:

```sh
docker build -t grandleon-dev .devcontainer
docker run --rm -it \
    -v "$PWD:/workspaces/grandleon" \
    -v /var/run/docker.sock:/var/run/docker.sock \
    grandleon-dev
```

## What is deliberately *not* baked in

**The console SDKs.** Console targets start their pinned toolchain containers as
siblings, not children. That is why the host socket is mounted. Two things
follow:

- the mounted socket grants the dev container control of the host daemon, which
  is effectively host root. Do not use this configuration on a machine where
  that matters.
- **run console targets from the host.** They bind-mount the checkout by the
  path they see, and the host daemon cannot resolve `/workspaces/grandleon`.
  Everything else in the container is unaffected.
