# SPDX-License-Identifier: MIT
# Test code is held to the same warning standard as the code it tests.
#
# `GRANDLEON_WARNING_FLAGS` is applied per target in the top-level list, and
# every engine and tool target names it. Naming it once per test executable
# would be the same line fifty times, and the fifty-first is the one somebody
# forgets, leaving a lane of test code compiling with no warnings at all under
# a step called "native gate, GCC -Werror".
#
# So it is applied at the bottom of this file instead, by walking the
# executables this directory defines. A test added tomorrow is covered by
# having been added, and there is nothing to remember.
add_executable(grandleon_core_tests
    core/version_test.cpp
)

target_link_libraries(grandleon_core_tests PRIVATE grandleon::core)

add_test(NAME grandleon.core COMMAND grandleon_core_tests)

# The bounds question every parser of untrusted bytes asks, and the wrapping
# subtraction it exists to replace. Both are exercised side by side: the two
# decide alike over every region a well-formed file can describe, and disagree
# on every region that starts past the end.
add_executable(grandleon_bounds_tests
    core/bounds_test.cpp
)
target_link_libraries(grandleon_bounds_tests PRIVATE grandleon::core)
add_test(NAME grandleon.bounds COMMAND grandleon_bounds_tests)

add_executable(grandleon_content_identity_tests
    core/content_identity_test.cpp
)
target_link_libraries(grandleon_content_identity_tests PRIVATE grandleon::core)
add_test(
    NAME grandleon.content_identity
    COMMAND grandleon_content_identity_tests
)

# The deterministic random substrate: the vectors every platform must
# reproduce, the properties the rules will lean on, and the measured mixing
# quality behind the header's claim that a shown chance is the real one.
add_executable(grandleon_random_tests
    core/random_test.cpp
)
target_link_libraries(grandleon_random_tests PRIVATE grandleon::core)
add_test(NAME grandleon.random COMMAND grandleon_random_tests)

# The three identity levels a roster needs: what kind of thing this is, which
# one of them it is, and who it is on the board right now. Collisions, renames,
# missing content, and the partial map back from a battle.
add_executable(grandleon_campaign_identity_tests
    campaign/identity_test.cpp
)
target_link_libraries(
    grandleon_campaign_identity_tests
    PRIVATE grandleon::campaign
)
add_test(
    NAME grandleon.campaign_identity
    COMMAND grandleon_campaign_identity_tests
)

# The campaign that outlives a battle: the roster, its inventory, objectives
# and world flags, and the atomic idempotent outcome batches that change them.
add_executable(grandleon_campaign_state_tests
    campaign/campaign_state_test.cpp
)
target_link_libraries(
    grandleon_campaign_state_tests
    PRIVATE grandleon::campaign
)
add_test(
    NAME grandleon.campaign_state
    COMMAND grandleon_campaign_state_tests
)

# Where a campaign goes next: the authored graph, the priority order that
# decides an edge, the fallback that is a last resort, recombination, a cycle
# walked once per completion, and the blocked node that keeps its outcome and
# stays put.
add_executable(grandleon_campaign_graph_tests
    campaign/campaign_graph_test.cpp
)
target_link_libraries(
    grandleon_campaign_graph_tests
    PRIVATE grandleon::campaign
)
add_test(
    NAME grandleon.campaign_graph
    COMMAND grandleon_campaign_graph_tests
)

# The bytes a campaign becomes when nobody is playing it: the envelope's
# header, package table and per-section directory, both levels of integrity,
# the bounded decode a hostile save is measured against, and the round trips in
# both directions. The measured size of a representative campaign is here too,
# so the console storage adapters have a figure to budget a card against.
add_executable(grandleon_campaign_save_tests
    campaign/save_test.cpp
)
target_link_libraries(
    grandleon_campaign_save_tests
    PRIVATE grandleon::campaign
)
add_test(
    NAME grandleon.campaign_save
    COMMAND grandleon_campaign_save_tests
)

# What happens when the save on the card is older than the build reading it:
# the chain walked one version at a time, the hole that is named rather than
# leapt, the downgrade that is refused rather than attempted, and two committed
# golden saves this build's writer cannot produce: one at an older section
# schema, one naming definitions a later content revision renamed away.
add_executable(grandleon_campaign_migration_tests
    campaign/migration_test.cpp
)
target_link_libraries(
    grandleon_campaign_migration_tests
    PRIVATE grandleon::campaign
)
target_compile_definitions(
    grandleon_campaign_migration_tests
    PRIVATE
        GRANDLEON_CAMPAIGN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/campaign/fixtures"
)
add_test(
    NAME grandleon.campaign_migration
    COMMAND grandleon_campaign_migration_tests
)

# Where compiled content and the persistent campaign meet: an authored flow
# translated into the graph the roster evaluates, and an encounter loaded
# through a roster that decides who may appear. The property that keeps every
# golden where it is lives here too: a full roster loads exactly the board the
# package holds, hash for hash.
add_executable(grandleon_campaign_runtime_tests
    campaign_runtime/campaign_runtime_test.cpp
)
target_link_libraries(
    grandleon_campaign_runtime_tests
    PRIVATE grandleon::campaign_runtime grandleon::game_content
)
add_test(
    NAME grandleon.campaign_runtime
    COMMAND grandleon_campaign_runtime_tests
)

# The vertical slice, in the maintained demo and with nothing standing in for
# anything: the real source project, the real compiler, the real package, a
# real battle, real envelope bytes in a real storage device, and the next map
# refusing to field the rider who fell. The demo's own conformance hash is
# checked on the way past, because a second campaign in that project must leave
# the first one's board exactly where it was.
add_executable(grandleon_demo_permadeath_tests
    campaign_runtime/demo_permadeath_test.cpp
)
target_link_libraries(
    grandleon_demo_permadeath_tests
    PRIVATE
        grandleon::campaign_runtime
        grandleon::game_content
        grandleon::storage
)
target_compile_definitions(
    grandleon_demo_permadeath_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.demo_permadeath
    COMMAND grandleon_demo_permadeath_tests
)

# The company, managed between battles, in the same maintained demo and at the
# level a client actually drives: `client::CampaignSession`, a real package, a
# real device. It is the end-to-end proof for the three properties no single
# layer can state alone: a tonic that fell can be drunk in the next battle, a
# member the player leaves behind is not on the board, and a campaign saved in
# the middle of deciding resumes in the middle of deciding. It asserts every
# refusal under the name `engine/campaign` has for it.
add_executable(grandleon_campaign_management_tests
    client/campaign_management_test.cpp
)
target_link_libraries(
    grandleon_campaign_management_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::game_content
        grandleon::storage
)
target_compile_definitions(
    grandleon_campaign_management_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.campaign_management
    COMMAND grandleon_campaign_management_tests
)

# What a fall costs the company, under both rules, over one identical battle.
# The load-bearing claim of `project.characterLoss` is that it changes what the
# campaign records and nothing else, so the same source is compiled twice and
# the same script is fought on both: the two campaigns differ afterwards and the
# two battles hash the same.
add_executable(grandleon_campaign_losses_tests
    client/campaign_losses_test.cpp
)
target_link_libraries(
    grandleon_campaign_losses_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::game_content
        grandleon::storage
)
target_compile_definitions(
    grandleon_campaign_losses_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.campaign_losses
    COMMAND grandleon_campaign_losses_tests
)

# What a campaign is given by its author, at the level a client drives it: the
# store a campaign is founded with, what passing a node puts there, a road that
# loops past the abbey and is blessed twice, the same batch offered twice and
# recognised as one, and a save that carries the lot with no migration to run.
add_executable(grandleon_campaign_store_tests
    client/campaign_store_test.cpp
)
target_link_libraries(
    grandleon_campaign_store_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::game_content
        grandleon::storage
)
add_test(
    NAME grandleon.campaign_store
    COMMAND grandleon_campaign_store_tests
)

# A whole scenario end to end, out of an authored file rather than a
# hand-built definition: a ford held for seven rounds while a wave arrives every
# three and comes at the player, through the real source reader, the real
# compiler, the real package container, the real loader and the same policy
# every client asks for the side nobody is steering.
add_executable(grandleon_surviving_waves_tests
    client/surviving_waves_test.cpp
)
target_link_libraries(
    grandleon_surviving_waves_tests
    PRIVATE
        grandleon::game_content
        grandleon::package_runtime
        grandleon::sheet
        grandleon::tactics
)
add_test(
    NAME grandleon.surviving_waves
    COMMAND grandleon_surviving_waves_tests
)

# The Tarnholt campaign's branch, driven through the client every platform
# drives: a conversation on the Harrow Burn raises a world flag, a
# `worldFlagEquals` edge opens a board no other route reaches and brings a
# member back with it, the thread that fought instead takes the fallback with
# the company it had, and both arrive at the same node. The shipped project is
# the fixture. Nothing here is hand-built.
add_executable(grandleon_tarnholt_branch_tests
    client/tarnholt_branch_test.cpp
)
target_link_libraries(
    grandleon_tarnholt_branch_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::game_content
        grandleon::sheet
        grandleon::storage
)
target_compile_definitions(
    grandleon_tarnholt_branch_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.tarnholt_branch
    COMMAND grandleon_tarnholt_branch_tests
)

# What the Nintendo 64 campaign ROM is allowed to believe about the Tarnholt
# company, derived on the host from the real session and the real save layer
# before the ROM is built. The ROM includes the same expectations header this
# test pins, so a console assertion is evidence rather than a recording: the
# only way to change what the console checks is to change what the host derives.
add_executable(grandleon_n64_campaign_expectation_tests
    nintendo64/campaign_expectations_test.cpp
)
target_link_libraries(
    grandleon_n64_campaign_expectation_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::game_content
        grandleon::storage
)
target_include_directories(
    grandleon_n64_campaign_expectation_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/games/tarnholt/autopilot"
)
target_compile_definitions(
    grandleon_n64_campaign_expectation_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.n64_campaign_expectations
    COMMAND grandleon_n64_campaign_expectation_tests
)

# Where the Nintendo 64's rows of text are allowed to land.
#
# libdragon does not clip: `graphics_draw_character` computes
# `buffer + y * stride + x` and stores, comparing nothing against the surface.
# A screen that walks a list of authored strings with no cap therefore writes
# into the memory after the framebuffer, and the console cannot report it. So
# the bands live in a header with no libdragon in it and are held to the frame
# here, on a host, before a ROM is built.
add_executable(grandleon_n64_screen_text_tests
    nintendo64/screen_text_test.cpp
)
target_include_directories(
    grandleon_n64_screen_text_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/platform/nintendo64/src"
)
target_compile_features(grandleon_n64_screen_text_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.n64_screen_text
    COMMAND grandleon_n64_screen_text_tests
)

# The words the two consoles refuse a command in, compared row by row.
#
# The shared client's header says its table "is the Nintendo 64's table because
# a player who has seen one console's refusal should recognise the other's".
# This is what makes that a checked claim rather than a hope: it compiles the
# console's table and links the shared one, walks every refusal the engine
# names, and fails on any disagreement that is not in its own written ledger,
# and on any ledger row that has quietly been reconciled.
add_executable(grandleon_n64_console_words_tests
    nintendo64/console_words_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_n64_console_words_tests
    PRIVATE
        grandleon::client
        grandleon::client_campaign
        grandleon::package_format
        grandleon::sheet
        grandleon::simulation
        grandleon::view
)
target_include_directories(
    grandleon_n64_console_words_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/platform/nintendo64/src"
)
target_compile_features(grandleon_n64_console_words_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.n64_console_words
    COMMAND grandleon_n64_console_words_tests
)

# A company larger than the screen, walked to its last row.
#
# It compiles `platform/client/src/turn_client.cpp` under the campaign define,
# which is the same translation unit a campaign console compiles, so what this
# proves about reachability it proves about the console's own composer rather
# than about a model of it. No console, no emulator and no baked package: the
# fixture project is compiled at test time, and the whole run is a host one.
add_executable(grandleon_client_company_scroll_tests
    client/company_scroll_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_client_company_scroll_tests
    PRIVATE
        grandleon::client
        grandleon::client_campaign
        grandleon::game_content
        grandleon::package_format
        grandleon::sheet
        grandleon::storage
        grandleon::view
)
target_compile_definitions(
    grandleon_client_company_scroll_tests
    PRIVATE
        GRANDLEON_TURN_CLIENT_CAMPAIGN
        GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_compile_features(grandleon_client_company_scroll_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_company_scroll
    COMMAND grandleon_client_company_scroll_tests
)

# Who the client says is standing on the board, against who the engine says is.
#
# The same translation unit both consoles compose their board from, for the same
# reason: the transcript this test reads is the transcript the console checks are
# compared on, so a disagreement caught here is caught in the shipped composer.
# Built without the campaign define. This asks about a board and never opens a
# screen. The fixture project is compiled at test time; no console, no emulator
# and no baked package.
add_executable(grandleon_client_board_predicate_tests
    client/board_predicate_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_client_board_predicate_tests
    PRIVATE
        grandleon::client
        grandleon::game_content
        grandleon::package_format
        grandleon::package_runtime
        grandleon::sheet
        grandleon::view
)
target_compile_definitions(
    grandleon_client_board_predicate_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_compile_features(grandleon_client_board_predicate_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_board_predicate
    COMMAND grandleon_client_board_predicate_tests
)

# What the player sees while the side nobody is steering plays its turn.
#
# The session's own loop, driven by a presenter that paints nothing and records
# the order it was called in. No console, no emulator and no package: the board
# is authored in the engine's own model, which is all a question about call
# order needs.
add_executable(grandleon_client_opposing_side_tests
    client/opposing_side_frames_test.cpp
)
target_link_libraries(
    grandleon_client_opposing_side_tests
    PRIVATE
        grandleon::client
        grandleon::package_runtime
        grandleon::simulation
        grandleon::tactics
)
target_compile_features(grandleon_client_opposing_side_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_opposing_side
    COMMAND grandleon_client_opposing_side_tests
)

# Where an aiming cursor may rest, and where a press moves it.
#
# The pure rule first, and then the same rule inside the translation unit both
# consoles compose their board from, so a disagreement is caught in the shipped
# composer rather than in a model of it.
add_executable(grandleon_client_aim_cursor_tests
    client/aim_cursor_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_client_aim_cursor_tests
    PRIVATE
        grandleon::client
        grandleon::package_format
        grandleon::package_runtime
        grandleon::sheet
        grandleon::view
)
target_compile_features(grandleon_client_aim_cursor_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_aim_cursor
    COMMAND grandleon_client_aim_cursor_tests
)

# When a character's turn ends itself, and the boards where it must not.
add_executable(grandleon_client_spent_turn_tests
    client/spent_turn_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_client_spent_turn_tests
    PRIVATE
        grandleon::client
        grandleon::package_format
        grandleon::package_runtime
        grandleon::sheet
        grandleon::simulation
        grandleon::view
)
target_compile_features(grandleon_client_spent_turn_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_spent_turn
    COMMAND grandleon_client_spent_turn_tests
)

# An authored speech taller than a page, and a word wider than one.
#
# The same translation unit under the same define, for the same reason: what a
# reader is shown is composed here and nowhere else. It authors its scene
# inline: `present_dialogue` takes a dialogue, so no package, no campaign and
# no storage are needed to ask the pager a question.
add_executable(grandleon_client_story_paging_tests
    client/story_paging_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/client/src/turn_client.cpp"
)
target_link_libraries(
    grandleon_client_story_paging_tests
    PRIVATE
        grandleon::client
        grandleon::client_campaign
        grandleon::package_format
        grandleon::sheet
        grandleon::storage
        grandleon::view
)
target_compile_definitions(
    grandleon_client_story_paging_tests PRIVATE GRANDLEON_TURN_CLIENT_CAMPAIGN
)
target_compile_features(grandleon_client_story_paging_tests PRIVATE cxx_std_17)
add_test(
    NAME grandleon.client_story_paging
    COMMAND grandleon_client_story_paging_tests
)

# Named slots of opaque bytes. One contract, run against the in-memory device,
# the desktop directory and the cartridge directory, because a promise only one
# adapter keeps is not a contract. The third is the one a console supplies bytes
# to, so the Nintendo 64's save path is proved here at a cartridge's own budget
# and an emulator is left only what a host cannot check.
add_executable(grandleon_storage_tests
    storage/storage_contract_test.cpp
)
target_link_libraries(
    grandleon_storage_tests
    PRIVATE
        grandleon::storage
        grandleon::storage_filesystem
        grandleon::campaign
)
target_compile_definitions(
    grandleon_storage_tests
    PRIVATE GRANDLEON_STORAGE_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/storage_slots"
)
add_test(NAME grandleon.storage COMMAND grandleon_storage_tests)

# The same vertical slice as `grandleon.demo_permadeath`, with a person at the
# other end of it: lines typed at a prompt, the real terminal front end reading
# them, and the campaign narrated back. What it proves is the one thing only a
# client can be wrong about: that what the terminal says a battle did is what
# the engine says it did. So every number it asserts is read out of the
# engine's own `BattleProgression` and out of the campaign that came back from
# the slot, and never out of a literal that would outlive the growth stream
# moving under it.
add_executable(grandleon_terminal_campaign_tests
    desktop/terminal_campaign_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/desktop/src/terminal_presenter.cpp"
)
target_link_libraries(
    grandleon_terminal_campaign_tests
    PRIVATE
        grandleon::client_campaign
        grandleon::storage_filesystem
        grandleon::sheet
        grandleon::game_content
)
target_include_directories(
    grandleon_terminal_campaign_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/platform/desktop/include"
)
target_compile_definitions(
    grandleon_terminal_campaign_tests
    PRIVATE
        GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        GRANDLEON_TERMINAL_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/campaign_slots"
)
add_test(
    NAME grandleon.terminal_campaign
    COMMAND grandleon_terminal_campaign_tests
)

# The board's presentation model: the camera, the elevation-offset
# projection, the autotile lookup and the depth-ordered draw list every
# renderer shares. Pure arithmetic, so the whole of it is checked here rather
# than three times over inside three renderers that need a console, a window
# or a browser to say anything.
add_executable(grandleon_view_camera_tests
    view/camera_test.cpp
)
target_link_libraries(grandleon_view_camera_tests PRIVATE grandleon::view)
add_test(NAME grandleon.view_camera COMMAND grandleon_view_camera_tests)

# The same arithmetic in one dimension, over rows. It is tested here rather
# than on either console because "the last row is reachable" is a claim about
# a rule, and the rule is one file both consoles include.
add_executable(grandleon_view_list_tests
    view/list_view_test.cpp
)
target_link_libraries(grandleon_view_list_tests PRIVATE grandleon::view)
add_test(NAME grandleon.view_list COMMAND grandleon_view_list_tests)

# The slot screen, as a model. It links `grandleon::storage` for exactly one
# assertion: that every name this screen invents is a name every platform could
# carry. The authority on that is `is_valid_slot_name`, not a rule restated in
# a test.
add_executable(grandleon_view_slot_menu_tests
    view/slot_menu_test.cpp
)
target_link_libraries(
    grandleon_view_slot_menu_tests PRIVATE grandleon::view grandleon::storage
)
add_test(NAME grandleon.view_slot_menu COMMAND grandleon_view_slot_menu_tests)

add_executable(grandleon_view_tests
    view/board_view_test.cpp
)
target_link_libraries(grandleon_view_tests PRIVATE grandleon::view)
target_include_directories(
    grandleon_view_tests
    PRIVATE
        # The art library's own autotile table and terrain registry, so the
        # lookup is pinned against the generator's data rather than a copy of
        # it made here.
        "${PROJECT_SOURCE_DIR}/tools/placeholder_art/assets"
)
add_test(NAME grandleon.view COMMAND grandleon_view_tests)

# The same model in time: frames per tile, the route a slide is drawn along,
# the cursor's pulse phase and the flinch. Pinned here for the same reason the
# projection is: four renderers animate, and a slide that ends a pixel short
# is not something a screenshot review would catch.
add_executable(grandleon_view_motion_tests
    view/motion_test.cpp
)
target_link_libraries(grandleon_view_motion_tests PRIVATE grandleon::view)
add_test(NAME grandleon.view_motion COMMAND grandleon_view_motion_tests)

# The console font. Two machines have no font of their own and read one table,
# and the *shape* of a letter is the part they share, so the shape is pinned
# here, where neither console is in the loop. The box matters most: both
# expansions walk a fixed rectangle per glyph, and the PlayStation's pixel
# claims name a particular ink pixel of a particular letter.
add_executable(grandleon_view_glyph_tests
    view/glyph_test.cpp
)
target_link_libraries(grandleon_view_glyph_tests PRIVATE grandleon::view)
add_test(NAME grandleon.view_glyphs COMMAND grandleon_view_glyph_tests)

# The full information sheet every client draws and none of them composes.
# Pinned line for line here, against a snapshot built by hand, because a sheet
# that said the wrong thing would be three clients saying it at once.
add_executable(grandleon_sheet_tests
    sheet/unit_sheet_test.cpp
)
target_link_libraries(
    grandleon_sheet_tests PRIVATE grandleon::sheet grandleon::core
)
add_test(NAME grandleon.sheet COMMAND grandleon_sheet_tests)

# Who a character is, from the authored file through the compiler and the
# loader to the name a client draws. Its own executable rather than another
# block in the sheet test above, because it needs the whole toolchain. Half of
# what it proves is that a name an author wrote survives being encoded, and the
# sheet test deliberately links nothing but the sheet.
add_executable(grandleon_character_name_tests
    sheet/character_name_test.cpp
)
target_link_libraries(
    grandleon_character_name_tests
    PRIVATE
        grandleon::game_content
        grandleon::package_runtime
        grandleon::sheet
)
add_test(
    NAME grandleon.character_name
    COMMAND
        grandleon_character_name_tests
        "${PROJECT_SOURCE_DIR}/tests/fixtures/source_projects/valid/character-names.json"
)

add_executable(grandleon_simulation_tests
    simulation/encounter_test.cpp
)
target_link_libraries(
    grandleon_simulation_tests
    PRIVATE grandleon::simulation
)
add_test(NAME grandleon.simulation COMMAND grandleon_simulation_tests)

# Every field of a battle held to a verdict: folded into the canonical hash, or
# deliberately not and argued. Its own executable because it is the one test
# that is meant to stop the *build*: a field added to a battle and left out of
# both verdicts fails to compile here.
add_executable(grandleon_canonical_hash_tests
    simulation/canonical_hash_test.cpp
)
target_link_libraries(
    grandleon_canonical_hash_tests
    PRIVATE grandleon::simulation grandleon::core
)
add_test(
    NAME grandleon.canonical_hash COMMAND grandleon_canonical_hash_tests
)

add_executable(grandleon_tactics_tests
    tactics/policy_test.cpp
)
target_link_libraries(
    grandleon_tactics_tests
    PRIVATE grandleon::tactics
)
add_test(NAME grandleon.tactics COMMAND grandleon_tactics_tests)

# The WebAssembly binding surface, compiled for the host and driven through
# the same exported functions the browser calls.
add_executable(grandleon_abi_tests
    abi/simulation_abi_test.cpp
    "${PROJECT_SOURCE_DIR}/platform/web/src/simulation_abi.cpp"
)
target_link_libraries(
    grandleon_abi_tests
    PRIVATE
        grandleon::simulation
        grandleon::tactics
        grandleon::core
        grandleon::package_runtime
        grandleon::client_campaign
        grandleon::storage
        grandleon::game_content
)
add_test(NAME grandleon.abi COMMAND grandleon_abi_tests)

add_executable(grandleon_package_format_tests
    package_format/package_test.cpp
)

target_link_libraries(
    grandleon_package_format_tests
    PRIVATE grandleon::package_format
)

add_test(NAME grandleon.package_format COMMAND grandleon_package_format_tests)

add_executable(grandleon_package_runtime_tests
    package_runtime/encounter_loader_test.cpp
)
target_link_libraries(
    grandleon_package_runtime_tests
    PRIVATE grandleon::package_runtime grandleon::game_content
)
target_compile_definitions(
    grandleon_package_runtime_tests
    PRIVATE GRANDLEON_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME grandleon.package_runtime
    COMMAND grandleon_package_runtime_tests
)

# A dialogue record's optional tail: two fields whose presence is the record's
# own length, so the three lengths are proved to mean three different scenes.
add_executable(grandleon_dialogue_tests
    package_runtime/dialogue_test.cpp
)
target_link_libraries(
    grandleon_dialogue_tests
    PRIVATE grandleon::package_runtime grandleon::game_content
)
add_test(
    NAME grandleon.dialogue
    COMMAND grandleon_dialogue_tests
)

# The package's manifest section: the name a project gave itself, from the
# compiler that writes it to the console screen that draws it.
add_executable(grandleon_manifest_tests
    package_runtime/manifest_test.cpp
)
target_link_libraries(
    grandleon_manifest_tests
    PRIVATE grandleon::package_runtime grandleon::game_content
)
add_test(
    NAME grandleon.manifest
    COMMAND grandleon_manifest_tests
)

# The package's presentation section: written by the compiler, read back by the
# runtime, and refused rather than misread when damaged.
add_executable(grandleon_presentation_tests
    package_runtime/presentation_test.cpp
)
target_link_libraries(
    grandleon_presentation_tests
    PRIVATE grandleon::package_runtime grandleon::game_content
)
add_test(
    NAME grandleon.presentation
    COMMAND grandleon_presentation_tests
)

# What every reader does with a package somebody meant it harm with. The
# executable replaces the global allocator, so it counts what a decode wants
# and poisons what it frees: one holds the rule that a count read out of a file
# is measured against the bytes present before anything is reserved, the other
# holds the rule that nothing borrowed from a package's bytes is a captured
# pointer. A target of its own because a replaced allocator belongs to a whole
# program and must not be imposed on tests that are measuring something else.
add_executable(grandleon_hostile_package_tests
    package_runtime/hostile_package_test.cpp
)
target_link_libraries(
    grandleon_hostile_package_tests
    PRIVATE grandleon::package_runtime grandleon::game_content
)
add_test(
    NAME grandleon.hostile_package
    COMMAND grandleon_hostile_package_tests
)

add_executable(grandleon_game_content_tests
    game_content/compiler_test.cpp
)

target_link_libraries(
    grandleon_game_content_tests
    PRIVATE grandleon::game_content
)

add_test(NAME grandleon.game_content COMMAND grandleon_game_content_tests)

add_executable(grandleon_source_project_tests
    game_content/source_project_test.cpp
)
target_link_libraries(
    grandleon_source_project_tests
    PRIVATE grandleon::game_content
)
# The art library's generated registry, so the theme menu and the terrain
# keyword table the content reader mirrors can be asserted against the one
# the generator emitted rather than trusted to stay in step.
target_include_directories(
    grandleon_source_project_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/tools/placeholder_art/assets"
)
add_test(NAME grandleon.source_project COMMAND grandleon_source_project_tests)

# The whole source-fixture corpus through the native compiler, which is where
# the seam between the schema validator and the compiler is pinned: what each
# accepts, what each refuses, and every difference named with its reason.
add_executable(grandleon_source_fixtures_tests
    game_content/source_fixtures_test.cpp
)
target_link_libraries(
    grandleon_source_fixtures_tests
    PRIVATE grandleon::game_content
)
target_compile_definitions(
    grandleon_source_fixtures_tests
    PRIVATE
        GRANDLEON_SOURCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_projects"
)
add_test(
    NAME grandleon.source_fixtures
    COMMAND grandleon_source_fixtures_tests
)

configure_file(
    cmake/package_sdk_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/package_sdk_test.cmake"
    @ONLY
)
add_test(
    NAME grandleon.package_sdk
    COMMAND
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/package_sdk_test.cmake"
)

configure_file(
    cmake/game_template_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/game_template_test.cmake"
    @ONLY
)
add_test(
    NAME grandleon.game_template
    COMMAND
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/game_template_test.cmake"
)

configure_file(
    cmake/desktop_client_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/desktop_client_test.cmake"
    @ONLY
)
add_test(
    NAME grandleon.desktop_client
    COMMAND
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/desktop_client_test.cmake"
)

if(GRANDLEON_DESKTOP_SDL_ENABLED)
    configure_file(
        cmake/sdl_presenter_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/sdl_presenter_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.sdl_presenter
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/sdl_presenter_test.cmake"
    )
endif()

configure_file(
    cmake/tarnholt_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/tarnholt_test.cmake"
    @ONLY
)
add_test(
    NAME grandleon.game_tarnholt
    COMMAND
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/tarnholt_test.cmake"
)

configure_file(
    cmake/game_demo_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/game_demo_test.cmake"
    @ONLY
)
add_test(
    NAME grandleon.game_demo
    COMMAND
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/game_demo_test.cmake"
)

# The Nintendo 64 conformance run needs a container runtime and builds a
# libdragon toolchain and ares from source on first use, which takes
# tens of minutes. It is therefore opt-in rather than part of the default gate;
# `cmake --build build --target grandleon_n64_check` does the same thing without
# reconfiguring.
#
# Opt-in is the right default and stays: a contributor with no container runtime
# would otherwise get failures that say nothing about their change, and an hour
# of emulator compilation before they got them. What was wrong was that it was
# silent. A default `ctest -R nintendo64` printed "No tests were found!!!" and
# exited 0, which reads exactly like a console gate that ran and passed. Two
# things fix that: `scripts/local-ci.sh` passes `--no-tests=error`, so a filter
# matching nothing is a failure, and the configure says out loud which lanes it
# left out.
if(GRANDLEON_N64_TESTS)
    configure_file(
        cmake/nintendo64_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_test.cmake"
    )
    set_tests_properties(grandleon.nintendo64 PROPERTIES TIMEOUT 3600)

    configure_file(
        cmake/nintendo64_probe_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_probe_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64_play
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_probe_test.cmake"
    )
    set_tests_properties(
        grandleon.nintendo64_play PROPERTIES TIMEOUT 3600
    )

    configure_file(
        cmake/nintendo64_autopilot_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_autopilot_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64_autopilot
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_autopilot_test.cmake"
    )
    set_tests_properties(
        grandleon.nintendo64_autopilot PROPERTIES TIMEOUT 3600
    )

    configure_file(
        cmake/nintendo64_campaign_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_campaign_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64_campaign
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_campaign_test.cmake"
    )
    set_tests_properties(
        grandleon.nintendo64_campaign PROPERTIES TIMEOUT 3600
    )

    # The two lanes the editor's export path owes.
    #
    # The first says the ROM an author downloads is the ROM this repository
    # checks. On this route that is nearly a tautology, because the export
    # path runs the same build script the gate runs. So the lane is deliberately
    # shaped to check the parts that are not: that the path really reaches the
    # toolchain, and that the toolchain is deterministic. It builds a different
    # game through the export path first, so a stale tree or a cached answer
    # fails on the bytes rather than passing by accident.
    #
    # A shell script rather than a `cmake -P` one, because the reference build
    # runs beside the export path's two rather than before them and CMake's
    # script mode runs one process at a time. The lane's header says which of
    # its builds are ordered against each other and why the third is not.
    configure_file(
        cmake/nintendo64_editor_rom_test.sh.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_editor_rom_test.sh"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64_editor_rom
        COMMAND
            bash "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_editor_rom_test.sh"
    )
    set_tests_properties(
        grandleon.nintendo64_editor_rom PROPERTIES TIMEOUT 3600
    )

    # The second boots a ROM built for a *different* game and requires the
    # machine to say which game it is running. Host checks structurally cannot
    # make that claim: a template that patches cleanly and runs the wrong game
    # satisfies every one of them, byte identity included, so this lane is a
    # boot rather than a comparison.
    configure_file(
        cmake/nintendo64_other_game_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_other_game_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.nintendo64_other_game
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/nintendo64_other_game_test.cmake"
    )
    set_tests_properties(
        grandleon.nintendo64_other_game PROPERTIES TIMEOUT 3600
    )
else()
    message(
        STATUS
        "ctest holds no Nintendo 64 lane: -DGRANDLEON_N64_TESTS=ON adds six, "
        "or scripts/local-ci.sh --n64 runs every check without reconfiguring"
    )
endif()

# The PlayStation entry gate, gated separately for the same reasons: its own
# toolchain image, its own emulator, and no reason to make a contributor
# working on one console build another's container.
if(GRANDLEON_PLAYSTATION_TESTS)
    configure_file(
        cmake/playstation_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_test.cmake"
    )
    set_tests_properties(grandleon.playstation PROPERTIES TIMEOUT 3600)

    # What the play executables drew, out of the emulator's own composited
    # display. A separate test from the one above because it asks a different
    # question of the same binary: that one reads text over the BIOS teletype,
    # and this one attaches a Lua script that captures the frame, checks the
    # GPU's display registers, and joins the frame against the executable's own
    # per-cell claims.
    configure_file(
        cmake/playstation_render_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_render_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation_render
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_render_test.cmake"
    )
    set_tests_properties(grandleon.playstation_render PROPERTIES TIMEOUT 3600)

    # The same board, played. A third test rather than a flag on the second
    # because its subject is not one picture but a session: the transcript the
    # rules say a controller script adds up to, and the pixels the console drew
    # at every point it settled. Its expectations are derived on the host before
    # the executable is built, which is why it has a step the other two do not.
    configure_file(
        cmake/playstation_turn_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_turn_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation_turn
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_turn_test.cmake"
    )
    set_tests_properties(grandleon.playstation_turn PROPERTIES TIMEOUT 3600)

    # The save path, against a memory card, across three emulator processes.
    # A fourth test rather than a flag on any of the others because it is the
    # only one whose subject outlives a run: what it proves is that bytes on a
    # card were still there when a different process came looking.
    configure_file(
        cmake/playstation_card_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_card_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation_card
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_card_test.cmake"
    )
    set_tests_properties(grandleon.playstation_card PROPERTIES TIMEOUT 3600)

    # Both shipped campaigns, played end to end and then picked up again in a
    # second process over the same card. A fifth test because its subject is
    # neither one picture nor one battle but a whole game: every screen the
    # campaign half composes, placed on this display, against a transcript
    # derived on the host before the executable existed.
    configure_file(
        cmake/playstation_campaign_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_campaign_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation_campaign
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_campaign_test.cmake"
    )
    set_tests_properties(
        grandleon.playstation_campaign PROPERTIES TIMEOUT 7200
    )

    # The disc, booted the way a console boots one. A sixth test because it is
    # the only one whose subject is the path in front of the executable: every
    # other run here injects the image straight into RAM with `-loadexe`, and
    # this one makes the BIOS shell read SYSTEM.CNF off an ISO 9660 filesystem
    # and load MAIN.EXE sector by sector. What comes out is compared against the
    # same host derivation the injected campaign is compared against.
    configure_file(
        cmake/playstation_disc_test.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/playstation_disc_test.cmake"
        @ONLY
    )
    add_test(
        NAME grandleon.playstation_disc
        COMMAND
            "${CMAKE_COMMAND}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/playstation_disc_test.cmake"
    )
    set_tests_properties(grandleon.playstation_disc PROPERTIES TIMEOUT 7200)
else()
    message(
        STATUS
        "ctest holds no PlayStation lane: -DGRANDLEON_PLAYSTATION_TESTS=ON "
        "adds six, or scripts/local-ci.sh --playstation runs the checks "
        "without reconfiguring"
    )
endif()

# The PlayStation turn probe's own memory safety.
#
# It sits here rather than in the PlayStation lane below on purpose: the probe
# is a single host translation unit that links nothing from this repository, so
# checking it needs a C compiler and nothing else: no container, no emulator,
# no ROM. Behind the console flag it would be checked only by whoever
# remembered to raise the flag, and the input it parses is an emulator's stdout.
find_program(GRANDLEON_BASH_EXECUTABLE bash REQUIRED)
add_test(
    NAME grandleon.playstation_turn_probe
    COMMAND
        "${GRANDLEON_BASH_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/playstation/turn_probe_test.sh"
        "${PROJECT_SOURCE_DIR}/platform/playstation/harness/playstation_turn_probe.c"
)

find_program(GRANDLEON_NODE_EXECUTABLE node REQUIRED)
add_test(
    NAME grandleon.source_schema
    COMMAND
        "${GRANDLEON_NODE_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/source_schema/test.mjs"
)

find_program(GRANDLEON_NPM_EXECUTABLE npm REQUIRED)
add_test(
    NAME grandleon.editor
    COMMAND
        "${GRANDLEON_NPM_EXECUTABLE}"
        run --prefix "${PROJECT_SOURCE_DIR}/editor" build
)
add_test(
    NAME grandleon.editor_deployment
    COMMAND
        "${GRANDLEON_NPM_EXECUTABLE}"
        run --prefix "${PROJECT_SOURCE_DIR}/editor" test:deployment
)
add_test(
    NAME grandleon.editor_offline
    COMMAND
        "${GRANDLEON_NPM_EXECUTABLE}"
        run --prefix "${PROJECT_SOURCE_DIR}/editor" test:offline
)

# The warning standard, applied to every test executable this file defines.
#
# See the note at the top. `BUILDSYSTEM_TARGETS` is read after the last
# `add_executable` above, so the list is complete by construction and cannot
# fall behind the file. The custom targets and the configured scripts in the
# same directory are filtered out by type: only something that compiles can
# carry a compile option.
#
# One warning is off, and only here. A test states the fixture it is about and
# leaves the rest of an `EncounterDefinition` (the weapons, the items, the
# terrain, the deployment tiles) to value-initialise, which is what makes the
# subject of a test readable at its call site. GCC counts that as 814 missing
# field initializers across thirteen files, every one of them the idiom working
# as intended, and spelling the omitted members out would tie every fixture in
# the suite to the order and arity of a struct it does not care about. The
# engine and the tools do not write that way and keep the warning.
set(grandleon_test_warning_flags ${GRANDLEON_WARNING_FLAGS})
if(NOT MSVC)
    list(APPEND grandleon_test_warning_flags -Wno-missing-field-initializers)
endif()

get_property(
    grandleon_test_targets
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY BUILDSYSTEM_TARGETS
)
foreach(grandleon_test_target IN LISTS grandleon_test_targets)
    get_target_property(
        grandleon_test_target_type "${grandleon_test_target}" TYPE
    )
    if(grandleon_test_target_type STREQUAL "EXECUTABLE")
        target_compile_options(
            "${grandleon_test_target}" PRIVATE ${grandleon_test_warning_flags}
        )
    endif()
endforeach()
