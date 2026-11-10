// SPDX-License-Identifier: MIT
// The compact-console rule that decides whether a company screen has a real
// decision on it. N64 and PlayStation call this exact helper before opening
// their editor, so these cases pin the player-facing direct-start contract
// without making an emulator infer it from a particular campaign fixture.

#include <grandleon/client/campaign_session.hpp>

#include <iostream>
#include <string_view>

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace runtime = grandleon::campaign_runtime;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

client::CompanyManagement fixed_company() {
    client::CompanyManagement company;
    company.roster.emplace_back();
    company.roster.back().member = {1};
    company.roster.back().availability = campaign::Availability::available;
    return company;
}

}  // namespace

int main() {
    client::CompanyManagement empty;
    expect(
        !client::company_can_start_directly(empty),
        "an empty company does not start"
    );

    client::CompanyManagement fixed = fixed_company();
    expect(
        client::company_can_start_directly(fixed),
        "a valid fixed formation with no inventory starts directly"
    );

    client::CompanyManagement deployment = fixed_company();
    deployment.has_deployment_options = true;
    expect(
        !client::company_can_start_directly(deployment),
        "a deployment choice opens management"
    );

    client::CompanyManagement store = fixed_company();
    store.store.emplace_back();
    expect(
        !client::company_can_start_directly(store),
        "a shared-store choice opens management"
    );

    client::CompanyManagement carried = fixed_company();
    carried.roster.front().carried.emplace_back();
    expect(
        client::company_can_start_directly(carried),
        "an equipped fixed formation starts directly"
    );

    client::CompanyManagement refused = fixed_company();
    refused.refused = runtime::RosterError::side_emptied;
    expect(
        !client::company_can_start_directly(refused),
        "a roster refusal opens management"
    );

    client::CompanyManagement errored = fixed_company();
    errored.error = client::CampaignSessionError::board_rejected;
    expect(
        !client::company_can_start_directly(errored),
        "a session error opens management"
    );

    if (failures != 0) return 1;
    std::cout << "compact-console direct-start policy passed\n";
    return 0;
}
