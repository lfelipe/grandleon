# Contributor environment for Grandleon.
#
# This pins the *host* toolchain only. The WebAssembly build deliberately runs
# in its own pinned Emscripten container and is not baked in here; see
# .devcontainer/README.md for how that works from inside this image.

FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

# Everything the host build needs is in the distribution's own repositories on
# 26.04, Node.js 22.22 included. Both compilers, because the gate builds under
# GCC and Clang and their warning sets differ; -Werror is only meaningfully
# verified against both.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        curl \
        git \
        gnupg \
        libsdl2-dev \
        ninja-build \
        nodejs \
        npm \
        python3 \
        python3-pip \
        python3-venv \
        sudo \
    && rm -rf /var/lib/apt/lists/*

# The container runtime CLI, so the pinned Emscripten and console builds can be
# driven from inside this container against the host daemon's mounted socket.
# The distribution's own `docker.io` would bring a daemon this image never runs,
# so the CLI-only package comes from Docker's repository, with the codename
# read from the base image rather than written down, so the two cannot drift.
RUN install -m 0755 -d /etc/apt/keyrings \
    && curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
        -o /etc/apt/keyrings/docker.asc \
    && chmod a+r /etc/apt/keyrings/docker.asc \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "${VERSION_CODENAME}") stable" \
        > /etc/apt/sources.list.d/docker.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends docker-ce-cli \
    && rm -rf /var/lib/apt/lists/*

# Chromium's shared libraries, so `playwright install chromium` needs no root
# later. The browser itself is installed per-checkout into .playwright-browsers/
# so that its version is governed by editor/package-lock.json rather than by
# this image.
RUN npx --yes playwright@latest install-deps chromium \
    && rm -rf /var/lib/apt/lists/* /root/.npm

ARG USERNAME=grandleon
ARG USER_UID=1001
ARG USER_GID=1001
RUN groupadd --gid "${USER_GID}" "${USERNAME}" \
    && useradd --uid "${USER_UID}" --gid "${USER_GID}" -m "${USERNAME}" \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}"

USER ${USERNAME}
WORKDIR /workspaces/grandleon
