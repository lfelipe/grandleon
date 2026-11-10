// SPDX-License-Identifier: MIT
// The shared PlayStation campaign client, driven through its real management
// seam. This checks that an author's permission is the only route to the
// keyboard and that its answer is the existing portable rename intent.

#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>

#include <iostream>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

class NamePilot final : public turn::TurnClient {
public:
    explicit NamePilot(turn::ReportSink& sink) : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    void paint_screen(const turn::ScreenView& view) override {
        if (view.screen == turn::Screen::name) ++name_screens_;
        if (view.screen == turn::Screen::story && view.page != nullptr) {
            std::vector<std::string> page;
            for (int row = 0; row < view.page->count; ++row) {
                page.emplace_back(view.page->line(row));
            }
            story_pages_.push_back(std::move(page));
        }
        if (view.screen == turn::Screen::member && view.verbs != nullptr) {
            member_offered_rename_ = false;
            for (int row = 0; row < view.items; ++row) {
                if (view.verbs[row].verb == client::ManagementVerb::rename) {
                    member_offered_rename_ = true;
                }
            }
        }
    }

    [[nodiscard]] std::uint16_t next_press() override {
        if (presses_.empty()) return turn::pad_end_of_script;
        const std::uint16_t next = presses_.front();
        presses_.erase(presses_.begin());
        return next;
    }

    void press(std::uint16_t value) { presses_.push_back(value); }
    [[nodiscard]] int name_screens() const noexcept { return name_screens_; }
    [[nodiscard]] bool member_offered_rename() const noexcept {
        return member_offered_rename_;
    }
    [[nodiscard]] const std::vector<std::vector<std::string>>& story_pages()
        const noexcept {
        return story_pages_;
    }

private:
    std::vector<std::uint16_t> presses_;
    std::vector<std::vector<std::string>> story_pages_;
    int name_screens_{0};
    bool member_offered_rename_{false};
};

client::RosterEntry member(
    bool nameable, bool custom = false
) {
    client::RosterEntry out;
    out.member = {1};
    out.placement_source_key =
        grandleon::core::stable_content_id_v1("aren");
    out.name = custom ? "Luzia" : "Aren";
    out.authored_name = "Aren";
    out.player_nameable = nameable;
    out.has_player_name = custom;
    out.availability = campaign::Availability::available;
    return out;
}

client::CompanyManagement company_of(const client::RosterEntry& entry) {
    client::CompanyManagement company;
    company.roster.push_back(entry);
    return company;
}

void type_lu(NamePilot& pilot) {
    for (int i = 0; i < 4; ++i) pilot.press(turn::pad_b);
    pilot.press(turn::pad_down);
    pilot.press(turn::pad_right);
    pilot.press(turn::pad_right);
    pilot.press(turn::pad_a);  // L
    pilot.press(turn::pad_down);
    pilot.press(turn::pad_a);  // U
    pilot.press(turn::pad_start);
}

bool page_contains(
    const std::vector<std::vector<std::string>>& pages,
    std::string_view wanted
) {
    for (const auto& page : pages) {
        for (const std::string& line : page) {
            if (line.find(wanted) != std::string::npos) return true;
        }
    }
    return false;
}

void present_name_tokens(NamePilot& pilot) {
    grandleon::package_runtime::Dialogue dialogue;
    grandleon::package_runtime::DialogueLine line;
    line.speaker = "{{member:aren}}";
    line.text = "Hold, {{member:aren}}. Missing {{member:missing}}.";
    dialogue.lines.push_back(line);
    pilot.press(turn::pad_a);
    pilot.present_dialogue(dialogue);
}

}  // namespace

int main() {
    QuietSink sink;

    const client::RosterEntry fixed = member(false);
    const client::CompanyManagement fixed_company = company_of(fixed);
    NamePilot fixed_pilot{sink};
    fixed_pilot.campaign_begun(fixed_company.roster, {}, "slot", false);
    const client::ManagementIntent fixed_intent =
        fixed_pilot.next_management_intent(fixed_company);
    expect(
        fixed_intent.verb == client::ManagementVerb::proceed &&
            fixed_pilot.name_screens() == 0,
        "a fixed formation stays on the existing direct-start path"
    );

    const client::RosterEntry eligible = member(true);
    const client::CompanyManagement eligible_company = company_of(eligible);
    NamePilot founding{sink};
    founding.campaign_begun(eligible_company.roster, {}, "slot", false);
    type_lu(founding);
    const client::ManagementIntent renamed =
        founding.next_management_intent(eligible_company);
    expect(
        renamed.verb == client::ManagementVerb::rename &&
            renamed.member == eligible.member && renamed.name == "LU" &&
            founding.name_screens() > 0,
        "a nameable founder returns the portable rename intent"
    );

    NamePilot cancelled{sink};
    cancelled.campaign_begun(eligible_company.roster, {}, "slot", false);
    cancelled.press(turn::pad_c);
    const client::ManagementIntent kept =
        cancelled.next_management_intent(eligible_company);
    expect(
        kept.verb == client::ManagementVerb::proceed,
        "cancel keeps the authored default and continues immediately"
    );

    NamePilot resumed{sink};
    resumed.campaign_begun(eligible_company.roster, {}, "slot", true);
    const client::ManagementIntent resumed_intent =
        resumed.next_management_intent(eligible_company);
    expect(
        resumed_intent.verb == client::ManagementVerb::proceed &&
            resumed.name_screens() == 0,
        "resume never repeats the founding name prompt"
    );

    NamePilot eligible_recruit{sink};
    eligible_recruit.campaign_begun(fixed_company.roster, {}, "slot", true);
    eligible_recruit.press(turn::pad_a);  // dismiss JOINS THE COMPANY
    eligible_recruit.members_joined({eligible});
    type_lu(eligible_recruit);
    const client::ManagementIntent recruit_name =
        eligible_recruit.next_management_intent(eligible_company);
    const int recruit_name_screens = eligible_recruit.name_screens();
    const client::ManagementIntent after_recruit_name =
        eligible_recruit.next_management_intent(eligible_company);
    expect(
        recruit_name.verb == client::ManagementVerb::rename &&
            recruit_name.member == eligible.member &&
            recruit_name.name == "LU" &&
            after_recruit_name.verb == client::ManagementVerb::proceed &&
            eligible_recruit.name_screens() == recruit_name_screens,
        "an eligible recruit queues exactly one naming prompt"
    );

    NamePilot fixed_recruit{sink};
    fixed_recruit.campaign_begun(fixed_company.roster, {}, "slot", true);
    fixed_recruit.press(turn::pad_a);  // dismiss JOINS THE COMPANY
    fixed_recruit.members_joined({fixed});
    const client::ManagementIntent fixed_recruit_intent =
        fixed_recruit.next_management_intent(fixed_company);
    expect(
        fixed_recruit_intent.verb == client::ManagementVerb::proceed &&
            fixed_recruit.name_screens() == 0,
        "a fixed recruit never queues the keyboard"
    );

    client::CompanyManagement editable = company_of(member(true, true));
    editable.has_deployment_options = true;
    NamePilot reset{sink};
    reset.campaign_begun(editable.roster, {}, "slot", true);
    reset.press(turn::pad_a);  // open the member
    reset.press(turn::pad_a);  // choose CHANGE NAME
    for (int row = 0; row < 4; ++row) reset.press(turn::pad_down);
    for (int column = 0; column < 3; ++column) {
        reset.press(turn::pad_right);
    }
    reset.press(turn::pad_a);  // DEFAULT
    const client::ManagementIntent reset_intent =
        reset.next_management_intent(editable);
    expect(
        reset_intent.verb == client::ManagementVerb::rename &&
            reset_intent.name == "Aren" && reset.member_offered_rename(),
        "an eligible member menu can clear a custom name to its exact default"
    );

    client::CompanyManagement fixed_editor = fixed_company;
    fixed_editor.has_deployment_options = true;
    NamePilot no_fixed_row{sink};
    no_fixed_row.campaign_begun(fixed_editor.roster, {}, "slot", true);
    no_fixed_row.press(turn::pad_a);
    no_fixed_row.press(turn::pad_b);
    const client::ManagementIntent untouched =
        no_fixed_row.next_management_intent(fixed_editor);
    expect(
        untouched.verb == client::ManagementVerb::none &&
            !no_fixed_row.member_offered_rename() &&
            no_fixed_row.name_screens() == 0,
        "a fixed member has neither a rename row nor a keyboard"
    );

    client::RosterEntry external = member(true, true);
    external.name = "Lu\xC3\xADza";
    client::CompanyManagement external_company = company_of(external);
    external_company.has_deployment_options = true;
    NamePilot keep_external{sink};
    keep_external.campaign_begun(
        external_company.roster, {}, "slot", true
    );
    keep_external.press(turn::pad_a);  // open member
    keep_external.press(turn::pad_a);  // choose CHANGE NAME
    keep_external.press(turn::pad_c);  // keep the external literal
    const client::ManagementIntent external_intent =
        keep_external.next_management_intent(external_company);
    expect(
        external_intent.verb == client::ManagementVerb::none &&
            keep_external.name_screens() > 0,
        "Cancel preserves an unsupported external name without a rename intent"
    );

    NamePilot authored_dialogue{sink};
    authored_dialogue.campaign_begun({member(true)}, {}, "slot", true);
    present_name_tokens(authored_dialogue);
    expect(
        page_contains(authored_dialogue.story_pages(), "AREN:") &&
            page_contains(authored_dialogue.story_pages(), "HOLD, AREN.") &&
            page_contains(
                authored_dialogue.story_pages(), "MEMBER:MISSING"
            ),
        "console dialogue resolves the authored effective name and keeps an "
        "unknown token visibly diagnosable"
    );

    NamePilot custom_dialogue{sink};
    custom_dialogue.campaign_begun({member(true, true)}, {}, "slot", true);
    present_name_tokens(custom_dialogue);
    expect(
        page_contains(custom_dialogue.story_pages(), "LUZIA:") &&
            page_contains(custom_dialogue.story_pages(), "HOLD, LUZIA."),
        "console dialogue resolves the player's effective custom name"
    );

    if (failures != 0) return 1;
    std::cout << "compact-console name entry passed\n";
    return 0;
}
