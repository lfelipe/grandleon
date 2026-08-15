// SPDX-License-Identifier: MIT
#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace pf = grandleon::package_format;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void patch_u16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void patch_u32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset++] =
            static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

pf::RecordSource record(std::uint64_t id, std::uint8_t marker) {
    return pf::RecordSource{id, {marker, 0x42U}};
}

pf::PackageSource representative_source() {
    pf::PackageSource source;
    source.game_id[0] = 0x47U;
    source.content_revision = 12;
    source.required_engine = {{1, 2, 0}, {1, 4, 99}};
    source.required_features = 0x04U;
    source.sections = {
        {pf::SectionType::classes, 1, 0, pf::section_flag_required,
         {record(100, 1), record(101, 2)}},
        {pf::SectionType::unit_types, 1, 1, pf::section_flag_required,
         {record(200, 3), record(201, 4), record(202, 5)}},
        {pf::SectionType::weapons, 1, 0, pf::section_flag_required,
         {record(300, 6), record(301, 7)}},
        {pf::SectionType::items, 1, 0, pf::section_flag_required,
         {record(400, 8)}},
    };
    return source;
}

pf::LoadOptions compatible_options() {
    return pf::LoadOptions{
        {1, 3, 0}, pf::TargetProfile::desktop, 0x04U, 1024, 10'000
    };
}

void round_trip_content_categories() {
    const auto bytes = pf::write_mock_package(representative_source());
    const auto loaded = pf::load_mock_package(bytes, compatible_options());
    expect(static_cast<bool>(loaded), "representative package loads");
    expect(loaded.package.sections.size() == 4, "all category sections load");
    expect(
        loaded.package.find(pf::SectionType::classes, 101) != nullptr,
        "class stable ID lookup works"
    );
    expect(
        loaded.package.find(pf::SectionType::unit_types, 202) != nullptr,
        "unit type stable ID lookup works"
    );
    expect(
        loaded.package.find(pf::SectionType::weapons, 301) != nullptr,
        "weapon stable ID lookup works"
    );
    expect(
        loaded.package.find(pf::SectionType::items, 400) != nullptr,
        "item stable ID lookup works"
    );
    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                loaded.package.game_id,
                grandleon::core::ContentCategory::unit_class,
                101,
            }
        ) != nullptr,
        "fully qualified content lookup works"
    );
    auto other_package = loaded.package.game_id;
    other_package[15] ^= 1U;
    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                other_package,
                grandleon::core::ContentCategory::unit_class,
                101,
            }
        ) == nullptr,
        "fully qualified lookup rejects another package namespace"
    );
}

// A content category is not a section type, and the fully qualified lookup has
// to know it.
//
// The two enumerations agree on the fourteen kinds a package stores records
// of, and then part. `campaign_node` and `world_flag` are identities with no
// section of their own, while 15 and 16 in the directory are `abilities` and
// `talks`. A node lives inside its campaign's record, and a flag is named by a
// transition and set by an outcome, so nothing in a package is a record *of*
// one. A lookup that carried the number across would answer a question
// about a campaign node with an ability's bytes, and hand back a record of the
// wrong shape for the caller to decode.
void a_category_without_a_section_finds_nothing() {
    auto source = representative_source();
    source.sections.push_back(
        {pf::SectionType::abilities, 1, 0, 0, {record(101, 9), record(777, 10)}}
    );
    source.sections.push_back(
        {pf::SectionType::talks, 1, 0, 0, {record(101, 11), record(888, 12)}}
    );
    const auto loaded =
        pf::load_mock_package(pf::write_mock_package(source), compatible_options());
    expect(static_cast<bool>(loaded), "a package with abilities and talks loads");

    // Both sections really are reachable by the section type they were
    // written under, so what follows is a refusal rather than a miss.
    expect(
        loaded.package.find(pf::SectionType::abilities, 777) != nullptr &&
            loaded.package.find(pf::SectionType::talks, 888) != nullptr,
        "and both new sections are indexed under their own section types"
    );

    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                loaded.package.game_id,
                grandleon::core::ContentCategory::campaign_node,
                777,
            }
        ) == nullptr,
        "a campaign node is not looked for among the abilities"
    );
    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                loaded.package.game_id,
                grandleon::core::ContentCategory::world_flag,
                888,
            }
        ) == nullptr,
        "and a world flag is not looked for among the talks"
    );

    // The identity these categories exist to keep apart, asked directly: a
    // node, a flag and a class all named 101 are three identities, and the one
    // that has a section is the only one that resolves.
    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                loaded.package.game_id,
                grandleon::core::ContentCategory::unit_class,
                101,
            }
        ) != nullptr,
        "the class named 101 still resolves"
    );
    expect(
        loaded.package.find(
            grandleon::core::ContentRef{
                loaded.package.game_id,
                grandleon::core::ContentCategory::campaign_node,
                101,
            }
        ) == nullptr &&
            loaded.package.find(
                grandleon::core::ContentRef{
                    loaded.package.game_id,
                    grandleon::core::ContentCategory::world_flag,
                    101,
                }
            ) == nullptr,
        "and the node and the flag named 101 resolve to nothing at all"
    );
}

void scalable_record_counts() {
    auto source = representative_source();
    source.sections.clear();
    pf::SectionSource classes{
        pf::SectionType::classes, 1, 0, pf::section_flag_required, {}
    };
    for (std::uint64_t id = 1; id <= 5'000; ++id) {
        classes.records.push_back(record(id, static_cast<std::uint8_t>(id)));
    }
    source.sections.push_back(std::move(classes));

    auto options = compatible_options();
    options.maximum_records_per_section = 5'000;
    const auto loaded =
        pf::load_mock_package(pf::write_mock_package(source), options);
    expect(static_cast<bool>(loaded), "5,000 records load");
    expect(
        loaded.package.sections.front().records.size() == 5'000,
        "record count is not a fixed engine constant"
    );
    expect(
        loaded.package.find(pf::SectionType::classes, 4'999) != nullptr,
        "large section remains indexed"
    );
}

void compatibility_rejections() {
    const auto bytes = pf::write_mock_package(representative_source());

    auto options = compatible_options();
    options.engine_version = {2, 0, 0};
    expect(
        pf::load_mock_package(bytes, options).error ==
            pf::Error::incompatible_engine,
        "engine outside declared range is rejected"
    );

    options = compatible_options();
    options.supported_features = 0;
    expect(
        pf::load_mock_package(bytes, options).error ==
            pf::Error::unsupported_feature,
        "missing required feature is rejected"
    );

    auto targeted_source = representative_source();
    targeted_source.target = pf::TargetProfile::nintendo64;
    expect(
        pf::load_mock_package(
            pf::write_mock_package(targeted_source),
            compatible_options()
        ).error == pf::Error::incompatible_target,
        "package for another target is rejected"
    );

    auto future_container = bytes;
    patch_u16(future_container, 4, pf::container_major + 1);
    expect(
        pf::load_mock_package(future_container, compatible_options()).error ==
            pf::Error::unsupported_container,
        "future container major is rejected"
    );

    auto future_required_schema = representative_source();
    future_required_schema.sections.front().schema_major = 2;
    expect(
        pf::load_mock_package(
            pf::write_mock_package(future_required_schema),
            compatible_options()
        ).error == pf::Error::unsupported_schema,
        "unknown required section schema is rejected"
    );
}

void optional_evolution() {
    auto source = representative_source();
    source.sections.push_back(
        {static_cast<pf::SectionType>(9000), 1, 0, 0, {record(1, 1)}}
    );
    const auto loaded =
        pf::load_mock_package(pf::write_mock_package(source), compatible_options());
    expect(static_cast<bool>(loaded), "unknown optional section can be skipped");
    expect(loaded.package.sections.size() == 4, "optional section is not exposed");

    source.sections.back().flags = pf::section_flag_required;
    expect(
        pf::load_mock_package(
            pf::write_mock_package(source),
            compatible_options()
        ).error == pf::Error::unsupported_required_section,
        "unknown required section is rejected"
    );
}

void corruption_rejections() {
    const auto valid = pf::write_mock_package(representative_source());

    auto truncated = valid;
    truncated.pop_back();
    expect(
        pf::load_mock_package(truncated, compatible_options()).error ==
            pf::Error::invalid_directory,
        "truncation is rejected"
    );

    auto corrupt = valid;
    corrupt.back() ^= 0xffU;
    expect(
        pf::load_mock_package(corrupt, compatible_options()).error ==
            pf::Error::checksum_mismatch,
        "section corruption is rejected"
    );

    auto bad_offset = valid;
    // First directory entry begins at 72; its section offset field is +16.
    patch_u32(bad_offset, 88, 0xfffffff0U);
    expect(
        pf::load_mock_package(bad_offset, compatible_options()).error ==
            pf::Error::checksum_mismatch,
        "directory corruption is rejected by envelope checksum"
    );

    auto overflowing_directory = valid;
    patch_u32(overflowing_directory, 60, 0xffffffffU);
    expect(
        pf::load_mock_package(
            overflowing_directory,
            compatible_options()
        ).error == pf::Error::invalid_directory,
        "overflowing directory count is rejected before arithmetic"
    );

    auto reversed_range = representative_source();
    reversed_range.required_engine = {{2, 0, 0}, {1, 0, 0}};
    expect(
        pf::load_mock_package(
            pf::write_mock_package(reversed_range),
            compatible_options()
        ).error == pf::Error::incompatible_engine,
        "reversed engine range is rejected"
    );

    auto duplicate_records = representative_source();
    duplicate_records.sections.front().records.push_back(record(100, 9));
    expect(
        pf::load_mock_package(
            pf::write_mock_package(duplicate_records),
            compatible_options()
        ).error == pf::Error::duplicate_record,
        "duplicate stable record ID is rejected"
    );

    auto duplicate_sections = representative_source();
    duplicate_sections.sections.push_back(
        {pf::SectionType::classes, 1, 0, 0, {}}
    );
    expect(
        pf::load_mock_package(
            pf::write_mock_package(duplicate_sections),
            compatible_options()
        ).error == pf::Error::duplicate_section,
        "duplicate section type is rejected"
    );
}

void bounded_loading() {
    const auto bytes = pf::write_mock_package(representative_source());
    auto options = compatible_options();
    options.maximum_sections = 3;
    expect(
        pf::load_mock_package(bytes, options).error ==
            pf::Error::invalid_directory,
        "runtime section budget is enforced"
    );
    options = compatible_options();
    options.maximum_records_per_section = 2;
    expect(
        pf::load_mock_package(bytes, options).error ==
            pf::Error::invalid_section,
        "runtime record budget is enforced"
    );
}

// A package read where its bytes already are means what the same package
// copied means, and is refused for the same reasons.
//
// The comparison is made record by record against the bytes each load says to
// read, rather than against the bytes the test happens to hold, because that is
// the only version of the claim that would notice a borrowing load addressing
// the wrong buffer.
void reading_a_package_in_place() {
    const auto bytes = pf::write_mock_package(representative_source());
    const auto copied = pf::load_mock_package(bytes, compatible_options());
    const auto borrowed = pf::load_mock_package_in_place(
        {bytes.data(), bytes.size()}, compatible_options()
    );
    expect(static_cast<bool>(borrowed), "a package loads where it already is");
    expect(
        borrowed.package.bytes.empty() &&
            borrowed.package.byte_data() == bytes.data() &&
            borrowed.package.byte_size() == bytes.size(),
        "a borrowing load copies nothing and points at the caller's bytes"
    );
    expect(
        !copied.package.bytes.empty() &&
            copied.package.byte_data() == copied.package.bytes.data(),
        "an owning load still holds the copy it always held"
    );
    expect(
        copied.package.game_id == borrowed.package.game_id &&
            copied.package.content_revision ==
                borrowed.package.content_revision &&
            copied.package.target == borrowed.package.target &&
            copied.package.required_features ==
                borrowed.package.required_features &&
            copied.package.required_engine.minimum ==
                borrowed.package.required_engine.minimum &&
            copied.package.required_engine.maximum ==
                borrowed.package.required_engine.maximum,
        "both loads publish the same envelope"
    );

    bool identical = copied.package.sections.size() ==
                     borrowed.package.sections.size();
    for (std::size_t index = 0;
         identical && index < copied.package.sections.size();
         ++index) {
        const pf::SectionView& left = copied.package.sections[index];
        const pf::SectionView& right = borrowed.package.sections[index];
        identical = left.type == right.type &&
                    left.schema_major == right.schema_major &&
                    left.schema_minor == right.schema_minor &&
                    left.flags == right.flags &&
                    left.records.size() == right.records.size();
        for (std::size_t at = 0; identical && at < left.records.size(); ++at) {
            identical = left.records[at].stable_id ==
                            right.records[at].stable_id &&
                        left.records[at].payload_size ==
                            right.records[at].payload_size;
            for (std::uint32_t byte = 0;
                 identical && byte < left.records[at].payload_size;
                 ++byte) {
                identical =
                    copied.package
                        .byte_data()[left.records[at].payload_offset + byte] ==
                    borrowed.package
                        .byte_data()[right.records[at].payload_offset + byte];
            }
        }
    }
    expect(identical, "both loads decode to the same records, byte for byte");

    // A package that borrows survives being handed on. A cached pointer into
    // the owning vector would answer the wrong buffer here; the rule that picks
    // between owned and borrowed on every call does not.
    const pf::LoadedPackage passed_on = borrowed.package;
    const pf::RecordView* const record =
        passed_on.find(pf::SectionType::items, 400);
    expect(
        record != nullptr && passed_on.byte_data() == bytes.data() &&
            passed_on.byte_data()[record->payload_offset] == 8U,
        "a borrowing package copied to another owner still reads the caller's "
        "bytes"
    );
    pf::LoadedPackage moved_on = copied.package;
    const pf::LoadedPackage received = std::move(moved_on);
    const pf::RecordView* const owned_record =
        received.find(pf::SectionType::items, 400);
    expect(
        owned_record != nullptr &&
            received.byte_data() == received.bytes.data() &&
            received.byte_data()[owned_record->payload_offset] == 8U,
        "an owning package moved to another owner reads its own copy"
    );
}

// Every refusal the owning load makes, the borrowing load makes for the same
// bytes. The list is the corruption suite's own list, walked twice.
void refusing_a_package_in_place() {
    const auto good = pf::write_mock_package(representative_source());
    const auto options = compatible_options();

    std::vector<std::vector<std::uint8_t>> damaged;
    damaged.push_back({});
    damaged.push_back(std::vector<std::uint8_t>(good.begin(), good.begin() + 40));
    {
        auto bytes = good;
        bytes[0] = 'X';
        damaged.push_back(bytes);
    }
    {
        auto bytes = good;
        patch_u16(bytes, 6, pf::container_minor + 1U);
        damaged.push_back(bytes);
    }
    {
        auto bytes = good;
        bytes[bytes.size() - 1] = static_cast<std::uint8_t>(
            bytes[bytes.size() - 1] ^ 0xffU
        );
        damaged.push_back(bytes);
    }
    {
        auto bytes = good;
        patch_u32(bytes, 12, static_cast<std::uint32_t>(bytes.size() + 1U));
        damaged.push_back(bytes);
    }

    bool agree = true;
    bool all_refused = true;
    for (const std::vector<std::uint8_t>& bytes : damaged) {
        const pf::Error owning = pf::load_mock_package(bytes, options).error;
        const pf::Error borrowing =
            pf::load_mock_package_in_place({bytes.data(), bytes.size()}, options)
                .error;
        agree = agree && owning == borrowing;
        all_refused = all_refused && owning != pf::Error::none;
    }
    expect(all_refused, "every damaged package is refused by the owning load");
    expect(agree, "a damaged package is refused identically either way");

    expect(
        pf::load_mock_package_in_place({nullptr, 0}, options).error ==
            pf::Error::truncated,
        "no bytes at all is truncated rather than a read through nothing"
    );
}

}  // namespace

int main() {
    round_trip_content_categories();
    a_category_without_a_section_finds_nothing();
    scalable_record_counts();
    compatibility_rejections();
    optional_evolution();
    corruption_rejections();
    bounded_loading();
    reading_a_package_in_place();
    refusing_a_package_in_place();
    return failures == 0 ? 0 : 1;
}
