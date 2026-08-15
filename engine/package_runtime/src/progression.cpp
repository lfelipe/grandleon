// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/progression.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grandleon::package_runtime {
namespace {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

// The same untrusted-input discipline every other decoder in this module
// keeps: nothing is read that the record does not still have bytes for.
class Reader final {
public:
    Reader(const LoadedPackage& package, const RecordView& record)
        : bytes_(package.byte_data()),
          cursor_(record.payload_offset),
          end_(static_cast<std::size_t>(record.payload_offset) +
               record.payload_size) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = bytes_[cursor_++];
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

    bool skip(std::size_t count) {
        if (remaining() < count) return false;
        cursor_ += count;
        return true;
    }

    bool skip_string() {
        std::uint16_t size = 0;
        return u16(size) && skip(size);
    }

    bool skip_ids() {
        std::uint16_t count = 0;
        if (!u16(count) || static_cast<std::size_t>(count) > remaining() / 8U) {
            return false;
        }
        return skip(static_cast<std::size_t>(count) * 8U);
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

UnitProgressionLoad load_unit_progression(
    const LoadedPackage& package,
    std::uint64_t unit_type_id
) {
    UnitProgressionLoad result;
    const RecordView* const record =
        package.find(SectionType::unit_types, unit_type_id);
    if (record == nullptr) {
        return result;
    }
    Reader reader(package, *record);
    // Name, class, faction, weapons, items, abilities: everything the board
    // cares about and nothing this does.
    if (!(reader.skip_string() && reader.skip(16) && reader.skip_ids() &&
          reader.skip_ids() && reader.skip_ids())) {
        return result;
    }
    // A record that ends here was written before growth existed. That is a
    // successful read of a unit type that does not grow, not a malformed one:
    // the defaults are the meaning of the absence, and stating that here is
    // why every package already on disk keeps loading.
    if (reader.finished()) {
        result.found = true;
        return result;
    }
    UnitProgression progression;
    if (!(reader.u16(progression.experience_award) &&
          reader.u16(progression.experience_per_level))) {
        return result;
    }
    // As many chances as the record actually carries. A block written before
    // the stat line grew stops four short, and the four it does not carry stay
    // zero, never rolled and never grown, which is the same thing four zero
    // bytes would say. Anything between the accepted lengths falls out of the
    // `finished()` check below.
    //
    // The drop tail that follows is nine more bytes on either length, and this
    // decoder skips it: a drop is a battle rule, and a level-up has no use for
    // it. It still has to be counted, because the shorter chance list is
    // recognised by what is left rather than by a flag.
    const std::size_t left = reader.remaining();
    const std::size_t carried =
        (left == growable_stat_count_before_richer_line ||
         left == growable_stat_count_before_richer_line + unit_type_drop_size)
            ? growable_stat_count_before_richer_line
            : growable_stat_count;
    for (std::size_t index = 0; index < carried; ++index) {
        std::uint8_t chance = 0;
        if (!reader.u8(chance) || chance > 100U) {
            return result;
        }
        progression.growth[index] = chance;
    }
    if (!reader.finished() && !reader.skip(unit_type_drop_size)) {
        return result;
    }
    // A threshold of zero is a level nothing reaches and a division with no
    // answer. The compiler refuses one; a package that carries one anyway is
    // refused here rather than divided by.
    if (!reader.finished() || progression.experience_per_level == 0U) {
        return result;
    }
    result.found = true;
    result.progression = progression;
    return result;
}

}  // namespace grandleon::package_runtime
