// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/client/session.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/presentation.hpp>

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

struct FrontendEvent final {
    enum class Kind : std::uint8_t {
        dialogue,
        battle_begins,
        definitions,
        deployment_begins,
        presentation,
        snapshot,
        refused,
        battle_ended,
        campaign_ended,
        session_finished,
        session_failed,
    };

    Kind kind{Kind::snapshot};
    simulation::EncounterSnapshot snapshot;
    std::vector<std::uint64_t> terrain;
    std::vector<simulation::Position> zone;
    std::vector<simulation::WeaponDefinition> weapons;
    std::vector<simulation::AbilityDefinition> abilities;
    std::vector<simulation::ItemDefinition> items;
    std::vector<simulation::ObjectiveDefinition> objectives;
    std::vector<simulation::Event> presentation_events;
    std::string action;
    simulation::ContentId action_content_id{};
    package_runtime::Dialogue dialogue;
    simulation::Side player_side{simulation::Side::first};
    std::uint64_t canonical_hash{};
    std::int64_t request_id{};
    std::string message;
};

class AsyncPresenter final : public client::Presenter {
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
    [[nodiscard]] client::Intent next_intent(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override;
    [[nodiscard]] client::Intent next_deployment_intent(
        const simulation::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override;
    void submit(client::Intent intent, std::int64_t request_id);
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
    bool stopping_{false};
};

class GrandleonSession final : public ::godot::RefCounted {
    GDCLASS(GrandleonSession, ::godot::RefCounted)

public:
    GrandleonSession();
    ~GrandleonSession() override;

    [[nodiscard]] bool start(
        const ::godot::String& package_path,
        const ::godot::String& campaign_key
    );
    [[nodiscard]] bool start_package(
        const ::godot::PackedByteArray& package_bytes,
        const ::godot::String& campaign_key,
        bool model_tactics = false
    );
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
protected:
    static void _bind_methods();

private:
    [[nodiscard]] ::godot::Dictionary event_dictionary(
        const FrontendEvent& event
    ) const;
    [[nodiscard]] ::godot::Dictionary snapshot_dictionary(
        const simulation::EncounterSnapshot& snapshot
    ) const;
    void reset_for_start();
    [[nodiscard]] bool start_bytes(
        const std::vector<std::uint8_t>& package_bytes,
        const ::godot::String& campaign_key,
        bool model_tactics
    );
    void join_worker();

    std::shared_ptr<AsyncPresenter> presenter_;
    std::thread worker_;
    package_format::LoadedPackage package_;
    package_runtime::Presentation presentation_;
    mutable std::mutex state_mutex_;
    std::string last_error_;
    simulation::EncounterSnapshot latest_snapshot_;
    std::vector<simulation::WeaponDefinition> latest_weapons_;
    std::vector<simulation::AbilityDefinition> latest_abilities_;
    bool has_snapshot_{false};
    bool running_{false};
};

}  // namespace grandleon::godot_frontend
