// SPDX-License-Identifier: MIT
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Leaving a battle for another Stage, on the larger sample campaign.
//
// The claim is one sentence: *a player checking a game can stand on any of its
// Stages without playing the ones before it, and the campaign that results is
// one the campaign layer accepts and the slot holds.*
//
// Tarnholt is the content because it is the case the aid exists for: six boards
// with cutscenes and a branch between them, where reaching the last one the
// ordinary way is five battles of work every time somebody wants to look at it.
//
// The other half of the claim is the limit, and it is asserted just as hard.
// A jump moves the campaign and changes nothing else, so the Stages it passed
// over are still unreached, the company is exactly the company that was
// standing before, and a screen can tell a safe jump from a risky one because
// the session publishes which Stages this playthrough has actually stood on.
//
// The picker is a build define, and one binary can only be one side of it. This
// executable is compiled without GRANDLEON_STAGE_PICKER, so it is the only thing
// that can say what an ordinary build offers, which is nothing;
// stage_picker_test.cpp is the other side.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace cr = grandleon::campaign_runtime;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const std::uint64_t tarnholt = core::stable_content_id_v1("tarnholt_line");
const std::uint64_t coldgate = core::stable_content_id_v1("coldgate_battle");

// The sample campaign, compiled as written. Nothing is switched on: what this
// test is about is a build that was never asked for the picker, which is every
// build but the one somebody makes to debug with.
pf::LoadedPackage compile_tarnholt() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and maps natively");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and mounts");
    return loaded.package;
}

// A build that did not ask for the picker has no Stages, and there is nothing a
// client can do about it.
//
// This is the gate, and it is the whole of what this executable is for: it is
// the one built *without* `GRANDLEON_STAGE_PICKER`, which is the only way to
// assert what a build lacking it does. A test compiled with the define could
// only ever assert the other half.
void a_build_without_the_picker_has_no_stages() {
    const pf::LoadedPackage package = compile_tarnholt();
    const client::PackageBoards boards{package};
    storage::MemorySlotStorage device;
    // The default, and where it comes from. This executable is compiled without
    // `GRANDLEON_STAGE_PICKER`, so a session nobody configured must offer
    // nothing: that is the property a console image relies on, and it is the one
    // thing a test carrying the define could not check.
    expect(
        client::CampaignSessionOptions{}.stage_picker == false,
        "a build without the define defaults to no picker"
    );
    client::CampaignSession session{package, tarnholt, boards, device, {}};

    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign as its author wrote it begins"
    );
    expect(
        session.stages().empty(),
        "and offers no Stages at all, which is what leaves every client with no "
        "row to draw and no setting to read"
    );
    // And not merely undrawn. A front end that produced the number some other
    // way still cannot move the campaign with it.
    const client::StageJump refused_jump = session.jump_to_stage(coldgate);
    expect(
        !refused_jump,
        "a jump named on a campaign that offers none is refused rather than "
        "taken"
    );
    expect(!refused_jump.saved, "and nothing is written");
    expect(
        session.standing().node ==
            campaign::DefinitionRef{
                package.game_id, core::ContentCategory::campaign_node,
                core::stable_content_id_v1("prologue")
            },
        "and the campaign stands exactly where it stood"
    );
}


}  // namespace

int main() {
    a_build_without_the_picker_has_no_stages();
    if (failures == 0) {
        std::cout << "a build without the Stage picker offers none\n";
    }
    return failures == 0 ? 0 : 1;
}
