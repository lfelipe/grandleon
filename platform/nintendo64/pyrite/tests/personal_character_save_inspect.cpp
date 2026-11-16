// SPDX-License-Identifier: MIT
// Read an emulator's real SRAM through the native save and campaign loaders.
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/storage/byte_window_storage.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace cli = grandleon::client;
namespace core = grandleon::core;
namespace camp = grandleon::campaign;
namespace pf = grandleon::package_format;
namespace sto = grandleon::storage;

std::vector<std::uint8_t> read_bytes(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: personal-save-inspect game.gpk cartridge.ram required_loss|recovery 1|2\n";
        return 2;
    }
    const std::string phase = argv[3];
    const std::string players = argv[4];
    if ((phase != "required_loss" && phase != "recovery") ||
        (players != "1" && players != "2")) return 2;
    const auto package = pf::load_mock_package(read_bytes(argv[1]),
        {{0, 1, 0}, pf::TargetProfile::nintendo64, 0, 64, 10000});
    auto bytes = read_bytes(argv[2]);
    if (!package || bytes.size() != 32768U) {
        std::cerr << "Refused invalid package or SRAM size\n";
        return 1;
    }
    sto::VectorByteWindow window(bytes.size());
    window.bytes() = std::move(bytes);  // In-memory copy; the input file stays read-only.
    sto::ByteWindowSlotStorage saves(window, sto::ByteWindowSlotStorage::budget_for(32768U, 4U));
    cli::PackageBoards boards{package.package, static_cast<std::uint8_t>(players == "2" ? 2U : 1U)};
    cli::CampaignSessionOptions options;
    options.slot = "geb4bda4c_" + players + "p";
    options.resume = true;
    options.local_players = players == "2" ? 2U : 1U;
    cli::CampaignSession session{package.package, core::stable_content_id_v1("personal_rules"),
                                 boards, saves, options};
    cli::SlotFailure failure;
    bool refused = false, resumed = false;
    if (session.begin(failure, refused, resumed) != cli::CampaignSessionError::none ||
        refused || !resumed) {
        std::cerr << "SRAM did not resume an accepted campaign\n";
        return 1;
    }
    const auto standing = session.standing();
    const auto roster = session.roster();
    auto board = session.prepare_board();
    bool valid = standing.node.stable_id == core::stable_content_id_v1(phase) &&
        roster.size() == 4U && board.error == cli::CampaignSessionError::none &&
        board.board.excluded == std::vector<camp::PersistentEntityId>{{2U}} &&
        board.encounter.encounter.definition.units.size() == 4U &&
        board.encounter.encounter.map_id == core::stable_content_id_v1("proof_field") &&
        board.encounter.encounter.surroundings.depth == 1U;
    for (const auto& member : roster) {
        const auto expected = member.member.value == 2U
            ? camp::Availability::dead : camp::Availability::available;
        valid = valid && member.availability == expected;
        std::cout << "N64_PERSONAL_MEMBER id=" << member.member.value
                  << " availability=" << camp::availability_name(member.availability) << '\n';
    }
    const auto scholar = board.encounter.binding.battle_of({1U});
    const auto willow = board.encounter.binding.battle_of({3U});
    const auto rhea = board.encounter.binding.battle_of({4U});
    const auto made = grandleon::simulation::create_encounter(board.encounter.encounter.definition);
    if (!made) return 1;
    for (const auto& unit : made.encounter.snapshot().units) {
        if (unit.id == scholar.value)
            valid = valid && unit.weapon_ids == std::vector<std::uint64_t>{
                core::stable_content_id_v1("traveller_blade")};
        if (unit.id == rhea.value)
            valid = valid && unit.defeat_ends_battle && unit.health == unit.maximum_health &&
                unit.local_player == options.local_players;
        if (unit.id == willow.value)
            valid = valid && unit.health == unit.maximum_health &&
                unit.local_player == options.local_players;
    }
    valid = valid && scholar.value != 0U && willow.value != 0U && rhea.value != 0U;
    std::cout << "N64_PERSONAL_SAVE_" << (valid ? "PASS" : "FAIL")
              << " phase=" << phase << " players=" << players << '\n';
    return valid ? 0 : 1;
}
