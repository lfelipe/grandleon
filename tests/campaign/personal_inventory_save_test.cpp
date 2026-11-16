// SPDX-License-Identifier: MIT
#include <grandleon/campaign/save.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>

namespace camp = grandleon::campaign;
namespace {
int failures{};
void check(bool value, const char* message) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main(int argc, char** argv) {
    auto plain = camp::make_campaign_save({}, {});
    const auto legacy_bytes = camp::save_campaign(plain);
    auto personal = plain;
    personal.carried_inventory = true;
    const auto bytes = camp::save_campaign(personal);
    const auto loaded = camp::load_campaign(bytes, {});
    check(bool(loaded) && loaded.save == personal,
          "empty bags and zero gold retain personal ownership mode");
    check(bytes != legacy_bytes, "personal mode is independent of equipment contents");
    check(camp::save_campaign(plain) == legacy_bytes, "plain saves keep their encoding");
    const auto plain_loaded = camp::load_campaign(legacy_bytes, {});
    check(bool(plain_loaded) && !plain_loaded.save.carried_inventory,
          "unmarked legacy saves remain legacy");
    auto decoded = camp::decode_save_envelope(bytes, {});
    check(bool(decoded), "personal save envelope decodes");
    if (!decoded) return 1;
    const auto marker = static_cast<std::uint32_t>(camp::SaveSectionType::carried_inventory);
    auto found = std::find_if(decoded.decoded.sections.begin(), decoded.decoded.sections.end(),
        [marker](const auto& section) { return section.type == marker; });
    check(found != decoded.decoded.sections.end() && found->required(),
          "old readers must refuse personal inventory saves");
    if (found != decoded.decoded.sections.end()) {
        found->flags = 0;
        check(camp::interpret_save(decoded.decoded, {}).error == camp::SaveError::invalid_section,
              "personal marker cannot be optional");
        found->flags = camp::save_section_flag_required;
        decoded.decoded.bytes[found->offset] = 0;
        check(camp::interpret_save(decoded.decoded, {}).error == camp::SaveError::invalid_section,
              "unrecognized inventory mode is refused");
    }
    if (argc == 2) {
        std::ofstream output(argv[1], std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return failures ? 1 : 0;
}
