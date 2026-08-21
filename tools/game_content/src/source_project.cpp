// SPDX-License-Identifier: MIT
#include <grandleon/game_content/source_project.hpp>

#include <grandleon/core/content_identity.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace grandleon::game_content {
namespace {

struct Json final {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using Value = std::variant<
        std::nullptr_t, bool, std::int64_t, std::string, Array, Object
    >;
    Value value;
};

class JsonError final : public std::runtime_error {
public:
    JsonError(std::size_t offset, std::string message)
        : std::runtime_error(std::move(message)), offset_(offset) {}
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
private:
    std::size_t offset_;
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    Json parse() {
        whitespace();
        Json result = value();
        whitespace();
        if (position_ != input_.size()) {
            fail("unexpected trailing input");
        }
        return result;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw JsonError(position_, message);
    }

    // The four characters JSON calls whitespace, and no others. `std::isspace`
    // would also accept a vertical tab and a form feed, which JSON forbids. It
    // also answers according to the C locale, which a compiler's reading of a
    // document must not depend on.
    static constexpr bool is_whitespace(char character) noexcept {
        return character == ' ' || character == '\t' || character == '\n' ||
               character == '\r';
    }

    void whitespace() {
        while (position_ < input_.size() &&
               is_whitespace(input_[position_])) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void literal(std::string_view expected) {
        if (input_.substr(position_, expected.size()) != expected) {
            fail("invalid literal");
        }
        position_ += expected.size();
    }

    // Containers recurse once per nesting level, and this parser also runs on
    // the Nintendo 64's small stack, so nesting is capped well below any
    // schema-valid document rather than left to exhaust the stack.
    static constexpr std::size_t maximum_depth = 64;

    Json value() {
        whitespace();
        if (position_ == input_.size()) fail("expected value");
        switch (input_[position_]) {
            case '{': return Json{object()};
            case '[': return Json{array()};
            case '"': return Json{string()};
            case 't': literal("true"); return Json{true};
            case 'f': literal("false"); return Json{false};
            case 'n': literal("null"); return Json{nullptr};
            default: return Json{integer()};
        }
    }

    // Guards one container level for the duration of a member function.
    class DepthGuard final {
    public:
        explicit DepthGuard(JsonParser& parser) : parser_(parser) {
            if (parser_.depth_ >= maximum_depth) {
                parser_.fail("nesting is too deep");
            }
            ++parser_.depth_;
        }
        ~DepthGuard() { --parser_.depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

    private:
        JsonParser& parser_;
    };

    Json::Object object() {
        const DepthGuard depth{*this};
        consume('{');
        whitespace();
        Json::Object result;
        if (consume('}')) return result;
        while (true) {
            whitespace();
            if (position_ == input_.size() || input_[position_] != '"') {
                fail("expected object key");
            }
            std::string key = string();
            whitespace();
            if (!consume(':')) fail("expected ':'");
            if (!result.emplace(std::move(key), value()).second) {
                fail("duplicate object key");
            }
            whitespace();
            if (consume('}')) return result;
            if (!consume(',')) fail("expected ',' or '}'");
        }
    }

    Json::Array array() {
        const DepthGuard depth{*this};
        consume('[');
        whitespace();
        Json::Array result;
        if (consume(']')) return result;
        while (true) {
            result.push_back(value());
            whitespace();
            if (consume(']')) return result;
            if (!consume(',')) fail("expected ',' or ']'");
        }
    }

    static void append_utf8(std::string& out, std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            out.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            out.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            out.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            out.push_back(
                static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU))
            );
            out.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            out.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            out.push_back(
                static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU))
            );
            out.push_back(
                static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU))
            );
            out.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }

    std::uint32_t hex_quad() {
        if (input_.size() - position_ < 4) fail("incomplete unicode escape");
        std::uint32_t result = 0;
        for (unsigned index = 0; index < 4; ++index) {
            const char character = input_[position_++];
            result <<= 4U;
            if (character >= '0' && character <= '9') {
                result |= static_cast<unsigned>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                result |= static_cast<unsigned>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                result |= static_cast<unsigned>(character - 'A' + 10);
            } else {
                fail("invalid unicode escape");
            }
        }
        return result;
    }

    std::string string() {
        consume('"');
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return result;
            if (character < 0x20U) fail("control character in string");
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ == input_.size()) fail("incomplete escape");
            switch (input_[position_++]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = hex_quad();
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        if (input_.size() - position_ < 2 ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') {
                            fail("missing low surrogate");
                        }
                        position_ += 2;
                        const std::uint32_t low = hex_quad();
                        if (low < 0xdc00U || low > 0xdfffU) {
                            fail("invalid low surrogate");
                        }
                        codepoint =
                            0x10000U + ((codepoint - 0xd800U) << 10U) +
                            (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U &&
                               codepoint <= 0xdfffU) {
                        fail("unexpected low surrogate");
                    }
                    append_utf8(result, codepoint);
                    break;
                }
                default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    std::int64_t integer() {
        const std::size_t begin = position_;
        consume('-');
        if (position_ == input_.size()) fail("invalid number");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() &&
                std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("leading zero in number");
            }
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid value");
            }
            while (position_ < input_.size() &&
                   std::isdigit(
                       static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        if (position_ < input_.size() &&
            (input_[position_] == '.' || input_[position_] == 'e' ||
             input_[position_] == 'E')) {
            fail("source numbers must be integers");
        }
        std::int64_t result{};
        const auto parsed = std::from_chars(
            input_.data() + begin, input_.data() + position_, result
        );
        if (parsed.ec != std::errc{}) fail("integer is out of range");
        return result;
    }

    std::string_view input_;
    std::size_t position_{};
    std::size_t depth_{};
};

using Object = Json::Object;
using Array = Json::Array;

//: How wide and how tall a board may be, which is what `map.schema.json` says
//: and therefore what the native reader says.
inline constexpr std::int64_t maximum_map_side = 256;

//: How many speakers one scene may name. A line says which of them it belongs
//: to in a single byte, and zero in that byte means "this scene names nobody
//: for this line", so the entries themselves run from one.
inline constexpr std::size_t maximum_cast = 255;

struct Mapper final {
    SourceParseResult result;
    std::map<
        std::pair<std::string, StableId>,
        std::string
    > hashed_ids;

    void error(
        SourceDiagnosticCode code,
        std::string path,
        std::string detail
    ) {
        result.diagnostics.push_back(
            {code, std::move(path), std::move(detail)}
        );
    }

    const Object* object(const Json& value, const std::string& path) {
        const auto* found = std::get_if<Object>(&value.value);
        if (found == nullptr) {
            error(SourceDiagnosticCode::invalid_value, path, "expected object");
        }
        return found;
    }

    const Json* member(
        const Object& value,
        std::string_view key,
        const std::string& path,
        bool required = true
    ) {
        const auto found = value.find(key);
        if (found == value.end()) {
            if (required) {
                error(
                    SourceDiagnosticCode::missing_value,
                    path + "." + std::string(key),
                    "required value is missing"
                );
            }
            return nullptr;
        }
        return &found->second;
    }

    std::optional<std::string> string(
        const Object& value,
        std::string_view key,
        const std::string& path,
        bool required = true
    ) {
        const Json* field = member(value, key, path, required);
        if (field == nullptr) return std::nullopt;
        const auto* found = std::get_if<std::string>(&field->value);
        if (found == nullptr) {
            error(
                SourceDiagnosticCode::invalid_value,
                path + "." + std::string(key),
                "expected string"
            );
            return std::nullopt;
        }
        return *found;
    }

    // One authored number, held to the range the field it lands in admits.
    //
    // The refusal quotes the number the author wrote and the range it missed,
    // rather than naming the bound it broke: an author reading it should be
    // able to fix the file without knowing anything about the type the number
    // ends up in. `because` is for a range whose end would otherwise look
    // arbitrary, and is written as a clause the message can finish with.
    std::optional<std::int64_t> integer(
        const Object& value,
        std::string_view key,
        const std::string& path,
        std::int64_t minimum,
        std::int64_t maximum,
        bool required = true,
        std::string_view because = {}
    ) {
        const Json* field = member(value, key, path, required);
        if (field == nullptr) return std::nullopt;
        const std::string where = path + "." + std::string(key);
        const auto* found = std::get_if<std::int64_t>(&field->value);
        if (found == nullptr) {
            error(
                SourceDiagnosticCode::invalid_value, where,
                "expected a whole number"
            );
            return std::nullopt;
        }
        if (*found < minimum || *found > maximum) {
            error(
                SourceDiagnosticCode::invalid_value, where,
                std::to_string(*found) + " is outside the " +
                    std::to_string(minimum) + " to " +
                    std::to_string(maximum) + " this field admits" +
                    std::string(because)
            );
            return std::nullopt;
        }
        return *found;
    }

    const Array* array(
        const Object& value,
        std::string_view key,
        const std::string& path,
        bool required = true
    ) {
        const Json* field = member(value, key, path, required);
        if (field == nullptr) return nullptr;
        const auto* found = std::get_if<Array>(&field->value);
        if (found == nullptr) {
            error(
                SourceDiagnosticCode::invalid_value,
                path + "." + std::string(key),
                "expected array"
            );
        }
        return found;
    }

    StableId id(
        const Object& value,
        const std::string& category,
        const std::string& path
    ) {
        const auto source_key = string(value, "id", path);
        if (!source_key) return 0;
        const StableId hashed = core::stable_content_id_v1(*source_key);
        auto [found, inserted] = hashed_ids.emplace(
            std::make_pair(category, hashed), *source_key
        );
        if (!inserted && found->second != *source_key) {
            error(
                SourceDiagnosticCode::stable_id_collision,
                path + ".id",
                "'" + *source_key + "' collides with '" + found->second + "'"
            );
        }
        return hashed;
    }

    StableId reference(
        const Object& value,
        std::string_view key,
        const std::string& path,
        bool required = true
    ) {
        const auto source_key = string(value, key, path, required);
        return source_key ? core::stable_content_id_v1(*source_key) : 0;
    }

    std::vector<StableId> references(
        const Object& value,
        std::string_view key,
        const std::string& path
    ) {
        std::vector<StableId> result_ids;
        const Array* values = array(value, key, path, false);
        if (values == nullptr) return result_ids;
        for (std::size_t index = 0; index < values->size(); ++index) {
            const auto* source_key =
                std::get_if<std::string>(&(*values)[index].value);
            if (source_key == nullptr) {
                error(
                    SourceDiagnosticCode::invalid_value,
                    path + "." + std::string(key) + "[" +
                        std::to_string(index) + "]",
                    "expected stable source-key string"
                );
            } else {
                result_ids.push_back(core::stable_content_id_v1(*source_key));
            }
        }
        return result_ids;
    }

    //: The art library's terrain kind of every cell, parallel to
    //: :func:`references`. Resolved here, once, because the identity a cell
    //: carries afterwards is a hash: a presenter cannot recover the authored
    //: name to match keywords against, and must not keep a table of its own.
    std::vector<std::uint8_t> terrain_kinds(
        const Object& value,
        std::string_view key,
        const std::string& path
    ) {
        std::vector<std::uint8_t> kinds;
        const Array* values = array(value, key, path, false);
        if (values == nullptr) return kinds;
        kinds.reserve(values->size());
        for (const Json& cell : *values) {
            const auto* name = std::get_if<std::string>(&cell.value);
            kinds.push_back(
                name == nullptr ? terrain_kind_unknown
                                : terrain_kind_index(*name)
            );
        }
        return kinds;
    }

    template<typename Definition, typename Convert>
    std::vector<Definition> definitions(
        const Object& root,
        std::string_view key,
        bool required,
        Convert convert
    ) {
        std::vector<Definition> result_values;
        const Array* values = array(root, key, "$", required);
        if (values == nullptr) return result_values;
        result_values.reserve(values->size());
        for (std::size_t index = 0; index < values->size(); ++index) {
            const std::string path =
                "$." + std::string(key) + "[" + std::to_string(index) + "]";
            const Object* definition = object((*values)[index], path);
            if (definition != nullptr) {
                result_values.push_back(convert(*definition, path));
            }
        }
        return result_values;
    }
};

// The package identity, read on exactly the terms `common.schema.json`'s
// `packageId` states them: thirty-six characters, lowercase hex, a version
// nibble in `[1-5]` and a variant nibble in `[89ab]`. Held to the same terms
// here rather than left to agree by convention: a project built by a tool that
// skipped the schema must not be able to name itself something no other
// component in the toolchain would accept.
bool parse_uuid(
    std::string_view text,
    std::array<std::uint8_t, 16>& output
) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') {
        return false;
    }
    const char version = text[14];
    if (version < '1' || version > '5') return false;
    const char variant = text[19];
    if (variant != '8' && variant != '9' && variant != 'a' &&
        variant != 'b') {
        return false;
    }
    unsigned nibble = 0;
    std::size_t byte_index = 0;
    bool high = true;
    for (char character : text) {
        if (character == '-') continue;
        unsigned value;
        if (character >= '0' && character <= '9') {
            value = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value = static_cast<unsigned>(character - 'a' + 10);
        } else {
            return false;
        }
        if (high) {
            nibble = value << 4U;
        } else {
            if (byte_index >= output.size()) return false;
            output[byte_index++] =
                static_cast<std::uint8_t>(nibble | value);
        }
        high = !high;
    }
    return byte_index == output.size() && high;
}

// Three components of at most 1023, packed ten bits each. A pre-release
// revision (the `-beta` the schema's version pattern also admits) has nowhere
// to go in thirty bits, and dropping the suffix would compile `1.2.3-beta` and
// `1.2.3` to one identical revision. So it is refused here, and the caller says
// so in those words, rather than accepted as something it is not.
std::optional<std::uint32_t> parse_revision(std::string_view text) {
    if (text.find('-') != std::string_view::npos) return std::nullopt;
    std::array<std::uint32_t, 3> parts{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        // The last component runs to the end of the text: there is no fourth
        // separator, and a hyphen was refused above.
        const std::size_t end =
            index == 2 ? std::string_view::npos : text.find('.', start);
        if (end == std::string_view::npos && index != 2) return std::nullopt;
        const std::string_view part =
            text.substr(start, end == std::string_view::npos
                                   ? text.size() - start
                                   : end - start);
        if (part.empty()) return std::nullopt;
        const auto parsed = std::from_chars(
            part.data(), part.data() + part.size(), parts[index]
        );
        if (parsed.ec != std::errc{} ||
            parsed.ptr != part.data() + part.size() ||
            parts[index] > 1023U) {
            return std::nullopt;
        }
        start = end == std::string_view::npos ? text.size() : end + 1;
    }
    return (parts[0] << 20U) | (parts[1] << 10U) | parts[2];
}

// Where every number the damage arithmetic reads stops, and the clause that
// says why to whoever wrote a bigger one.
//
// The bound is asked for rather than written out. `create_encounter` and
// `engine/package_runtime`'s loader both refuse a stat past it, so a compiler
// keeping a larger copy of its own would emit a package no runtime would open:
// the author would pass the schema, get a `.gpk`, and hear about the number
// from a board that refuses to start. All three asking one constant is what
// keeps the refusal where the author can still see the file.
constexpr std::int64_t damage_stat_maximum = simulation::maximum_stat;

constexpr std::string_view damage_stat_reason =
    ", because the rules add two of these together before subtracting a third "
    "and the total has to fit the number health is kept in";

Stats stats(Mapper& mapper, const Object& owner, const std::string& path) {
    Stats result;
    const Json* field = mapper.member(owner, "baseStats", path);
    if (field == nullptr) return result;
    const Object* values = mapper.object(*field, path + ".baseStats");
    if (values == nullptr) return result;
    const auto health = mapper.integer(
        *values, "health", path + ".baseStats", 1,
        std::numeric_limits<std::int16_t>::max()
    );
    const auto strength = mapper.integer(
        *values, "strength", path + ".baseStats", 0, damage_stat_maximum, true,
        damage_stat_reason
    );
    const auto defense = mapper.integer(
        *values, "defense", path + ".baseStats", 0, damage_stat_maximum, true,
        damage_stat_reason
    );
    const auto movement = mapper.integer(
        *values, "movement", path + ".baseStats", 1,
        std::numeric_limits<std::uint8_t>::max()
    );
    if (health) result.health = static_cast<std::int16_t>(*health);
    if (strength) result.strength = static_cast<std::int16_t>(*strength);
    if (defense) result.defense = static_cast<std::int16_t>(*defense);
    if (movement) result.movement = static_cast<std::uint8_t>(*movement);
    if (mapper.member(*values, "actionPoints", path + ".baseStats", false) !=
        nullptr) {
        const auto points = mapper.integer(
            *values, "actionPoints", path + ".baseStats", 1,
            std::numeric_limits<std::uint8_t>::max()
        );
        if (points) result.action_points = static_cast<std::uint8_t>(*points);
    }
    if (mapper.member(*values, "speed", path + ".baseStats", false) != nullptr) {
        const auto speed = mapper.integer(
            *values, "speed", path + ".baseStats", 1,
            std::numeric_limits<std::uint8_t>::max()
        );
        if (speed) result.speed = static_cast<std::uint8_t>(*speed);
    }
    if (mapper.member(*values, "resistance", path + ".baseStats", false) !=
        nullptr) {
        const auto resistance = mapper.integer(
            *values, "resistance", path + ".baseStats", 0,
            damage_stat_maximum, true, damage_stat_reason
        );
        if (resistance) {
            result.resistance = static_cast<std::int16_t>(*resistance);
        }
    }
    // The richer stat line, each optional and each zero when absent. Zero is
    // not a placeholder here: it is the value at which the hit chance is the
    // weapon's authored accuracy and a cast costs its authored power, so a
    // class that says nothing about them plays exactly as it always did.
    //
    // Only magic stops at the damage bound. The other three are percentage
    // points on a hit chance the rules clamp to nought and a hundred before
    // anything reads it, so they reach no arithmetic that can overflow and the
    // engine asks nothing of them but that they not be negative.
    struct OptionalStat final {
        std::string_view field;
        std::int16_t Stats::*target;
        std::int64_t maximum;
        std::string_view because;
    };
    static constexpr std::int64_t chance_maximum =
        std::numeric_limits<std::int16_t>::max();
    static constexpr std::array<OptionalStat, 4> richer{
        {{"skill", &Stats::skill, chance_maximum, {}},
         {"luck", &Stats::luck, chance_maximum, {}},
         {"evasion", &Stats::evasion, chance_maximum, {}},
         {"magic", &Stats::magic, damage_stat_maximum, damage_stat_reason}}
    };
    for (const OptionalStat& stat : richer) {
        if (mapper.member(*values, stat.field, path + ".baseStats", false) ==
            nullptr) {
            continue;
        }
        const auto authored = mapper.integer(
            *values, stat.field, path + ".baseStats", 0, stat.maximum, true,
            stat.because
        );
        if (authored) {
            result.*stat.target = static_cast<std::int16_t>(*authored);
        }
    }
    return result;
}

void reject_unmapped_runtime_data(
    Mapper& mapper,
    const Json& value,
    const std::string& path
) {
    if (const auto* object = std::get_if<Object>(&value.value)) {
        for (const auto& [key, child] : *object) {
            const std::string child_path = path + "." + key;
            if (key == "extensions" || key == "scriptBindings") {
                bool empty = false;
                if (const auto* child_object =
                        std::get_if<Object>(&child.value)) {
                    empty = child_object->empty();
                } else if (const auto* child_array =
                               std::get_if<Array>(&child.value)) {
                    empty = child_array->empty();
                }
                if (!empty) {
                    mapper.error(
                        SourceDiagnosticCode::unsupported_content,
                        child_path,
                        "runtime extension data cannot be represented in the "
                        "native vertical package"
                    );
                }
            } else {
                reject_unmapped_runtime_data(mapper, child, child_path);
            }
        }
    } else if (const auto* array = std::get_if<Array>(&value.value)) {
        for (std::size_t index = 0; index < array->size(); ++index) {
            reject_unmapped_runtime_data(
                mapper, (*array)[index],
                path + "[" + std::to_string(index) + "]"
            );
        }
    }
}

// One roster member, wherever the author wrote them: the campaign's founding
// company, or the node whose completion brings them in. The shape is the same
// in both places on purpose: a recruit is a member of the company from the
// moment they join and is nothing to it before.
CampaignMember read_member(
    Mapper& mapper,
    const Json& value,
    const std::string& path,
    StableId join_node_id
) {
    CampaignMember member;
    const Object* object = mapper.object(value, path);
    if (object == nullptr) return member;
    member.id = mapper.id(*object, "campaign_members", path);
    const auto name = mapper.string(*object, "name", path);
    if (name) member.name = *name;
    member.unit_type_id = mapper.reference(*object, "unitTypeId", path);
    member.join_node_id = join_node_id;

    // What makes this character more than their class, if the author said
    // anything. Read as written and not checked here: whether a delta is
    // reachable depends on the class it lands on, which this pass does not
    // hold, so the bounds are the compiler's to decide and this is only the
    // reading. A stated object is recorded as stated even when it turns out to
    // hold nothing, because "stated and empty" is a refusal and "omitted" is
    // not, and only this pass can tell them apart.
    const Json* stated = mapper.member(*object, "specificity", path, false);
    if (stated != nullptr) {
        member.states_specificity = true;
        const std::string specificity_path = path + ".specificity";
        const Object* fields = mapper.object(*stated, specificity_path);
        if (fields != nullptr) {
            const Json* stats =
                mapper.member(*fields, "stats", specificity_path, false);
            if (stats != nullptr) {
                const std::string stats_path = specificity_path + ".stats";
                const Object* deltas = mapper.object(*stats, stats_path);
                if (deltas != nullptr) {
                    for (std::size_t index = 0; index < specific_stat_count;
                         ++index) {
                        const std::string_view field = specific_stat_name(
                            static_cast<SpecificStat>(index)
                        );
                        if (mapper.member(
                                *deltas, field, stats_path, false
                            ) == nullptr) {
                            continue;
                        }
                        const auto delta = mapper.integer(
                            *deltas, field, stats_path, -32767, 32767
                        );
                        member.specificity.stated[index] = true;
                        if (delta) {
                            member.specificity.stat_deltas[index] =
                                static_cast<std::int16_t>(*delta);
                        }
                    }
                }
            }
            if (mapper.member(
                    *fields, "rangeBonus", specificity_path, false
                ) != nullptr) {
                const auto bonus = mapper.integer(
                    *fields, "rangeBonus", specificity_path, 1, 32
                );
                if (bonus) {
                    member.specificity.reach_bonus =
                        static_cast<std::uint8_t>(*bonus);
                }
            }
        }
    }
    return member;
}

// One item grant, wherever the author wrote it: the campaign's starting store,
// or the node whose completion puts it there. The shape is the same in both
// places for the same reason a recruit's shape is the same as a founding
// member's: it is the same fact told at two moments.
CampaignItemGrant read_grant(
    Mapper& mapper,
    const Json& value,
    const std::string& path,
    StableId join_node_id
) {
    CampaignItemGrant grant;
    const Object* object = mapper.object(value, path);
    if (object == nullptr) return grant;
    // An item or a weapon, and never both. Neither is read as required
    // separately: a grant naming nothing is refused by the compiler where
    // every other thing wrong with a grant is refused, in a sentence rather
    // than as two missing fields.
    grant.item_id = mapper.reference(*object, "itemId", path, false);
    grant.weapon_id = mapper.reference(*object, "weaponId", path, false);
    const auto quantity = mapper.integer(*object, "quantity", path, 1, 65535);
    if (quantity) grant.quantity = static_cast<std::uint32_t>(*quantity);
    grant.join_node_id = join_node_id;
    return grant;
}

}  // namespace

std::string_view source_diagnostic_name(SourceDiagnosticCode code) noexcept {
    switch (code) {
        case SourceDiagnosticCode::invalid_json: return "invalid_json";
        case SourceDiagnosticCode::missing_value: return "missing_value";
        case SourceDiagnosticCode::invalid_value: return "invalid_value";
        case SourceDiagnosticCode::unsupported_content:
            return "unsupported_content";
        case SourceDiagnosticCode::stable_id_collision:
            return "stable_id_collision";
    }
    return "unknown";
}

SourceParseResult parse_source_project_json(std::string_view json) {
    Mapper mapper;
    Json document;
    try {
        document = JsonParser(json).parse();
    } catch (const JsonError& error) {
        mapper.error(
            SourceDiagnosticCode::invalid_json,
            "$",
            "byte " + std::to_string(error.offset()) + ": " + error.what()
        );
        return std::move(mapper.result);
    }
    const Object* root = mapper.object(document, "$");
    if (root == nullptr) return std::move(mapper.result);
    reject_unmapped_runtime_data(mapper, document, "$");

    const auto schema_version = mapper.string(*root, "schemaVersion", "$");
    if (schema_version && *schema_version != supported_source_schema) {
        // Named, both ends, and with the way out. "unsupported" on its own
        // leaves a person holding a file and no idea whether the tools are old,
        // the file is old, or the file was never a project. Those are three
        // different problems with three different answers, and only this end
        // knows which one it is looking at.
        //
        // **Which way out depends on which direction it is**, and offering the
        // wrong one is worse than offering none: an author sent to `upgrade.mjs`
        // with a file from a newer Grandleon is sent to a tool that will refuse
        // them, because going backwards can only be done by throwing something
        // away. `parse_revision` orders the two, because it packs three
        // components monotonically, which is the same arithmetic a comparison
        // needs. An unparseable version falls through to the sentence that
        // promises nothing.
        const auto found = parse_revision(*schema_version);
        const auto reads = parse_revision(supported_source_schema);
        const std::string both =
            "this project was made with source schema " + *schema_version +
            "; this compiler reads " + std::string(supported_source_schema);
        std::string way_out;
        if (found && reads && *found > *reads) {
            way_out =
                ". Upgrade the tools rather than the file: going backwards can "
                "only be done by throwing something away.";
        } else if (found && reads) {
            way_out =
                ". Bring it up with `node tools/source_schema/upgrade.mjs "
                "<project.json>`, or open it in the editor, which offers to.";
        } else {
            way_out = ".";
        }
        mapper.error(
            SourceDiagnosticCode::invalid_value, "$.schemaVersion",
            both + way_out
        );
    }
    const auto package_id = mapper.string(*root, "packageId", "$");
    if (package_id &&
        !parse_uuid(*package_id, mapper.result.source.game_id)) {
        mapper.error(
            SourceDiagnosticCode::invalid_value,
            "$.packageId",
            "expected UUID package identity"
        );
    }
    const auto title = mapper.string(*root, "title", "$");
    if (title) mapper.result.source.title = *title;
    const auto revision = mapper.string(*root, "contentRevision", "$");
    if (revision) {
        const auto packed = parse_revision(*revision);
        if (packed) {
            mapper.result.source.content_revision = *packed;
        } else {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.contentRevision",
                "revision must have three components of at most 1023; "
                "pre-release revisions are not representable in .gpk yet"
            );
        }
    }
    const auto theme = mapper.string(*root, "themeId", "$", false);
    if (theme) {
        const auto index = theme_index(*theme);
        if (index < theme_count) {
            mapper.result.source.theme = index;
        } else {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.themeId",
                "not a season the art library offers"
            );
        }
    }
    const auto character_style =
        mapper.string(*root, "characterStyleId", "$", false);
    if (character_style) {
        const auto index = character_style_index(*character_style);
        if (index < character_style_count) {
            mapper.result.source.character_style = index;
        } else {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.characterStyleId",
                "not a character style the art library offers"
            );
        }
    }
    const auto character_figure =
        mapper.string(*root, "characterFigureId", "$", false);
    if (character_figure) {
        const auto index = character_figure_index(*character_figure);
        if (index < character_figure_count) {
            mapper.result.source.character_figure = index;
        } else {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.characterFigureId",
                "not a figure the art library draws"
            );
        }
    }
    const auto character_geometry =
        mapper.string(*root, "characterGeometry", "$", false);
    if (character_geometry) {
        const auto index = character_geometry_index(*character_geometry);
        if (index < character_geometry_count) {
            mapper.result.source.character_geometry = index;
        } else {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.characterGeometry",
                "not a way the art library draws a character"
            );
        }
    }
    // The order every battle takes that does not state one of its own. It is
    // resolved here and carried no further than the encounters below: the
    // package records each encounter's resolved order and nothing about how it
    // was reached, so a project that states no default compiles to the bytes it
    // always did.
    TurnOrder default_turn_order = TurnOrder::alternating;
    const auto project_order =
        mapper.string(*root, "defaultTurnOrder", "$", false);
    if (project_order) {
        if (*project_order == "alternating") {
            default_turn_order = TurnOrder::alternating;
        } else if (*project_order == "sideBlocks") {
            default_turn_order = TurnOrder::side_blocks;
        } else if (*project_order == "initiative") {
            default_turn_order = TurnOrder::initiative;
        } else {
            // Refused rather than replaced: a reader that quietly ran
            // alternating would hide a misspelling that changes every battle.
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.defaultTurnOrder",
                "expected 'alternating', 'sideBlocks', or 'initiative'"
            );
        }
    }
    // What a fall costs the company. Unlike the turn order above, this one is
    // not resolved away here: it is carried on the source and written on every
    // campaign record, because the campaign is what holds a company and a
    // company is what a fall takes from. A project that states nothing leaves
    // it permanent and its campaign records unchanged, byte for byte.
    const auto character_loss =
        mapper.string(*root, "characterLoss", "$", false);
    if (character_loss) {
        if (*character_loss == "permanent") {
            mapper.result.source.character_loss = CharacterLoss::permanent;
        } else if (*character_loss == "recoverable") {
            mapper.result.source.character_loss = CharacterLoss::recoverable;
        } else {
            // Refused rather than replaced, for the reason the turn order
            // above is: a reader that quietly fell back to permanent loss
            // would hide a misspelling that costs a player their characters.
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.characterLoss",
                "expected 'permanent' or 'recoverable'"
            );
        }
    }
    // And whether anybody in the company can fall at all. A testing aid, and
    // read here beside the rule it is not a value of, because it travels the
    // same road: declared once by the project, written on every campaign
    // record, and read back by whoever holds the package. Omitted is nobody
    // protected, which is what every project before this said.
    const Json* invulnerable =
        mapper.member(*root, "invulnerableForTesting", "$", false);
    if (invulnerable != nullptr) {
        const auto* flag = std::get_if<bool>(&invulnerable->value);
        if (flag == nullptr) {
            mapper.error(
                SourceDiagnosticCode::invalid_value,
                "$.invulnerableForTesting",
                "expected a boolean"
            );
        } else {
            mapper.result.source.invulnerable_for_testing = *flag;
        }
    }
    mapper.result.source.required_engine = {{0, 1, 0}, {0, 1, 99}};

    mapper.result.source.weapon_types =
        mapper.definitions<WeaponType>(
            *root, "weaponTypes", false,
            [&mapper](const Object& value, const std::string& path) {
                WeaponType result;
                result.id = mapper.id(value, "weaponTypes", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                return result;
            }
        );
    mapper.result.source.item_types =
        mapper.definitions<ItemType>(
            *root, "itemTypes", false,
            [&mapper](const Object& value, const std::string& path) {
                ItemType result;
                result.id = mapper.id(value, "itemTypes", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                return result;
            }
        );
    mapper.result.source.classes =
        mapper.definitions<UnitClass>(
            *root, "classes", true,
            [&mapper](const Object& value, const std::string& path) {
                UnitClass result;
                result.id = mapper.id(value, "classes", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                result.base_stats = stats(mapper, value, path);
                const Json* after = mapper.member(
                    value, "actsAfterAttacking", path, false
                );
                if (after != nullptr) {
                    const auto* flag = std::get_if<bool>(&after->value);
                    if (flag == nullptr) {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".actsAfterAttacking",
                            "expected a boolean"
                        );
                    } else {
                        result.acts_after_attacking = *flag;
                    }
                }
                // Stating an allowance and stating none are different facts,
                // and the vector alone cannot tell them apart: both come out
                // empty. Omitted is unrestricted access; present and empty
                // permits no weapon type at all.
                result.states_allowed_weapon_types =
                    mapper.member(
                        value, "allowedWeaponTypeIds", path, false
                    ) != nullptr;
                result.allowed_weapon_types = mapper.references(
                    value, "allowedWeaponTypeIds", path
                );
                // What this class crosses. Omitted is a walker, which is what
                // every class authored before terrain could stop anyone is.
                const Json* traversal =
                    mapper.member(value, "traversal", path, false);
                if (traversal != nullptr) {
                    const Object* fields =
                        mapper.object(*traversal, path + ".traversal");
                    if (fields != nullptr) {
                        const std::string traversal_path = path + ".traversal";
                        const Json* flying = mapper.member(
                            *fields, "flying", traversal_path, false
                        );
                        if (flying != nullptr) {
                            const auto* wings =
                                std::get_if<bool>(&flying->value);
                            if (wings == nullptr) {
                                mapper.error(
                                    SourceDiagnosticCode::invalid_value,
                                    traversal_path + ".flying",
                                    "expected a boolean"
                                );
                            } else if (*wings) {
                                result.crossings = static_cast<std::uint8_t>(
                                    result.crossings | crossing_every
                                );
                            }
                        }
                        const Array* crossings = mapper.array(
                            *fields, "crossings", traversal_path, false
                        );
                        if (crossings != nullptr) {
                            for (std::size_t index = 0;
                                 index < crossings->size(); ++index) {
                                const auto* named = std::get_if<std::string>(
                                    &(*crossings)[index].value
                                );
                                const std::uint8_t bit =
                                    named == nullptr
                                        ? crossing_none
                                        : crossing_bit(*named);
                                if (bit == crossing_none) {
                                    mapper.error(
                                        SourceDiagnosticCode::invalid_value,
                                        traversal_path + ".crossings[" +
                                            std::to_string(index) + "]",
                                        "expected a terrain this vocabulary "
                                        "names"
                                    );
                                    continue;
                                }
                                result.crossings = static_cast<std::uint8_t>(
                                    result.crossings | bit
                                );
                            }
                        }
                    }
                }
                return result;
            }
        );
    mapper.result.source.weapons =
        mapper.definitions<Weapon>(
            *root, "weapons", true,
            [&mapper](const Object& value, const std::string& path) {
                Weapon result;
                result.id = mapper.id(value, "weapons", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                // Optional, as the schema and `SOURCE_FORMAT.md` both have it:
                // a weapon that names no type is legacy unclassified content
                // with unknown compatibility, not a malformed weapon.
                result.type_id =
                    mapper.reference(value, "weaponTypeId", path, false);
                const auto power = mapper.integer(
                    value, "power", path, 0, damage_stat_maximum, true,
                    damage_stat_reason
                );
                if (power) result.power = static_cast<std::int16_t>(*power);
                // An explicit band wins. `range` alone is the legacy spelling
                // and means one to range, so an author who never asked for a
                // minimum does not get one.
                const bool has_band =
                    mapper.member(value, "minimumRange", path, false) != nullptr ||
                    mapper.member(value, "maximumRange", path, false) != nullptr;
                if (has_band) {
                    const auto minimum = mapper.integer(
                        value, "minimumRange", path, 1,
                        std::numeric_limits<std::uint8_t>::max()
                    );
                    const auto maximum = mapper.integer(
                        value, "maximumRange", path, 1,
                        std::numeric_limits<std::uint8_t>::max()
                    );
                    result.minimum_range =
                        minimum ? static_cast<std::uint8_t>(*minimum) : 1U;
                    result.maximum_range =
                        maximum ? static_cast<std::uint8_t>(*maximum)
                                : result.minimum_range;
                    if (result.minimum_range > result.maximum_range) {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".minimumRange",
                            "minimum range cannot exceed maximum range"
                        );
                    }
                } else {
                    const auto range = mapper.integer(
                        value, "range", path, 1,
                        std::numeric_limits<std::uint8_t>::max()
                    );
                    if (range) {
                        result.minimum_range = 1U;
                        result.maximum_range =
                            static_cast<std::uint8_t>(*range);
                    }
                }
                // How often it lands. Absent means certain, which is what
                // every weapon authored before attacks could miss was.
                if (mapper.member(value, "accuracy", path, false) != nullptr) {
                    const auto accuracy =
                        mapper.integer(value, "accuracy", path, 0, 100);
                    if (accuracy) {
                        result.accuracy =
                            static_cast<std::uint8_t>(*accuracy);
                    }
                }
                return result;
            }
        );
    mapper.result.source.items =
        mapper.definitions<Item>(
            *root, "items", true,
            [&mapper](const Object& value, const std::string& path) {
                Item result;
                result.id = mapper.id(value, "items", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                // Optional, on the weapon type's exact terms.
                result.type_id =
                    mapper.reference(value, "itemTypeId", path, false);
                const auto limit = mapper.integer(
                    value, "stackLimit", path, 1,
                    std::numeric_limits<std::uint16_t>::max()
                );
                if (limit) {
                    result.stack_limit = static_cast<std::uint16_t>(*limit);
                }
                const auto kind = mapper.string(value, "kind", path, false);
                if (kind && *kind == "restore") {
                    result.kind = ItemKind::restore;
                } else if (kind) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".kind",
                        "expected 'restore'"
                    );
                }
                // How much it gives back. Absent means nothing, which is what
                // every item authored before items could be spent gives back.
                if (mapper.member(value, "power", path, false) != nullptr) {
                    const auto power = mapper.integer(
                        value, "power", path, 0, damage_stat_maximum, true,
                        damage_stat_reason
                    );
                    if (power) {
                        result.power = static_cast<std::int16_t>(*power);
                    }
                }
                return result;
            }
        );
    mapper.result.source.unit_types =
        mapper.definitions<UnitType>(
            *root, "unitTypes", true,
            [&mapper](const Object& value, const std::string& path) {
                UnitType result;
                result.id = mapper.id(value, "unitTypes", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                result.class_id = mapper.reference(value, "classId", path);
                result.faction_id = mapper.reference(
                    value, "factionId", path, false
                );
                result.starting_weapons = mapper.references(
                    value, "startingWeaponIds", path
                );
                result.starting_items = mapper.references(
                    value, "startingItemIds", path
                );
                result.abilities = mapper.references(
                    value, "abilityIds", path
                );
                // The style this one character is drawn in. Absent is not a
                // value: it means "the game's", which is resolved at compile
                // time and never written back here, so a project that says
                // nothing keeps saying nothing.
                const auto unit_style =
                    mapper.string(value, "characterStyleId", path, false);
                if (unit_style) {
                    const auto index = character_style_index(*unit_style);
                    if (index < character_style_count) {
                        result.character_style = index;
                    } else {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".characterStyleId",
                            "not a character style the art library offers"
                        );
                    }
                }
                // And the body it is drawn with, read the same way and absent
                // for the same reason.
                const auto unit_figure =
                    mapper.string(value, "characterFigureId", path, false);
                if (unit_figure) {
                    const auto index = character_figure_index(*unit_figure);
                    if (index < character_figure_count) {
                        result.character_figure = index;
                    } else {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".characterFigureId",
                            "not a figure the art library draws"
                        );
                    }
                }
                // What a battle is worth and what a level costs. Absent means
                // the defaults every unit type authored before growth existed
                // reads as: nothing to defeat, a hundred per level.
                if (mapper.member(value, "experienceAward", path, false) !=
                    nullptr) {
                    const auto award = mapper.integer(
                        value, "experienceAward", path, 0,
                        std::numeric_limits<std::uint16_t>::max()
                    );
                    if (award) {
                        result.experience_award =
                            static_cast<std::uint16_t>(*award);
                    }
                }
                if (mapper.member(value, "experiencePerLevel", path, false) !=
                    nullptr) {
                    const auto per_level = mapper.integer(
                        value, "experiencePerLevel", path, 1,
                        std::numeric_limits<std::uint16_t>::max()
                    );
                    if (per_level) {
                        result.experience_per_level =
                            static_cast<std::uint16_t>(*per_level);
                    }
                }
                // What this one leaves behind, and how often. Both absent is a
                // unit type that leaves nothing and rolls nothing; either one
                // alone is refused by the compiler, which is where the two
                // halves can be compared against each other.
                result.drop_item =
                    mapper.reference(value, "dropItemId", path, false);
                if (mapper.member(value, "dropChance", path, false) !=
                    nullptr) {
                    const auto chance =
                        mapper.integer(value, "dropChance", path, 1, 100);
                    if (chance) {
                        result.drop_chance =
                            static_cast<std::uint8_t>(*chance);
                    }
                }
                // The growth rates, read in the one order every level-up rolls
                // them in. An absent block, and an absent stat inside a
                // present one, are both zero: never rolled, never grown.
                const Json* growth =
                    mapper.member(value, "growthRates", path, false);
                if (growth != nullptr) {
                    const std::string growth_path = path + ".growthRates";
                    const Object* fields = mapper.object(*growth, growth_path);
                    if (fields != nullptr) {
                        static constexpr std::array<
                            std::string_view, growable_stat_count>
                            growth_fields{
                                "health",     "strength",
                                "defense",    "resistance",
                                "movement",   "actionPoints",
                                "skill",      "luck",
                                "evasion",    "magic"
                            };
                        for (std::size_t index = 0;
                             index < growable_stat_count; ++index) {
                            const std::string_view field = growth_fields[index];
                            if (mapper.member(
                                    *fields, field, growth_path, false
                                ) == nullptr) {
                                continue;
                            }
                            const auto chance = mapper.integer(
                                *fields, field, growth_path, 0, 100
                            );
                            if (chance) {
                                result.growth.chance[index] =
                                    static_cast<std::uint8_t>(*chance);
                            }
                        }
                    }
                }
                return result;
            }
        );

    mapper.result.source.maps =
        mapper.definitions<Map>(
            *root, "maps", true,
            [&mapper](const Object& value, const std::string& path) {
                Map result;
                result.id = mapper.id(value, "maps", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                // The bounds `map.schema.json` states, held to here as well.
                // The wider `uint16` the field is stored in is not the rule: a
                // board past this size has coordinates a placement's signed
                // sixteen bits cannot express, and the only thing that refused
                // it before was a cast going negative further downstream.
                const auto width = mapper.integer(
                    value, "width", path, 1, maximum_map_side
                );
                const auto height = mapper.integer(
                    value, "height", path, 1, maximum_map_side
                );
                if (width) result.width = static_cast<std::uint16_t>(*width);
                if (height) result.height = static_cast<std::uint16_t>(*height);
                result.terrain = mapper.references(value, "terrain", path);
                result.terrain_kinds = mapper.terrain_kinds(value, "terrain", path);
                return result;
            }
        );
    mapper.result.source.factions =
        mapper.definitions<Faction>(
            *root, "factions", false,
            [&mapper](const Object& value, const std::string& path) {
                Faction result;
                result.id = mapper.id(value, "factions", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                const auto colour = mapper.string(value, "color", path, false);
                if (colour) {
                    result.colour = faction_colour_index(*colour);
                    if (result.colour == faction_colour_unchosen) {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".color",
                            "not a colour the art library offers"
                        );
                    }
                }
                return result;
            }
        );
    mapper.result.source.objectives =
        mapper.definitions<Objective>(
            *root, "objectives", false,
            [&mapper](const Object& value, const std::string& path) {
                Objective result;
                result.id = mapper.id(value, "objectives", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                const auto kind = mapper.string(value, "kind", path, false);
                if (!kind || *kind == "defeatAllOpponents") {
                    result.kind = ObjectiveKind::defeat_all_opponents;
                } else if (*kind == "defeatTarget") {
                    result.kind = ObjectiveKind::defeat_target;
                } else if (*kind == "protectTarget") {
                    result.kind = ObjectiveKind::protect_target;
                } else if (*kind == "surviveRounds") {
                    result.kind = ObjectiveKind::survive_rounds;
                } else {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".kind",
                        "expected 'defeatAllOpponents', 'defeatTarget', "
                        "'protectTarget', or 'surviveRounds'"
                    );
                }
                const auto side = mapper.string(value, "side", path, false);
                result.side = side && *side == "second"
                                  ? ObjectiveSide::second
                                  : ObjectiveSide::first;
                if (side && *side != "first" && *side != "second") {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".side",
                        "expected 'first' or 'second'"
                    );
                }
                const bool needs_target =
                    result.kind == ObjectiveKind::defeat_target ||
                    result.kind == ObjectiveKind::protect_target;
                const auto target =
                    mapper.string(value, "targetPlacementId", path, false);
                if (needs_target && !target) {
                    mapper.error(
                        SourceDiagnosticCode::missing_value,
                        path + ".targetPlacementId",
                        "defeatTarget and protectTarget need a target placement"
                    );
                }
                // Placements are not a top-level registry, so this is hashed
                // directly rather than resolved through mapper.reference. The
                // encounter section carries the same value per placement.
                if (target) {
                    result.target_placement_id =
                        core::stable_content_id_v1(*target);
                }
                // The count and the kind are one authored fact, so a survive
                // objective with no count and a count on a kind that could
                // never read one are both said out loud rather than silently
                // defaulted or dropped.
                const bool counts =
                    result.kind == ObjectiveKind::survive_rounds;
                const auto rounds =
                    mapper.integer(value, "rounds", path, 1, 65535, false);
                if (counts && !rounds) {
                    mapper.error(
                        SourceDiagnosticCode::missing_value,
                        path + ".rounds",
                        "surviveRounds needs the number of rounds to survive"
                    );
                } else if (!counts && rounds) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".rounds",
                        "only surviveRounds reads a round count"
                    );
                }
                if (counts && rounds) {
                    result.rounds = static_cast<std::uint16_t>(*rounds);
                }
                return result;
            }
        );

    mapper.result.source.abilities =
        mapper.definitions<Ability>(
            *root, "abilities", false,
            [&mapper](const Object& value, const std::string& path) {
                Ability result;
                result.id = mapper.id(value, "abilities", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                const auto kind = mapper.string(value, "kind", path);
                if (kind && *kind == "restore") {
                    result.kind = AbilityKind::restore;
                } else if (kind && *kind == "damage") {
                    result.kind = AbilityKind::damage;
                } else if (kind) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".kind",
                        "expected 'damage' or 'restore'"
                    );
                }
                // Refused rather than replaced, for the reason the turn order
                // and the loss rule are: a misspelling quietly read as
                // physical changes what the cast is reduced by and what stat
                // it reads, and the author would never learn that their
                // fireball had stopped being magic.
                const auto damage_type =
                    mapper.string(value, "damageType", path, false);
                if (!damage_type || *damage_type == "physical") {
                    result.damage_type = DamageType::physical;
                } else if (*damage_type == "magical") {
                    result.damage_type = DamageType::magical;
                } else {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".damageType",
                        "expected 'physical' or 'magical'"
                    );
                }
                const auto area =
                    mapper.string(value, "areaShape", path, false);
                if (!area || *area == "single") {
                    result.area = AreaShape::single;
                } else if (*area == "cross") {
                    result.area = AreaShape::cross;
                } else if (*area == "diamond") {
                    result.area = AreaShape::diamond;
                } else {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".areaShape",
                        "expected 'single', 'cross', or 'diamond'"
                    );
                }
                const auto power = mapper.integer(
                    value, "power", path, 0, damage_stat_maximum, true,
                    damage_stat_reason
                );
                if (power) result.power = static_cast<std::int16_t>(*power);
                const auto minimum = mapper.integer(
                    value, "minimumRange", path, 1,
                    std::numeric_limits<std::uint8_t>::max()
                );
                const auto maximum = mapper.integer(
                    value, "maximumRange", path, 1,
                    std::numeric_limits<std::uint8_t>::max()
                );
                if (minimum) {
                    result.minimum_range = static_cast<std::uint8_t>(*minimum);
                }
                if (maximum) {
                    result.maximum_range = static_cast<std::uint8_t>(*maximum);
                }
                if (minimum && maximum && *minimum > *maximum) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".minimumRange",
                        "minimum range cannot exceed maximum range"
                    );
                }
                if (mapper.member(value, "radius", path, false) != nullptr) {
                    const auto radius = mapper.integer(
                        value, "radius", path, 0,
                        std::numeric_limits<std::uint8_t>::max()
                    );
                    if (radius) {
                        result.radius = static_cast<std::uint8_t>(*radius);
                    }
                }
                // How often the cast lands. Absent means certain, as it does
                // on a weapon.
                if (mapper.member(value, "accuracy", path, false) != nullptr) {
                    const auto accuracy =
                        mapper.integer(value, "accuracy", path, 0, 100);
                    if (accuracy) {
                        result.accuracy =
                            static_cast<std::uint8_t>(*accuracy);
                    }
                }
                return result;
            }
        );
    mapper.result.source.dialogues =
        mapper.definitions<Dialogue>(
            *root, "dialogues", false,
            [&mapper](const Object& value, const std::string& path) {
                Dialogue result;
                result.id = mapper.id(value, "dialogues", path);
                const auto name = mapper.string(value, "name", path);
                if (name) result.name = *name;
                // What the scene is drawn against, held as the menu index plus
                // one so that zero stays "names none". Refused by name here as
                // well as by the schema, for the reason a theme and a style
                // are: a package built by a tool that skipped the schema must
                // not reach a client holding a backdrop nothing can draw.
                const auto backdrop =
                    mapper.string(value, "backgroundId", path, false);
                if (backdrop) {
                    const auto index = backdrop_index(*backdrop);
                    if (index < backdrop_count) {
                        result.backdrop = static_cast<std::uint8_t>(index + 1);
                    } else {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".backgroundId",
                            "not a backdrop the art library offers"
                        );
                    }
                }
                // Who each speaker is. Read before the lines because the lines
                // are joined to it below, and refused a second entry for one
                // speaker here: two answers to "who is Mirea" is a question
                // this reader must not settle by picking one.
                const Array* cast = mapper.array(value, "cast", path, false);
                // Where each kept entry was authored. A refused entry leaves a
                // hole, so a diagnostic reported after the lines are read must
                // name the position an author can find rather than the
                // position the survivor ended up at.
                std::vector<std::size_t> cast_authored_at;
                if (cast != nullptr) {
                    // A cast is capped so that a line can name its speaker in
                    // one byte, which is what keeps the join off every client.
                    //
                    // Asked of the authored array before a single entry is
                    // read, rather than of the surviving list afterwards. The
                    // answer is the same either way; what differs is the work
                    // spent reaching it. Sixty thousand entries took seventy
                    // seconds to be told they were fifty-nine thousand too
                    // many, because every one of them was read, allocated and
                    // compared against every entry before it. On a cartridge
                    // that is a hang rather than a refusal.
                    const std::size_t authored = cast->size();
                    const std::size_t kept =
                        authored > maximum_cast ? maximum_cast : authored;
                    if (authored > maximum_cast) {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            path + ".cast",
                            "a scene may cast at most 255 speakers"
                        );
                    }
                    for (std::size_t index = 0; index < kept; ++index) {
                        const std::string cast_path =
                            path + ".cast[" + std::to_string(index) + "]";
                        const Object* entry =
                            mapper.object((*cast)[index], cast_path);
                        if (entry == nullptr) continue;
                        DialogueCastEntry output;
                        const auto speaker =
                            mapper.string(*entry, "speaker", cast_path);
                        output.unit_type_id =
                            mapper.reference(*entry, "unitTypeId", cast_path);
                        if (!speaker) continue;
                        output.speaker = *speaker;
                        const bool repeated = std::any_of(
                            result.cast.begin(), result.cast.end(),
                            [&output](const DialogueCastEntry& seen) {
                                return seen.speaker == output.speaker;
                            }
                        );
                        if (repeated) {
                            mapper.error(
                                SourceDiagnosticCode::invalid_value,
                                cast_path + ".speaker",
                                "'" + output.speaker +
                                    "' is cast twice in this scene"
                            );
                            continue;
                        }
                        result.cast.push_back(std::move(output));
                        cast_authored_at.push_back(index);
                    }
                }
                // A scene may author no lines at all, which the schema allows
                // and which changes nothing about the cast: an entry nobody
                // speaks for is still an entry nobody speaks for. The
                // reconciliation below therefore runs whether or not there are
                // lines to reconcile against, because the presence of one key
                // must not decide whether a documented refusal exists.
                const Array* lines =
                    mapper.array(value, "lines", path, false);
                for (std::size_t index = 0;
                     lines != nullptr && index < lines->size(); ++index) {
                    const std::string line_path =
                        path + ".lines[" + std::to_string(index) + "]";
                    const Object* line =
                        mapper.object((*lines)[index], line_path);
                    if (line == nullptr) continue;
                    DialogueLine output;
                    const auto speaker =
                        mapper.string(*line, "speaker", line_path);
                    const auto text = mapper.string(*line, "text", line_path);
                    if (speaker) output.speaker = *speaker;
                    if (text) output.text = *text;
                    // The join, done once and here. A line whose speaker no
                    // entry names keeps `cast_entry` at zero, which is what
                    // every line carried before a scene could name anybody.
                    for (std::size_t entry = 0; entry < result.cast.size();
                         ++entry) {
                        if (result.cast[entry].speaker == output.speaker) {
                            output.cast_entry =
                                static_cast<std::uint8_t>(entry + 1);
                            break;
                        }
                    }
                    result.lines.push_back(std::move(output));
                }
                // A cast entry that speaks no line is a mistake worth naming:
                // it is what a renamed speaker leaves behind, and its symptom
                // on screen is the old drawing rather than an error.
                for (std::size_t entry = 0; entry < result.cast.size();
                     ++entry) {
                    const bool spoken = std::any_of(
                        result.lines.begin(), result.lines.end(),
                        [entry](const DialogueLine& line) {
                            return line.cast_entry == entry + 1;
                        }
                    );
                    if (spoken) continue;
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        path + ".cast[" +
                            std::to_string(cast_authored_at[entry]) +
                            "].speaker",
                        "'" + result.cast[entry].speaker +
                            "' speaks no line in this scene"
                    );
                }
                return result;
            }
        );

    const Array* campaigns = mapper.array(*root, "campaigns", "$", false);
    if (campaigns != nullptr) {
        for (std::size_t campaign_index = 0;
             campaign_index < campaigns->size(); ++campaign_index) {
            const std::string campaign_path =
                "$.campaigns[" + std::to_string(campaign_index) + "]";
            const Object* value =
                mapper.object((*campaigns)[campaign_index], campaign_path);
            if (value == nullptr) continue;
            Campaign campaign;
            const auto campaign_key =
                mapper.string(*value, "id", campaign_path);
            campaign.id = mapper.id(*value, "campaigns", campaign_path);
            const auto campaign_name =
                mapper.string(*value, "name", campaign_path);
            if (campaign_name) campaign.name = *campaign_name;
            // The company, before the flow, because the flow's placements
            // field it and its recruits are appended to it in flow order. The
            // order this vector ends up in is the order persistent identities
            // are assigned in, so it is authored rather than incidental.
            const Array* roster =
                mapper.array(*value, "roster", campaign_path, false);
            if (roster != nullptr) {
                for (std::size_t member_index = 0;
                     member_index < roster->size(); ++member_index) {
                    const std::string member_path = campaign_path + ".roster[" +
                        std::to_string(member_index) + "]";
                    campaign.roster.push_back(
                        read_member(mapper, (*roster)[member_index], member_path, 0)
                    );
                }
            }
            // What the store is founded with, read here for the same reason
            // the company is: what a campaign begins with is decided before
            // any node of it is walked, and a grant carries a join node of
            // zero to say so.
            const Array* starting_store =
                mapper.array(*value, "startingStore", campaign_path, false);
            if (starting_store != nullptr) {
                for (std::size_t stock_index = 0;
                     stock_index < starting_store->size(); ++stock_index) {
                    const std::string stock_path = campaign_path +
                        ".startingStore[" + std::to_string(stock_index) + "]";
                    campaign.grants.push_back(read_grant(
                        mapper, (*starting_store)[stock_index], stock_path, 0
                    ));
                }
            }
            // The flow is optional to the schema, because a campaign may be
            // authored as a portable membership registry rather than as
            // something to play. It is not optional to this compiler: a
            // campaign record holding no node has no node to enter, and
            // `campaign::load_campaign` refuses one outright. Said here in
            // those words rather than reported as a missing required value the
            // schema does not require, and rather than emitted as a record
            // every runtime will decline.
            const Json* flow_json =
                mapper.member(*value, "flow", campaign_path, false);
            if (flow_json == nullptr) {
                mapper.error(
                    SourceDiagnosticCode::unsupported_content,
                    campaign_path + ".flow",
                    "a campaign with no flow has no node to enter, and no "
                    "runtime can walk one"
                );
                mapper.result.source.campaigns.push_back(std::move(campaign));
                continue;
            }
            const Object* flow =
                mapper.object(*flow_json, campaign_path + ".flow");
            if (flow == nullptr) continue;
            // The contract the flow was written against. The schema states it
            // as a constant and requires it; read here as well, on the same
            // terms `schemaVersion` is, because a flow written against a later
            // contract would otherwise be compiled against this one's rules
            // without a word being said about it.
            const auto contract = mapper.string(
                *flow, "contractVersion", campaign_path + ".flow"
            );
            if (contract && *contract != "1.0.0") {
                mapper.error(
                    SourceDiagnosticCode::invalid_value,
                    campaign_path + ".flow.contractVersion",
                    "native compiler supports exactly campaign flow contract "
                    "1.0.0"
                );
            }
            campaign.entry_node_id = mapper.reference(
                *flow, "entryNodeId", campaign_path + ".flow"
            );
            const Array* nodes =
                mapper.array(*flow, "nodes", campaign_path + ".flow");
            if (nodes == nullptr) continue;
            for (std::size_t node_index = 0;
                 node_index < nodes->size(); ++node_index) {
                const std::string node_path = campaign_path + ".flow.nodes[" +
                    std::to_string(node_index) + "]";
                const Object* node =
                    mapper.object((*nodes)[node_index], node_path);
                if (node == nullptr) continue;
                CampaignNode output_node;
                const auto node_key = mapper.string(*node, "id", node_path);
                if (node_key) {
                    output_node.id = core::stable_content_id_v1(*node_key);
                }
                const auto kind = mapper.string(*node, "kind", node_path);
                if (kind && *kind == "encounter") {
                    output_node.kind = CampaignNodeKind::encounter;
                    const std::string encounter_key =
                        campaign_key.value_or("") + "/" +
                        node_key.value_or("");
                    output_node.encounter_id =
                        core::stable_content_id_v1(encounter_key);
                    Encounter encounter;
                    encounter.id = output_node.encounter_id;
                    const auto name = mapper.string(*node, "name", node_path);
                    if (name) encounter.name = *name;
                    encounter.map_id =
                        mapper.reference(*node, "mapId", node_path);
                    encounter.objective_ids = mapper.references(
                        *node, "objectiveIds", node_path
                    );
                    // A board that states nothing takes the game's default,
                    // which is alternating when the project states none. That
                    // is what a board stating nothing has always meant.
                    const auto order =
                        mapper.string(*node, "turnOrder", node_path, false);
                    if (!order) {
                        encounter.turn_order = default_turn_order;
                    } else if (*order == "alternating") {
                        encounter.turn_order = TurnOrder::alternating;
                    } else if (*order == "sideBlocks") {
                        encounter.turn_order = TurnOrder::side_blocks;
                    } else if (*order == "initiative") {
                        encounter.turn_order = TurnOrder::initiative;
                    } else {
                        mapper.error(
                            SourceDiagnosticCode::invalid_value,
                            node_path + ".turnOrder",
                            "expected 'alternating', 'sideBlocks', or "
                            "'initiative'"
                        );
                    }
                    const Array* placements = mapper.array(
                        *node, "placements", node_path
                    );
                    if (placements != nullptr) {
                        for (std::size_t placement_index = 0;
                             placement_index < placements->size();
                             ++placement_index) {
                            const std::string placement_path =
                                node_path + ".placements[" +
                                std::to_string(placement_index) + "]";
                            const Object* placement = mapper.object(
                                (*placements)[placement_index], placement_path
                            );
                            if (placement == nullptr) continue;
                            Placement output_placement;
                            const auto placement_key = mapper.string(
                                *placement, "id", placement_path
                            );
                            if (placement_key) {
                                output_placement.id =
                                    core::stable_content_id_v1(
                                        encounter_key + "/" + *placement_key
                                    );
                                output_placement.source_key_id =
                                    core::stable_content_id_v1(*placement_key);
                            }
                            // Who stands here. The key a campaign joins its
                            // roster by is the member's, so a placement that
                            // fields one carries the member's identity as its
                            // source key: the same character across every
                            // board that places them, however each board
                            // names the tile.
                            output_placement.member_id = mapper.reference(
                                *placement, "memberId", placement_path, false
                            );
                            if (output_placement.member_id != 0) {
                                output_placement.source_key_id =
                                    output_placement.member_id;
                            }
                            output_placement.unit_type_id = mapper.reference(
                                *placement, "unitTypeId", placement_path
                            );
                            // What the author calls this character. Optional,
                            // and absent is not a gap: a placement with no name
                            // is named after its unit type, with an ordinal
                            // when the board fields more than one of that kind.
                            if (const auto placement_name = mapper.string(
                                    *placement, "name", placement_path, false
                                )) {
                                output_placement.name = *placement_name;
                            }
                            const auto side = mapper.string(
                                *placement, "side", placement_path
                            );
                            if (side && *side == "first") {
                                output_placement.side = EncounterSide::first;
                            } else if (side && *side == "second") {
                                output_placement.side = EncounterSide::second;
                            } else if (side) {
                                mapper.error(
                                    SourceDiagnosticCode::invalid_value,
                                    placement_path + ".side",
                                    "expected 'first' or 'second'"
                                );
                            }
                            const auto behavior = mapper.string(
                                *placement, "behavior", placement_path, false
                            );
                            if (!behavior || *behavior == "hold") {
                                output_placement.behavior = UnitBehavior::hold;
                            } else if (*behavior == "patrol") {
                                output_placement.behavior = UnitBehavior::patrol;
                            } else if (*behavior == "pursue") {
                                output_placement.behavior = UnitBehavior::pursue;
                            } else {
                                mapper.error(
                                    SourceDiagnosticCode::invalid_value,
                                    placement_path + ".behavior",
                                    "expected 'hold', 'patrol', or 'pursue'"
                                );
                            }
                            const Array* patrol = mapper.array(
                                *placement, "patrolPoints", placement_path, false
                            );
                            if (patrol != nullptr) {
                                for (std::size_t point_index = 0;
                                     point_index < patrol->size();
                                     ++point_index) {
                                    const std::string point_path =
                                        placement_path + ".patrolPoints[" +
                                        std::to_string(point_index) + "]";
                                    const Object* point = mapper.object(
                                        (*patrol)[point_index], point_path
                                    );
                                    if (point == nullptr) continue;
                                    PatrolPoint output_point;
                                    const auto px = mapper.integer(
                                        *point, "x", point_path,
                                        std::numeric_limits<std::int16_t>::min(),
                                        std::numeric_limits<std::int16_t>::max()
                                    );
                                    const auto py = mapper.integer(
                                        *point, "y", point_path,
                                        std::numeric_limits<std::int16_t>::min(),
                                        std::numeric_limits<std::int16_t>::max()
                                    );
                                    if (px) {
                                        output_point.x =
                                            static_cast<std::int16_t>(*px);
                                    }
                                    if (py) {
                                        output_point.y =
                                            static_cast<std::int16_t>(*py);
                                    }
                                    output_placement.patrol.push_back(
                                        output_point
                                    );
                                }
                            }
                            const auto x = mapper.integer(
                                *placement, "x", placement_path,
                                std::numeric_limits<std::int16_t>::min(),
                                std::numeric_limits<std::int16_t>::max()
                            );
                            const auto y = mapper.integer(
                                *placement, "y", placement_path,
                                std::numeric_limits<std::int16_t>::min(),
                                std::numeric_limits<std::int16_t>::max()
                            );
                            if (x) {
                                output_placement.x =
                                    static_cast<std::int16_t>(*x);
                            }
                            if (y) {
                                output_placement.y =
                                    static_cast<std::int16_t>(*y);
                            }
                            // Who can be talked to on this board, and what talking to
                // them records. An absent `talk` is the overwhelmingly common
                // case and costs nothing: no field is materialised and no
                // section is written.
                const Json* talk =
                    mapper.member(*placement, "talk", placement_path, false);
                if (talk != nullptr) {
                    const Object* talk_object =
                        mapper.object(*talk, placement_path + ".talk");
                    if (talk_object != nullptr) {
                        // Required inside the object: a `talk` that names no
                        // flag is an author saying somebody is talkable and
                        // not saying what talking to them does, which nothing
                        // downstream could act on.
                        output_placement.talk_flag_id = mapper.reference(
                            *talk_object, "flagId", placement_path + ".talk"
                        );
                    }
                }
                // When this character comes in, if it is not here from the
                // opening. Absent is the overwhelmingly common case and costs
                // nothing: no field is materialised and no section is written.
                //
                // The recurrence is carried as authored rather than expanded
                // here. The simulation expands it, so the browser (which never
                // sees a package) and the consoles cannot disagree about what
                // "every three rounds, four times" means.
                const Json* arrival = mapper.member(
                    *placement, "arrival", placement_path, false
                );
                if (arrival != nullptr) {
                    const std::string arrival_path = placement_path + ".arrival";
                    const Object* arrival_object =
                        mapper.object(*arrival, arrival_path);
                    if (arrival_object != nullptr) {
                        const auto round = mapper.integer(
                            *arrival_object, "round", arrival_path, 2, 4095
                        );
                        if (round) {
                            output_placement.arrival_round =
                                static_cast<std::uint16_t>(*round);
                        }
                        const auto every = mapper.integer(
                            *arrival_object, "every", arrival_path, 1, 4095,
                            false
                        );
                        const auto times = mapper.integer(
                            *arrival_object, "times", arrival_path, 2, 64,
                            false
                        );
                        // Both halves of the recurrence or neither. A gap with
                        // no number of arrivals is a stream no battle could
                        // outlast; a number with no gap is a stack rather than
                        // a wave.
                        if (every.has_value() != times.has_value()) {
                            mapper.error(
                                SourceDiagnosticCode::invalid_value,
                                arrival_path,
                                "'every' and 'times' are authored together or "
                                "not at all"
                            );
                        } else if (every && times) {
                            output_placement.arrival_every =
                                static_cast<std::uint16_t>(*every);
                            output_placement.arrival_times =
                                static_cast<std::uint16_t>(*times);
                        }
                    }
                }
                encounter.placements.push_back(output_placement);
                        }
                    }
                    // What is said while this battle is on. Absent is the
                    // overwhelmingly common case and costs nothing: no field
                    // is materialised and no section is written, so a battle
                    // nobody speaks during is the package it always was.
                    {
                        const std::string moments_path = node_path + ".moments";
                        const Array* moments =
                            mapper.array(*node, "moments", node_path, false);
                        if (moments != nullptr) {
                            std::size_t at = 0;
                            for (const Json& entry : *moments) {
                                const std::string moment_path =
                                    moments_path + "[" + std::to_string(at++) + "]";
                                const Object* moment =
                                    mapper.object(entry, moment_path);
                                if (moment == nullptr) continue;
                                Moment output_moment;
                                output_moment.id =
                                    mapper.reference(*moment, "id", moment_path);
                                output_moment.dialogue_id = mapper.reference(
                                    *moment, "dialogueId", moment_path
                                );
                                const Json* when = mapper.member(
                                    *moment, "when", moment_path, true
                                );
                                if (when != nullptr) {
                                    const std::string when_path =
                                        moment_path + ".when";
                                    const Object* when_object =
                                        mapper.object(*when, when_path);
                                    if (when_object != nullptr) {
                                        const auto kind = mapper.string(
                                            *when_object, "kind", when_path
                                        );
                                        // The schema's enum has already
                                        // refused anything else; this is the
                                        // join from its word to the byte.
                                        if (kind && *kind == "characterTalked") {
                                            output_moment.trigger =
                                                MomentTrigger::character_talked;
                                        } else if (kind &&
                                                   *kind == "characterFalls") {
                                            output_moment.trigger =
                                                MomentTrigger::character_falls;
                                        } else {
                                            output_moment.trigger =
                                                MomentTrigger::stage_opens;
                                        }
                                        // Named by the author's own placement
                                        // key, hashed the way a placement's own
                                        // identity is hashed, so the two are
                                        // the same number.
                                        const auto about = mapper.string(
                                            *when_object, "placementId",
                                            when_path, false
                                        );
                                        if (about) {
                                            output_moment.placement_id =
                                                core::stable_content_id_v1(
                                                    encounter_key + "/" + *about
                                                );
                                        }
                                    }
                                }
                                encounter.moments.push_back(output_moment);
                            }
                        }
                    }
                    // The region the player arranges their own troops in. An
                    // encounter that says nothing here has no deployment phase
                    // and every character opens on the tile its placement
                    // names, which is what every encounter written before this
                    // says and what keeps such a board byte-identical.
                    const Json* deployment_json = mapper.member(
                        *node, "deployment", node_path, false
                    );
                    if (deployment_json != nullptr) {
                        const std::string zone_path =
                            node_path + ".deployment";
                        const Object* deployment =
                            mapper.object(*deployment_json, zone_path);
                        if (deployment != nullptr) {
                            const auto zone_key =
                                mapper.string(*deployment, "id", zone_path);
                            if (zone_key) {
                                encounter.deployment.id =
                                    core::stable_content_id_v1(*zone_key);
                            }
                            // Optional, because a deployment may state only a
                            // capacity. Which of the two it must state is
                            // decided in the compiler beside every other thing
                            // that can be wrong with a deployment, rather than
                            // here where the answer would be "a missing field".
                            const Array* tiles = mapper.array(
                                *deployment, "tiles", zone_path, false
                            );
                            if (tiles != nullptr) {
                                for (std::size_t tile_index = 0;
                                     tile_index < tiles->size(); ++tile_index) {
                                    const std::string tile_path = zone_path +
                                        ".tiles[" +
                                        std::to_string(tile_index) + "]";
                                    const Object* tile = mapper.object(
                                        (*tiles)[tile_index], tile_path
                                    );
                                    if (tile == nullptr) continue;
                                    PatrolPoint output_tile;
                                    const auto tx = mapper.integer(
                                        *tile, "x", tile_path,
                                        std::numeric_limits<std::int16_t>::min(),
                                        std::numeric_limits<std::int16_t>::max()
                                    );
                                    const auto ty = mapper.integer(
                                        *tile, "y", tile_path,
                                        std::numeric_limits<std::int16_t>::min(),
                                        std::numeric_limits<std::int16_t>::max()
                                    );
                                    if (tx) {
                                        output_tile.x =
                                            static_cast<std::int16_t>(*tx);
                                    }
                                    if (ty) {
                                        output_tile.y =
                                            static_cast<std::int16_t>(*ty);
                                    }
                                    encounter.deployment.tiles.push_back(
                                        output_tile
                                    );
                                }
                            }
                            // How many of the company may take this field.
                            // Absent is no cap, which is what the placements
                            // alone have always meant.
                            const auto capacity = mapper.integer(
                                *deployment, "capacity", zone_path, 1, 4095,
                                false
                            );
                            if (capacity) {
                                encounter.deployment.capacity =
                                    static_cast<std::uint16_t>(*capacity);
                            }
                        }
                    }
                    mapper.result.source.encounters.push_back(
                        std::move(encounter)
                    );
                } else if (kind && *kind == "terminal") {
                    output_node.kind = CampaignNodeKind::terminal;
                } else if (kind && *kind == "story") {
                    output_node.kind = CampaignNodeKind::story;
                } else if (kind) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        node_path + ".kind",
                        "expected 'encounter', 'story', or 'terminal'"
                    );
                }
                // A region on a node that is not a battle is a region nobody
                // could ever be arranged in, and saying nothing about it would
                // leave an author looking at a field the compiler silently
                // dropped.
                if (output_node.kind != CampaignNodeKind::encounter &&
                    mapper.member(*node, "deployment", node_path, false) !=
                        nullptr) {
                    mapper.error(
                        SourceDiagnosticCode::invalid_value,
                        node_path + ".deployment",
                        "only an encounter node arranges troops"
                    );
                }
                output_node.dialogue_ids = mapper.references(
                    *node, "dialogueIds", node_path
                );
                // Who joins when this node completes, appended to the company
                // in flow order behind everyone already in it.
                const Array* recruits =
                    mapper.array(*node, "recruits", node_path, false);
                if (recruits != nullptr) {
                    for (std::size_t recruit_index = 0;
                         recruit_index < recruits->size(); ++recruit_index) {
                        const std::string recruit_path = node_path +
                            ".recruits[" + std::to_string(recruit_index) + "]";
                        campaign.roster.push_back(read_member(
                            mapper, (*recruits)[recruit_index], recruit_path,
                            output_node.id
                        ));
                    }
                }
                // What passing this node puts in the store, appended in flow
                // order behind everything already granted. A grant is an event
                // rather than a statement about how much the store should
                // hold, so a node a route returns to grants again.
                const Array* grants =
                    mapper.array(*node, "grants", node_path, false);
                if (grants != nullptr) {
                    for (std::size_t grant_index = 0;
                         grant_index < grants->size(); ++grant_index) {
                        const std::string grant_path = node_path + ".grants[" +
                            std::to_string(grant_index) + "]";
                        campaign.grants.push_back(read_grant(
                            mapper, (*grants)[grant_index], grant_path,
                            output_node.id
                        ));
                    }
                }
                const Array* transitions = mapper.array(
                    *node, "transitions", node_path
                );
                if (transitions != nullptr) {
                    for (std::size_t transition_index = 0;
                         transition_index < transitions->size();
                         ++transition_index) {
                        const std::string transition_path =
                            node_path + ".transitions[" +
                            std::to_string(transition_index) + "]";
                        const Object* transition = mapper.object(
                            (*transitions)[transition_index], transition_path
                        );
                        if (transition == nullptr) continue;
                        const StableId target = mapper.reference(
                            *transition, "targetNodeId", transition_path
                        );
                        const Json* when = mapper.member(
                            *transition, "when", transition_path, false
                        );
                        if (when == nullptr) {
                            output_node.unconditional_targets.push_back(target);
                            continue;
                        }
                        const Object* condition =
                            mapper.object(*when, transition_path + ".when");
                        if (condition == nullptr) continue;
                        const auto condition_kind = mapper.string(
                            *condition, "kind", transition_path + ".when"
                        );
                        if (!condition_kind) continue;

                        CampaignConditionalTarget branch;
                        branch.target_id = target;
                        const auto priority = mapper.integer(
                            *transition, "priority", transition_path, 0,
                            std::numeric_limits<std::uint16_t>::max()
                        );
                        if (priority) {
                            branch.priority =
                                static_cast<std::uint16_t>(*priority);
                        }

                        // Reads one predicate. Two of the schema's three kinds
                        // compile: an objective's result, and a world flag's
                        // value. `inventoryAtLeast` still has no bytes in a
                        // package and is refused by name rather than silently
                        // treated as false.
                        const auto read_predicate =
                            [&mapper](
                                const Object& source,
                                const std::string& source_path,
                                CampaignPredicate& predicate
                            ) -> bool {
                            const auto kind =
                                mapper.string(source, "kind", source_path);
                            if (!kind) return false;
                            if (*kind == "worldFlagEquals") {
                                predicate.kind =
                                    CampaignPredicateKind::world_flag_equals;
                                // The flag key is a source key with no record
                                // behind it, hashed exactly as every other
                                // stable identity is. What tells it apart from
                                // an objective of the same name is the category
                                // it is read back under, not the hash.
                                predicate.subject = mapper.reference(
                                    source, "flagId", source_path
                                );
                                if (predicate.subject == 0) return false;
                                const Json* const value = mapper.member(
                                    source, "value", source_path
                                );
                                if (value == nullptr) return false;
                                if (const auto* flag =
                                        std::get_if<bool>(&value->value)) {
                                    predicate.value_type = 1;
                                    predicate.value = *flag ? 1 : 0;
                                    return true;
                                }
                                if (const auto* number =
                                        std::get_if<std::int64_t>(
                                            &value->value
                                        )) {
                                    predicate.value_type = 2;
                                    predicate.value = *number;
                                    return true;
                                }
                                // A string world value is authorable in the
                                // schema and has no typed home in the campaign
                                // state, whose vocabulary is boolean and
                                // integer. Refused by name rather than hashed
                                // into a number nobody authored.
                                mapper.error(
                                    SourceDiagnosticCode::unsupported_content,
                                    source_path + ".value",
                                    "a world flag compares as a boolean or a "
                                    "whole number"
                                );
                                return false;
                            }
                            if (*kind != "objectiveResult") {
                                mapper.error(
                                    SourceDiagnosticCode::unsupported_content,
                                    source_path + ".kind",
                                    "only objectiveResult and worldFlagEquals "
                                    "predicates are executable in the vertical "
                                    "runtime"
                                );
                                return false;
                            }
                            predicate.kind =
                                CampaignPredicateKind::objective_result;
                            predicate.subject = mapper.reference(
                                source, "objectiveId", source_path
                            );
                            const auto outcome =
                                mapper.string(source, "result", source_path);
                            if (outcome && *outcome == "defeat") {
                                predicate.result = ObjectiveOutcome::failed;
                            } else if (outcome && *outcome == "victory") {
                                predicate.result = ObjectiveOutcome::satisfied;
                            } else if (outcome) {
                                mapper.error(
                                    SourceDiagnosticCode::invalid_value,
                                    source_path + ".result",
                                    "expected 'victory' or 'defeat'"
                                );
                                return false;
                            }
                            return true;
                        };

                        const std::string when_path = transition_path + ".when";
                        bool usable = true;
                        if (*condition_kind == "objectiveResult" ||
                            *condition_kind == "worldFlagEquals") {
                            branch.combinator = ConditionCombinator::all;
                            CampaignPredicate predicate;
                            usable = read_predicate(
                                *condition, when_path, predicate
                            );
                            if (usable) branch.predicates.push_back(predicate);
                        } else if (*condition_kind == "not") {
                            branch.combinator = ConditionCombinator::none;
                            const Json* inner = mapper.member(
                                *condition, "condition", when_path
                            );
                            const Object* nested = inner == nullptr
                                ? nullptr
                                : mapper.object(*inner, when_path + ".condition");
                            CampaignPredicate predicate;
                            usable = nested != nullptr &&
                                read_predicate(
                                    *nested, when_path + ".condition", predicate
                                );
                            if (usable) branch.predicates.push_back(predicate);
                        } else if (*condition_kind == "all" ||
                                   *condition_kind == "any") {
                            branch.combinator = *condition_kind == "all"
                                                    ? ConditionCombinator::all
                                                    : ConditionCombinator::any;
                            const Array* nested = mapper.array(
                                *condition, "conditions", when_path
                            );
                            if (nested == nullptr) {
                                usable = false;
                            } else {
                                for (std::size_t nested_index = 0;
                                     nested_index < nested->size();
                                     ++nested_index) {
                                    const std::string nested_path =
                                        when_path + ".conditions[" +
                                        std::to_string(nested_index) + "]";
                                    const Object* entry = mapper.object(
                                        (*nested)[nested_index], nested_path
                                    );
                                    CampaignPredicate predicate;
                                    if (entry == nullptr ||
                                        !read_predicate(
                                            *entry, nested_path, predicate
                                        )) {
                                        usable = false;
                                        break;
                                    }
                                    branch.predicates.push_back(predicate);
                                }
                                if (branch.predicates.empty()) usable = false;
                            }
                        } else if (*condition_kind == "inventoryAtLeast") {
                            // Authorable and schema-valid, and still the one
                            // predicate with no bytes in a package. Unsupported
                            // rather than malformed, and refused by name so an
                            // author is told rather than quietly answered
                            // false.
                            mapper.error(
                                SourceDiagnosticCode::unsupported_content,
                                when_path + ".kind",
                                "only objectiveResult and worldFlagEquals "
                                "predicates are executable in the vertical "
                                "runtime"
                            );
                            usable = false;
                        } else {
                            mapper.error(
                                SourceDiagnosticCode::invalid_value,
                                when_path + ".kind",
                                "expected 'objectiveResult', 'worldFlagEquals',"
                                " 'all', 'any', or 'not'"
                            );
                            usable = false;
                        }
                        if (!usable) continue;
                        output_node.conditional_targets.push_back(branch);
                    }
                }
                campaign.nodes.push_back(std::move(output_node));
            }
            mapper.result.source.campaigns.push_back(std::move(campaign));
        }
    }

    return std::move(mapper.result);
}

}  // namespace grandleon::game_content
