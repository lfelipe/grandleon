// SPDX-License-Identifier: MIT
// The whole source-fixture corpus, put through the native compiler.
//
// `tests/fixtures/source_projects/valid` means *valid source*: what
// `tools/source_schema/validate.mjs` accepts. It does not mean "compiles to a
// native package", and the two are deliberately different sets. The native
// compiler is narrower, because it will not emit a package holding something no
// runtime can execute. `tools/game_content/README.md` lists what it refuses on
// those grounds, and every such refusal is named below with the diagnostic that
// carries it. A fixture that compiles is not listed; a fixture that does not
// must be, with a reason, or this test fails.
//
// Read the two tables as the seam between the analyzers. Everything the JS
// validator refuses, the compiler refuses too, bar the one entry in
// `accepted_though_invalid` and the reason written against it. Everything the
// JS validator accepts, the compiler compiles, bar the entries in
// `refused_though_valid` and the reasons written against them. A rule that
// drifts out of step on either side shows up here as a fixture that changed
// category.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gc = grandleon::game_content;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// What the compiler refuses although the source is valid, and the reason it is
// entitled to. Each value is a substring of the diagnostic that must carry the
// refusal, so a fixture refused for some *other* reason is a failure rather
// than a pass.
const std::map<std::string, std::string> refused_though_valid = {
    // Runtime extension data, which has no runtime at all. Named in
    // `tools/game_content/README.md`.
    {"script-bindings", "$.abilities[0].scriptBindings"},
    // A campaign predicate over inventory, for which there is no runtime state
    // to evaluate it against. Also named in the README. This fixture is the
    // corpus's one non-linear flow, which is why it is authored with the
    // predicate rather than moved.
    {"nonlinear-campaign", "when.kind"},
    // A campaign authored as a portable membership registry rather than as
    // something to play. `campaign::load_campaign` refuses a record holding no
    // node, so there is nothing to emit.
    {"authoring-registries", "$.campaigns[0].flow"},
};

// What the compiler accepts although the source is invalid. One entry, and the
// division of labour behind it: an identifier's spelling is an authoring
// convention that vanishes at the first hash, so nothing the compiler writes or
// any runtime reads can tell a well-spelled key from a badly spelled one. The
// package identity is the opposite case: it reaches the package bytes
// unhashed, and the reader does hold that one to the schema's shape.
const std::map<std::string, std::string> accepted_though_invalid = {
    {"bad-identifier", "a stable identifier's spelling is not a package rule"},
};

// Rules both analyzers hold, pinned on one fixture that carries them and
// nothing else. `tools/source_schema/test.mjs` asserts the same file produces
// exactly the three matching JS diagnostics, and
// `editor/src/analysis/source-conformance.test.ts` asserts the editor produces
// the same three again. A rule held in one of three places is a rule that will
// drift, so the three lists are the same length by construction and a change to
// any one of them shows up as a failure in the other two.
const std::vector<std::pair<std::string, std::vector<std::string>>>
    rules_both_analyzers_hold = {
        {
            "campaign-flow-routing",
            {
                // SOURCE_REF_MISSING at .../dialogueIds/1
                "missing_reference: campaigns[4619284293332167049]"
                ".nodes.dialogue_ids",
                // SOURCE_CAMPAIGN_FALLBACK_DUPLICATE at .../transitions/3
                "invalid_transition: campaigns[4619284293332167049]"
                ".nodes.targets",
                // SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE at
                // .../transitions/1/priority
                "invalid_transition: campaigns[4619284293332167049]"
                ".nodes.transitions.priority",
            }
        },
        {
            // Every number the damage arithmetic reads, written past
            // `simulation::maximum_stat` in each of the seven places one can
            // be written. The compiler asks the engine for that bound, the
            // schema writes it once in `common.schema.json`'s `damageStat`,
            // and `tools/source_schema/test.mjs` asserts the same seven
            // instance paths. That way a project the schema admits and the
            // board refuses cannot come back.
            "stat-past-the-damage-cap",
            {
                "$.classes[0].baseStats.strength: 20000 is outside the 0 to "
                "16383",
                "$.classes[0].baseStats.defense: 16384 is outside the 0 to "
                "16383",
                "$.classes[0].baseStats.resistance: 32767 is outside the 0 to "
                "16383",
                "$.classes[0].baseStats.magic: 20000 is outside the 0 to "
                "16383",
                "$.weapons[0].power: 20000 is outside the 0 to 16383",
                "$.items[0].power: 20000 is outside the 0 to 16383",
                "$.abilities[0].power: 20000 is outside the 0 to 16383",
            }
        },
};

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

// Every diagnostic either analyzer half of the pipeline produced, as one
// string, so a table entry can name the reason without knowing which half said
// it.
std::string refusal(const std::string& document) {
    const auto parsed = gc::parse_source_project_json(document);
    std::string reported;
    for (const gc::SourceDiagnostic& diagnostic : parsed.diagnostics) {
        reported += std::string(source_diagnostic_name(diagnostic.code)) +
            ": " + diagnostic.path + ": " + diagnostic.detail + '\n';
    }
    if (!parsed) return reported;
    const auto compiled = gc::compile(parsed.source);
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        reported +=
            std::string(diagnostic_name(diagnostic.code)) + ": " +
            diagnostic.path + '\n';
    }
    return reported;
}

std::vector<std::filesystem::path> fixtures(std::string_view category) {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path directory =
        std::filesystem::path(GRANDLEON_SOURCE_FIXTURE_DIR) / category;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

void every_valid_fixture_compiles_or_is_refused_for_a_named_reason() {
    const auto paths = fixtures("valid");
    expect(!paths.empty(), "the valid fixture corpus is not empty");
    std::map<std::string, bool> exercised;
    for (const auto& [name, reason] : refused_though_valid) {
        exercised[name] = false;
    }

    for (const std::filesystem::path& path : paths) {
        const std::string name = path.stem().string();
        const std::string reported = refusal(read(path));
        const auto listed = refused_though_valid.find(name);
        if (listed == refused_though_valid.end()) {
            expect(
                reported.empty(),
                "valid fixture '" + name + "' compiles:\n" + reported
            );
            continue;
        }
        exercised[name] = true;
        expect(
            !reported.empty(),
            "valid fixture '" + name +
                "' is listed as refused but now compiles; take it off the list"
        );
        expect(
            reported.find(listed->second) != std::string::npos,
            "valid fixture '" + name + "' is refused for the listed reason '" +
                listed->second + "', not:\n" + reported
        );
    }

    for (const auto& [name, seen] : exercised) {
        expect(
            seen,
            "the exception list names '" + name +
                "', which is no longer a fixture"
        );
    }
}

void every_invalid_fixture_is_refused_or_excused_by_name() {
    const auto paths = fixtures("invalid");
    expect(!paths.empty(), "the invalid fixture corpus is not empty");
    std::map<std::string, bool> exercised;
    for (const auto& [name, reason] : accepted_though_invalid) {
        exercised[name] = false;
    }

    for (const std::filesystem::path& path : paths) {
        const std::string name = path.stem().string();
        const bool refused = !refusal(read(path)).empty();
        const auto excused = accepted_though_invalid.find(name);
        if (excused == accepted_though_invalid.end()) {
            expect(
                refused,
                "invalid fixture '" + name +
                    "' is refused by the compiler as well as by the schema "
                    "validator"
            );
            continue;
        }
        exercised[name] = true;
        expect(
            !refused,
            "invalid fixture '" + name +
                "' is excused as accepted, but the compiler now refuses it; "
                "take it off the list"
        );
    }

    for (const auto& [name, seen] : exercised) {
        expect(
            seen,
            "the excuse list names '" + name +
                "', which is no longer a fixture"
        );
    }
}

void names_every_fault_the_schema_validator_names() {
    for (const auto& [name, expected] : rules_both_analyzers_hold) {
        const std::filesystem::path path =
            std::filesystem::path(GRANDLEON_SOURCE_FIXTURE_DIR) / "invalid" /
            (name + ".json");
        const std::string reported = refusal(read(path));
        for (const std::string& fault : expected) {
            expect(
                reported.find(fault) != std::string::npos,
                "'" + name + "' is refused with '" + fault + "', not:\n" +
                    reported
            );
        }
        // Nothing else, so the fixture keeps saying exactly what it was written
        // to say and cannot start passing for an unrelated reason.
        expect(
            std::count(reported.begin(), reported.end(), '\n') ==
                static_cast<std::ptrdiff_t>(expected.size()),
            "'" + name + "' is refused for those reasons and no others:\n" +
                reported
        );
    }
}

}  // namespace

int main() {
    every_valid_fixture_compiles_or_is_refused_for_a_named_reason();
    every_invalid_fixture_is_refused_or_excused_by_name();
    names_every_fault_the_schema_validator_names();
    return failures == 0 ? 0 : 1;
}
