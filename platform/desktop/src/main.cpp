// SPDX-License-Identifier: MIT
// Desktop client entry point.
//
// Chooses a presenter and hands it to the session. Everything about rules,
// campaign flow, and the opposing side lives in the session, so adding a client
// is adding a Presenter and one line here.

#include <grandleon/core/content_identity.hpp>
#include <grandleon/desktop/presenters.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/storage/filesystem_storage.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace core = grandleon::core;
namespace desktop = grandleon::desktop;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;

int main(int argc, char** argv) {
    std::string path;
    std::string campaign_key;
    std::string slot;
    std::string saves = "saves";
    bool colour = true;
    bool graphical = false;
    // Only the SDL presenter takes a probe flag, and a build without SDL2
    // refuses `--probe` before anything reads this. Marked rather than moved
    // into the guard so the argument loop stays one list of every option the
    // client accepts, whatever this build can do with them.
    [[maybe_unused]] bool probe = false;
    bool resume = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--no-colour" || argument == "--no-color") {
            colour = false;
        } else if (argument == "--sdl") {
            graphical = true;
        } else if (argument == "--probe") {
            graphical = true;
            probe = true;
        } else if (argument == "--resume") {
            resume = true;
        } else if (argument.rfind("--campaign=", 0) == 0) {
            campaign_key = argument.substr(11);
        } else if (argument.rfind("--slot=", 0) == 0) {
            slot = argument.substr(7);
        } else if (argument.rfind("--saves=", 0) == 0) {
            saves = argument.substr(8);
        } else if (path.empty()) {
            path = argument;
        }
    }
    if (path.empty() || campaign_key.empty()) {
        std::cerr << "usage: grandleon_play <game.gpk> --campaign=<key> "
                     "[--slot=<name> [--resume] [--saves=<dir>]] "
                     "[--sdl] [--probe] [--no-colour]\n";
        return 64;
    }
    // `--resume` without `--slot` is a request with nowhere to read from, and
    // guessing a slot name would be guessing which campaign the player meant.
    if (resume && slot.empty()) {
        std::cerr << "--resume needs a --slot to resume from\n";
        return 64;
    }
    if (!slot.empty() && graphical) {
        // The SDL presenter draws a battle and says nothing about a campaign.
        // Refusing beats playing a persistent campaign that narrates none of
        // itself and silently overwrites a slot.
        std::cerr << "campaign mode is terminal-only for now; omit --sdl\n";
        return 64;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "cannot open " << path << '\n';
        return 66;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const auto loaded = pf::load_mock_package(
        bytes, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        std::cerr << "package rejected: " << pf::error_name(loaded.error)
                  << '\n';
        return 65;
    }

    // What the author chose about how the game looks. A package that carries
    // no presentation section resolves to the default theme and to no faction
    // colours, which is what a client drew before packages carried any of it;
    // a package that carries a damaged one is refused rather than half-read.
    const auto presentation = pr::load_presentation(loaded.package);
    if (!presentation) {
        std::cerr << "presentation rejected: "
                  << pr::error_name(presentation.error) << '\n';
        return 65;
    }

    // A named slot means a campaign the player keeps: the roster is joined to
    // every board, the permanently dead stay off later ones, and the whole
    // state is written to the slot between battles. No slot means the
    // one-sitting session this client has always had.
    if (!slot.empty()) {
        const std::unique_ptr<grandleon::client::CampaignFrontEnd> front_end =
            desktop::make_terminal_front_end(colour, &loaded.package);
        storage::FilesystemSlotStorage device(saves);
        if (!device.available()) {
            std::cerr << "cannot use save directory " << saves << '\n';
            return 73;
        }
        grandleon::client::CampaignSessionOptions options;
        options.slot = slot;
        options.resume = resume;
        options.player_side = sim::Side::first;
        const auto status = grandleon::client::run_persistent_campaign(
            loaded.package,
            core::stable_content_id_v1(campaign_key),
            *front_end,
            *front_end,
            device,
            options
        );
        if (status != grandleon::client::CampaignSessionError::none) {
            std::cerr << "campaign failed: "
                      << grandleon::client::campaign_session_error_name(status)
                      << '\n';
            return 65;
        }
        return 0;
    }

    std::unique_ptr<grandleon::client::Presenter> presenter;
    if (graphical) {
#ifdef GRANDLEON_DESKTOP_SDL
        presenter = desktop::make_sdl_presenter(
            probe, presentation.presentation
        );
#else
        std::cerr << "this build has no SDL presenter; omit --sdl\n";
        return 64;
#endif
    }
    if (!presenter) {
        presenter = desktop::make_terminal_presenter(colour, &loaded.package);
    }

    const auto status = grandleon::client::run_campaign(
        loaded.package,
        core::stable_content_id_v1(campaign_key),
        sim::Side::first,
        *presenter
    );
    if (status != grandleon::client::SessionError::none) {
        std::cerr << "session failed: " << grandleon::client::error_name(status) << '\n';
        return 65;
    }
    return 0;
}
