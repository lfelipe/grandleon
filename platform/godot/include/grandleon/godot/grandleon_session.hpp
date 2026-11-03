// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/storage/filesystem_storage.hpp>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace grandleon::godot_frontend {

struct MotionPresentation final {
    simulation::UnitId unit_id{};
    std::vector<simulation::Position> route;
    std::int32_t duration_msec{};
};

struct FrontendEvent final {
    enum class Kind : std::uint8_t {
        dialogue,
        battle_begins,
        battle_surroundings,
        definitions,
        deployment_begins,
        presentation,
        snapshot,
        refused,
        battle_ended,
        campaign_ended,
        campaign_begun,
        checkpoint_saved,
        checkpoint_refused,
        management,
        management_committed,
        session_finished,
        session_failed,
    };

    Kind kind{Kind::snapshot};
    simulation::EncounterSnapshot snapshot;
    std::vector<std::uint64_t> terrain;
    package_runtime::MapSurroundings surroundings;
    std::vector<simulation::Position> zone;
    std::vector<simulation::WeaponDefinition> weapons;
    std::vector<simulation::AbilityDefinition> abilities;
    std::vector<simulation::ItemDefinition> items;
    std::vector<simulation::ObjectiveDefinition> objectives;
    std::vector<simulation::Event> presentation_events;
    std::optional<MotionPresentation> motion;
    std::string action;
    simulation::ContentId action_content_id{};
    package_runtime::Dialogue dialogue;
    client::CompanyManagement company;
    simulation::Side player_side{simulation::Side::first};
    std::uint64_t canonical_hash{};
    std::int64_t request_id{};
    bool resumed{false};
    bool successful{false};
    std::string message;
};

class AsyncPresenter final : public client::Presenter,
                             public client::CampaignNarrator {
public:
    AsyncPresenter() = default;
    ~AsyncPresenter() override = default;

    void present_dialogue(const package_runtime::Dialogue& dialogue) override;
    void battle_begins(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        simulation::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) override;
    void battle_surroundings(
        const package_runtime::MapSurroundings& surroundings
    ) override;
    void battle_definitions(
        const std::vector<simulation::WeaponDefinition>& weapons,
        const std::vector<simulation::AbilityDefinition>& abilities,
        const std::vector<simulation::ItemDefinition>& items,
        const std::vector<simulation::ObjectiveDefinition>& objectives
    ) override;
    void deployment_begins(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        const std::vector<simulation::Position>& zone
    ) override;
    void draw(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override;
    void report(
        const simulation::CommandResult& result,
        const client::Roster& roster
    ) override;
    void refused(simulation::CommandError error) override;
    void show_state(
        const simulation::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<simulation::ObjectiveDefinition>& objectives
    ) override;
    void battle_ended(
        const simulation::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash
    ) override;
    void campaign_ended() override;
    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override;
    void slot_refused(const client::SlotFailure& failure) override;
    void board_prepared(const client::CampaignBoard& board) override;
    void battle_aftermath(const client::BattleAftermath& aftermath) override;
    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override;
    void campaign_saved(
        std::string_view slot, storage::StorageError error
    ) override;
    void management_opened(
        const client::CompanyManagement& company
    ) override;
    void management_committed(
        const client::ManagementCommit& result
    ) override;
    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override;
    [[nodiscard]] client::Intent next_intent(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override;
    [[nodiscard]] client::Intent next_deployment_intent(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override;
    void submit(client::Intent intent, std::int64_t request_id);
    [[nodiscard]] bool submit_management(client::ManagementIntent intent);
    void stop();
    void push(FrontendEvent event);
    [[nodiscard]] std::deque<FrontendEvent> take_events();

private:
    struct QueuedIntent final {
        client::Intent intent;
        std::int64_t request_id{};
    };

    [[nodiscard]] client::Intent wait_for_intent(
        const simulation::EncounterSnapshot& snapshot
    );

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<FrontendEvent> events_;
    std::deque<QueuedIntent> intents_;
    std::int64_t active_request_id_{};
    std::string active_action_;
    simulation::ContentId active_action_content_id_{};
    std::optional<MotionPresentation> active_motion_;
    std::optional<client::ManagementIntent> management_intent_;
    bool awaiting_management_{false};
    bool stopping_{false};
};

class GrandleonSession final : public ::godot::RefCounted {
    GDCLASS(GrandleonSession, ::godot::RefCounted)

public:
    GrandleonSession();
    ~GrandleonSession() override;

    [[nodiscard]] bool start(
        const ::godot::String& package_path,
        const ::godot::String& campaign_key,
        std::int64_t local_players = 1,
        const ::godot::String& save_root = {}
    );
    [[nodiscard]] bool start_package(
        const ::godot::PackedByteArray& package_bytes,
        const ::godot::String& campaign_key,
        std::int64_t local_players = 1,
        const ::godot::String& save_root = {}
    );
    [[nodiscard]] bool has_progress() const;
    [[nodiscard]] bool reset_progress();
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] ::godot::String last_error() const;
    [[nodiscard]] ::godot::Array poll();
    [[nodiscard]] ::godot::Array query_tiles(
        const ::godot::String& kind,
        std::int64_t unit_id,
        std::int64_t content_id = 0
    ) const;
    void submit_intent(
        const ::godot::String& kind,
        std::int64_t unit_id = 0,
        std::int64_t target_id = 0,
        std::int64_t x = 0,
        std::int64_t y = 0,
        std::int64_t content_id = 0,
        std::int64_t request_id = 0
    );
    [[nodiscard]] bool submit_management(
        const ::godot::String& kind,
        std::int64_t member_id = 0,
        std::int64_t item_id = 0,
        std::int64_t stage_id = 0
    );

protected:
    static void _bind_methods();

private:
    [[nodiscard]] ::godot::Dictionary event_dictionary(
        const FrontendEvent& event
    ) const;
    [[nodiscard]] ::godot::Dictionary snapshot_dictionary(
        const simulation::EncounterSnapshot& snapshot
    ) const;
    [[nodiscard]] ::godot::Dictionary management_dictionary(
        const client::CompanyManagement& company
    ) const;
    void reset_for_start();
    [[nodiscard]] bool start_bytes(
        const std::vector<std::uint8_t>& package_bytes,
        const ::godot::String& campaign_key,
        std::int64_t local_players,
        const ::godot::String& save_root
    );
    [[nodiscard]] bool launch_worker();
    void join_worker();

    std::shared_ptr<AsyncPresenter> presenter_;
    std::thread worker_;
    package_format::LoadedPackage package_;
    package_runtime::Presentation presentation_;
    std::unique_ptr<storage::FilesystemSlotStorage> storage_;
    mutable std::mutex state_mutex_;
    std::string last_error_;
    std::string save_slot_;
    std::uint64_t campaign_id_{};
    simulation::EncounterSnapshot latest_snapshot_;
    std::vector<simulation::WeaponDefinition> latest_weapons_;
    std::vector<simulation::AbilityDefinition> latest_abilities_;
    bool has_snapshot_{false};
    std::uint8_t local_players_{1};
    bool progress_available_{false};
    bool running_{false};
};

}  // namespace grandleon::godot_frontend
