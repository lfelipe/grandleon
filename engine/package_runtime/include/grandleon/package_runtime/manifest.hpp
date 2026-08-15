// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <string_view>

namespace grandleon::package_runtime {

// What the project calls itself, read out of the package a console is holding.
//
// Every project has a name: `title` is required by
// `schemas/source/v1/project.schema.json`, and the compiler refuses a source
// whose title is empty (`tools/game_content/src/compiler.cpp`). It has been
// written into the manifest section's title record since the format's first
// version, so this reads packages nobody rebuilt.
//
// It exists because a name is the one thing a console screen says that belongs
// to the *game* rather than to the engine, and this is how a client holding
// only a package asks for it. Without it a console has nothing to draw but a
// name compiled into the binary: one binary telling every author's cartridge it
// was somebody else's game.
//
// The view borrows the package's own bytes and lives exactly as long as the
// package does. Nothing is copied and nothing is allocated, which is what lets
// a 64 KiB machine reading its package in place out of cartridge ROM ask this
// question at all. The bytes are **not** NUL-terminated, and a caller that
// needs a C string copies them out.
//
// Empty for a package with no manifest section, no title record, or a record
// whose bytes do not decode. Those are the three ways a package can fail to
// name itself, and none of them is something a console can do anything about
// beyond drawing no name. This reports them as one answer rather than as an
// error a caller would have nowhere to put.
[[nodiscard]] std::string_view project_title(
    const package_format::LoadedPackage& package
) noexcept;

}  // namespace grandleon::package_runtime
