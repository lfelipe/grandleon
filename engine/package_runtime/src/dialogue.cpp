// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/dialogue.hpp>

#include <cstddef>
#include <utility>

namespace grandleon::package_runtime {
namespace {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

class Reader final {
public:
    Reader(const LoadedPackage& package, const RecordView& record)
        : bytes_(package.byte_data()),
          cursor_(record.payload_offset),
          end_(static_cast<std::size_t>(record.payload_offset) +
               record.payload_size) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = bytes_[cursor_];
        cursor_ += 1;
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(
            bytes_[cursor_] |
            (static_cast<std::uint16_t>(bytes_[cursor_ + 1]) << 8U)
        );
        cursor_ += 2;
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(bytes_[cursor_ + index])
                     << (index * 8U);
        }
        cursor_ += 8;
        return true;
    }

    bool string(std::string& value) {
        std::uint16_t size = 0;
        if (!u16(size) || remaining() < size) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_ + cursor_),
            size
        );
        cursor_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return cursor_ <= end_ ? end_ - cursor_ : 0;
    }

    [[nodiscard]] bool finished() const noexcept { return cursor_ == end_; }

private:
    const std::uint8_t* bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

}  // namespace

std::string_view error_name(DialogueError error) noexcept {
    switch (error) {
        case DialogueError::none: return "none";
        case DialogueError::missing_section: return "missing_section";
        case DialogueError::missing_record: return "missing_record";
        case DialogueError::malformed_payload: return "malformed_payload";
    }
    return "unknown";
}

DialogueLoadResult load_dialogue(
    const LoadedPackage& package,
    std::uint64_t dialogue_id
) {
    auto fail = [](DialogueError error) {
        DialogueLoadResult result;
        result.error = error;
        return result;
    };
    if (package.find(SectionType::dialogue) == nullptr) {
        return fail(DialogueError::missing_section);
    }
    const RecordView* record =
        package.find(SectionType::dialogue, dialogue_id);
    if (record == nullptr) return fail(DialogueError::missing_record);

    Reader reader(package, *record);
    DialogueLoadResult result;
    result.dialogue.id = dialogue_id;
    std::uint16_t count = 0;
    if (!reader.string(result.dialogue.name) || !reader.u16(count)) {
        return fail(DialogueError::malformed_payload);
    }
    // Two bytes minimum per string, so a count the record cannot hold is
    // rejected before anything is reserved.
    if (static_cast<std::size_t>(count) * 4U > reader.remaining()) {
        return fail(DialogueError::malformed_payload);
    }
    result.dialogue.lines.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        DialogueLine line;
        if (!reader.string(line.speaker) || !reader.string(line.text)) {
            return fail(DialogueError::malformed_payload);
        }
        result.dialogue.lines.push_back(std::move(line));
    }
    // What the scene is drawn against, appended after the lines and written
    // only when the scene names one. A record that ends here is a scene with
    // no backdrop, which is every scene authored before backdrops existed and
    // is why nothing older had to be rewritten to gain the field. One byte
    // past the lines is the menu index plus one; zero is not a valid encoding,
    // so a record padded with a zero is refused rather than read as backdrop
    // zero, and anything longer is refused as it always was.
    if (reader.remaining() == 1) {
        std::uint8_t backdrop = 0;
        if (!reader.u8(backdrop) || backdrop == 0) {
            return fail(DialogueError::malformed_payload);
        }
        result.dialogue.backdrop = backdrop;
    } else if (reader.remaining() > 1) {
        // A scene that cast somebody. More than one trailing byte is a shape
        // no writer produced before a cast existed, so the three cases
        // (nothing, one byte, more) are told apart by what is left rather
        // than by a flag anybody had to add to a record already in the field.
        //
        // The backdrop is unconditional in this tail and a zero is legal, so
        // that a scene may cast a company and name no backdrop. Then the cast
        // itself, then one byte per line naming the entry that speaks it:
        // the join already made by the compiler, which is the component that
        // saw both the speaker strings and the cast. Nothing here compares a
        // string, and nothing here holds a keyword.
        std::uint8_t backdrop = 0;
        std::uint8_t cast_count = 0;
        if (!reader.u8(backdrop) || !reader.u8(cast_count)) {
            return fail(DialogueError::malformed_payload);
        }
        // Zero would be the empty cast, which is written as no tail at all;
        // reaching it here means a record claiming a tail it does not have.
        if (cast_count == 0) return fail(DialogueError::malformed_payload);
        if (static_cast<std::size_t>(cast_count) * 8U +
                result.dialogue.lines.size() >
            reader.remaining()) {
            return fail(DialogueError::malformed_payload);
        }
        result.dialogue.backdrop = backdrop;
        result.dialogue.cast.reserve(cast_count);
        for (std::uint8_t index = 0; index < cast_count; ++index) {
            std::uint64_t unit_type_id = 0;
            if (!reader.u64(unit_type_id)) {
                return fail(DialogueError::malformed_payload);
            }
            result.dialogue.cast.push_back(unit_type_id);
        }
        for (DialogueLine& line : result.dialogue.lines) {
            std::uint8_t entry = 0;
            if (!reader.u8(entry) || entry > cast_count) {
                return fail(DialogueError::malformed_payload);
            }
            line.cast_entry = entry;
        }
    }
    if (!reader.finished()) return fail(DialogueError::malformed_payload);
    return result;
}

}  // namespace grandleon::package_runtime
