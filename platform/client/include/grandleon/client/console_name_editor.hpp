// SPDX-License-Identifier: MIT
#pragma once

// The compact consoles' shared, fixed-storage character-name editor.
//
// Nintendo 64 and PlayStation both draw the same 8-pixel console alphabet:
// printable ASCII from space through underscore, folded to capitals. The
// keyboard deliberately offers the useful name subset both machines can show
// rather than accepting bytes that turn into blank glyphs. Eighteen glyphs is
// the narrowest battle HUD's name field, so a name made here is readable where
// it matters as well as valid under the portable 64-byte campaign limit.

#include <array>
#include <cstddef>
#include <string_view>

namespace grandleon::client {

inline constexpr std::size_t console_player_name_glyphs = 18U;

enum class ConsoleNameAnswer {
    editing = 0,
    save,
    use_default,
    cancel,
};

enum class ConsoleNameKeyKind {
    character = 0,
    space,
    use_default,
};

struct ConsoleNameKey final {
    const char* label;
    char character;
    ConsoleNameKeyKind kind;
};

class ConsoleNameEditor final {
public:
    static constexpr int rows = 5;

    ConsoleNameEditor(
        std::string_view current_name,
        std::string_view authored_default
    ) noexcept {
        copy_if_editable(current_name);
        copy_default_if_editable(authored_default);
    }

    [[nodiscard]] int row() const noexcept { return row_; }
    [[nodiscard]] int column() const noexcept { return column_; }
    [[nodiscard]] bool invalid() const noexcept { return invalid_; }
    [[nodiscard]] bool current_is_editable() const noexcept {
        return current_is_editable_;
    }
    [[nodiscard]] std::string_view name() const noexcept {
        return {name_.data(), length_};
    }
    [[nodiscard]] const char* c_str() const noexcept { return name_.data(); }

    [[nodiscard]] static constexpr int columns(int row) noexcept {
        return row >= 0 && row < rows ? row_lengths_[row] : 0;
    }

    [[nodiscard]] static constexpr ConsoleNameKey key(
        int row, int column
    ) noexcept {
        if (row < 0 || row >= rows || column < 0 ||
            column >= columns(row)) {
            return {"", '\0', ConsoleNameKeyKind::character};
        }
        return keys_[row][column];
    }

    void move(int across, int down) noexcept {
        if (down < 0 && row_ > 0) --row_;
        if (down > 0 && row_ + 1 < rows) ++row_;
        const int count = columns(row_);
        if (column_ >= count) column_ = count - 1;
        if (across < 0 && column_ > 0) --column_;
        if (across > 0 && column_ + 1 < count) ++column_;
        invalid_ = false;
    }

    [[nodiscard]] ConsoleNameAnswer activate() noexcept {
        const ConsoleNameKey selected = key(row_, column_);
        if (selected.kind == ConsoleNameKeyKind::use_default) {
            invalid_ = false;
            return ConsoleNameAnswer::use_default;
        }
        const char value = selected.kind == ConsoleNameKeyKind::space
                               ? ' '
                               : selected.character;
        if (length_ < console_player_name_glyphs) {
            name_[length_++] = value;
            name_[length_] = '\0';
        }
        invalid_ = false;
        return ConsoleNameAnswer::editing;
    }

    void erase() noexcept {
        if (length_ > 0U) name_[--length_] = '\0';
        invalid_ = false;
    }

    [[nodiscard]] ConsoleNameAnswer save() noexcept {
        trim();
        if (length_ == 0U) {
            invalid_ = true;
            return ConsoleNameAnswer::editing;
        }
        invalid_ = false;
        if (default_is_editable_ && length_ == default_length_) {
            bool same = true;
            for (std::size_t i = 0; i < length_; ++i) {
                if (name_[i] != default_name_[i]) {
                    same = false;
                    break;
                }
            }
            if (same) return ConsoleNameAnswer::use_default;
        }
        return ConsoleNameAnswer::save;
    }

private:
    [[nodiscard]] static constexpr bool offered(char value) noexcept {
        return (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == ' ' ||
               value == '\'' || value == '-';
    }

    [[nodiscard]] static constexpr char folded(char value) noexcept {
        return value >= 'a' && value <= 'z'
                   ? static_cast<char>(value - ('a' - 'A'))
                   : value;
    }

    void copy_if_editable(std::string_view source) noexcept {
        if (source.size() > console_player_name_glyphs) return;
        for (char value : source) {
            value = folded(value);
            if (!offered(value)) return;
        }
        for (char value : source) name_[length_++] = folded(value);
        name_[length_] = '\0';
        current_is_editable_ = true;
    }

    void copy_default_if_editable(std::string_view source) noexcept {
        if (source.size() > console_player_name_glyphs) return;
        for (char value : source) {
            value = folded(value);
            if (!offered(value)) return;
        }
        for (char value : source) {
            default_name_[default_length_++] = folded(value);
        }
        default_name_[default_length_] = '\0';
        default_is_editable_ = true;
    }

    void trim() noexcept {
        std::size_t first = 0;
        while (first < length_ && name_[first] == ' ') ++first;
        while (length_ > first && name_[length_ - 1U] == ' ') --length_;
        if (first > 0U) {
            const std::size_t kept = length_ - first;
            for (std::size_t i = 0; i < kept; ++i) {
                name_[i] = name_[first + i];
            }
            length_ = kept;
        }
        name_[length_] = '\0';
    }

    inline static constexpr std::array<int, rows> row_lengths_{{9, 9, 9, 9, 4}};
    inline static constexpr std::array<std::array<ConsoleNameKey, 9>, rows>
        keys_{{
            {{{"A", 'A', ConsoleNameKeyKind::character},
              {"B", 'B', ConsoleNameKeyKind::character},
              {"C", 'C', ConsoleNameKeyKind::character},
              {"D", 'D', ConsoleNameKeyKind::character},
              {"E", 'E', ConsoleNameKeyKind::character},
              {"F", 'F', ConsoleNameKeyKind::character},
              {"G", 'G', ConsoleNameKeyKind::character},
              {"H", 'H', ConsoleNameKeyKind::character},
              {"I", 'I', ConsoleNameKeyKind::character}}},
            {{{"J", 'J', ConsoleNameKeyKind::character},
              {"K", 'K', ConsoleNameKeyKind::character},
              {"L", 'L', ConsoleNameKeyKind::character},
              {"M", 'M', ConsoleNameKeyKind::character},
              {"N", 'N', ConsoleNameKeyKind::character},
              {"O", 'O', ConsoleNameKeyKind::character},
              {"P", 'P', ConsoleNameKeyKind::character},
              {"Q", 'Q', ConsoleNameKeyKind::character},
              {"R", 'R', ConsoleNameKeyKind::character}}},
            {{{"S", 'S', ConsoleNameKeyKind::character},
              {"T", 'T', ConsoleNameKeyKind::character},
              {"U", 'U', ConsoleNameKeyKind::character},
              {"V", 'V', ConsoleNameKeyKind::character},
              {"W", 'W', ConsoleNameKeyKind::character},
              {"X", 'X', ConsoleNameKeyKind::character},
              {"Y", 'Y', ConsoleNameKeyKind::character},
              {"Z", 'Z', ConsoleNameKeyKind::character},
              {"-", '-', ConsoleNameKeyKind::character}}},
            {{{"0", '0', ConsoleNameKeyKind::character},
              {"1", '1', ConsoleNameKeyKind::character},
              {"2", '2', ConsoleNameKeyKind::character},
              {"3", '3', ConsoleNameKeyKind::character},
              {"4", '4', ConsoleNameKeyKind::character},
              {"5", '5', ConsoleNameKeyKind::character},
              {"6", '6', ConsoleNameKeyKind::character},
              {"7", '7', ConsoleNameKeyKind::character},
              {"8", '8', ConsoleNameKeyKind::character}}},
            {{{"9", '9', ConsoleNameKeyKind::character},
              {"'", '\'', ConsoleNameKeyKind::character},
              {"SPACE", ' ', ConsoleNameKeyKind::space},
              {"DEFAULT", '\0', ConsoleNameKeyKind::use_default},
              {"", '\0', ConsoleNameKeyKind::character},
              {"", '\0', ConsoleNameKeyKind::character},
              {"", '\0', ConsoleNameKeyKind::character},
              {"", '\0', ConsoleNameKeyKind::character},
              {"", '\0', ConsoleNameKeyKind::character}}},
        }};

    std::array<char, console_player_name_glyphs + 1U> name_{};
    std::array<char, console_player_name_glyphs + 1U> default_name_{};
    std::size_t length_{0U};
    std::size_t default_length_{0U};
    int row_{0};
    int column_{0};
    bool current_is_editable_{false};
    bool default_is_editable_{false};
    bool invalid_{false};
};

}  // namespace grandleon::client
