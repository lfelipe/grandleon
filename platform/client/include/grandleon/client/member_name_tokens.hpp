// SPDX-License-Identifier: MIT
#pragma once

// Portable replacement of explicit campaign-member tokens in player-facing
// text. The caller owns identity lookup because a full client has roster
// entries while a compact exported runtime has generated member records. The
// parser and its fail-visible behavior stay identical on every player.

#include <string>
#include <string_view>

namespace grandleon::client {

template <typename Resolve>
[[nodiscard]] std::string substitute_member_name_tokens(
    std::string_view authored_text,
    Resolve&& resolve
) {
    constexpr std::string_view opening = "{{member:";
    constexpr std::string_view closing = "}}";
    std::string result;
    // Nugget's PlayStation libstdc++ omits the hosted pointer-length append
    // symbol. Copying bounded source text byte by byte is equivalent and keeps
    // unknown or malformed tokens byte-for-byte visible.
    const auto append_bytes = [&result](std::string_view text) {
        for (char byte : text) result.push_back(byte);
    };
    std::size_t cursor = 0U;
    while (cursor < authored_text.size()) {
        const std::size_t begin = authored_text.find(opening, cursor);
        if (begin == std::string_view::npos) {
            append_bytes(authored_text.substr(cursor));
            break;
        }
        append_bytes(authored_text.substr(cursor, begin - cursor));
        const std::size_t key_begin = begin + opening.size();
        const std::size_t end = authored_text.find(closing, key_begin);
        if (end == std::string_view::npos) {
            append_bytes(authored_text.substr(begin));
            break;
        }
        const std::string_view key = authored_text.substr(
            key_begin, end - key_begin
        );
        const std::string_view replacement = key.empty()
            ? std::string_view{} : std::string_view{resolve(key)};
        if (!replacement.empty()) {
            append_bytes(replacement);
        } else {
            append_bytes(authored_text.substr(
                begin, end + closing.size() - begin
            ));
        }
        cursor = end + closing.size();
    }
    return result;
}

}  // namespace grandleon::client
