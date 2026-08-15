// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <string_view>

namespace grandleon::package_runtime {

// What the author called a piece of content, read out of the package.
//
// Every content section this format defines writes its definition's display
// name as the **first field of the record**, and keys the record by the
// definition's own identity: unit types, weapons, items and abilities all do
// (`tools/game_content/src/compiler.cpp`). So one decode serves all four, and
// the section is a parameter rather than four near-identical readers.
//
// It exists because the alternative is `platform/sheet`, a hardcoded table of
// the *shipped* projects' ids: content that table has never met falls back to
// its category. On a console that is not a cosmetic gap: a character an author
// named goes down and the board says `UNIT DIED`, which is what a cartridge
// prints whenever nothing reads the authored name back.
//
// The view borrows the package's own bytes and lives exactly as long as the
// package does. Nothing is copied and nothing is allocated, on the same terms
// and for the same reason as `project_title`: a machine reading its cartridge
// in place cannot afford a reader that allocates per name, and a name is asked
// for at the moment somebody is struck.
//
// The bytes are **not** NUL-terminated. That is the whole reason this is not
// simply wired underneath `platform/sheet`'s existing accessors, which return
// `const char*`: a borrowed counted string cannot be handed back as one without
// a copy, and the copy has to belong to somebody.
//
// Empty when the package has no such section, no record under that identity, or
// a record whose leading string does not decode. Those are the same three ways
// of not knowing, reported as one answer, because a caller's move is the same
// for all three: say what it said before it could ask.
[[nodiscard]] std::string_view content_name(
    const package_format::LoadedPackage& package,
    package_format::SectionType section,
    std::uint64_t stable_id
) noexcept;

// Which class a unit type belongs to, read out of the package.
//
// Here beside `content_name` and not in the encounter loader, because the
// question a client asks is the same shape: one field off the front of one
// record, wanted by a surface that is drawing rather than by a rule that is
// resolving. The loader decodes the whole record to build a battle; a panel
// that wants to write the class under a name would have to load an encounter
// to find out, which is a lot of decoding to answer "what kind of character is
// this".
//
// The class identity is the field immediately after the unit type's display
// name, which is what makes this two decodes of the same record's front rather
// than a second reader: `content_name` takes the string, this takes the eight
// little-endian bytes behind it.
//
// Zero when the package has no unit types section, no record under that
// identity, or a record too short to hold both fields. Those are the same
// three ways of not knowing `content_name` reports as an empty view, and a
// caller's move is the same for all three: say what it said before it could
// ask.
[[nodiscard]] std::uint64_t unit_type_class(
    const package_format::LoadedPackage& package,
    std::uint64_t unit_type_id
) noexcept;

}  // namespace grandleon::package_runtime
