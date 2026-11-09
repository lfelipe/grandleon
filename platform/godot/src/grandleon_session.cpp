// SPDX-License-Identifier: MIT
#include <grandleon/godot/grandleon_session.hpp>

#include <grandleon/client/session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/localization.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/view/motion.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace grandleon::godot_frontend {
namespace {

namespace gd = ::godot;
namespace pf = package_format;
namespace pr = package_runtime;
namespace sim = simulation;

gd::String text(std::string_view value) {
    return gd::String::utf8(value.data(), static_cast<int64_t>(value.size()));
}

std::string narrow(const gd::String& value) {
    const gd::CharString utf8 = value.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

std::int64_t signed_id(std::uint64_t value) {
    return static_cast<std::int64_t>(value);
}

const char* terrain_feature_name(std::uint64_t terrain_id) {
    static const std::uint64_t village_house =
        core::stable_content_id_v1("mountain_village_house");
    static const std::uint64_t ancient_grove =
        core::stable_content_id_v1("mountain_ancient_grove");
    static const std::uint64_t castle_wall =
        core::stable_content_id_v1("mountain_castle_wall");
    static const std::uint64_t castle_door =
        core::stable_content_id_v1("paved_castle_door");
    static const std::uint64_t castle_floor =
        core::stable_content_id_v1("paved_castle_floor");
    static const std::uint64_t castle_dais =
        core::stable_content_id_v1("paved_castle_dais");
    static const std::uint64_t farmland_orchard_tree =
        core::stable_content_id_v1("farmland_orchard_tree");
    static const std::uint64_t forest_orchard_tree =
        core::stable_content_id_v1("forest_orchard_tree");
    static const std::uint64_t orchard_bell =
        core::stable_content_id_v1("paved_orchard_bell");
    static const std::uint64_t watch_ruin =
        core::stable_content_id_v1("ruins_watch_ruin");
    static const std::uint64_t paved_signal_beacon =
        core::stable_content_id_v1("paved_signal_beacon");
    static const std::uint64_t road_signal_beacon =
        core::stable_content_id_v1("road_signal_beacon");
    if (terrain_id == village_house) return "village_house";
    if (terrain_id == ancient_grove) return "ancient_grove";
    if (terrain_id == castle_wall) return "castle_wall";
    if (terrain_id == castle_door) return "castle_door";
    if (terrain_id == castle_floor) return "castle_floor";
    if (terrain_id == castle_dais) return "castle_dais";
    if (terrain_id == farmland_orchard_tree ||
        terrain_id == forest_orchard_tree) return "orchard_tree";
    if (terrain_id == orchard_bell) return "orchard_bell";
    if (terrain_id == watch_ruin) return "watch_ruin";
    if (terrain_id == paved_signal_beacon ||
        terrain_id == road_signal_beacon) return "signal_beacon";
    return "";
}

gd::Dictionary position_dictionary(sim::Position position) {
    gd::Dictionary result;
    result["x"] = position.x;
    result["y"] = position.y;
    return result;
}

gd::Array positions_array(const std::vector<sim::Position>& positions) {
    gd::Array result;
    result.resize(static_cast<int64_t>(positions.size()));
    for (std::size_t index = 0; index < positions.size(); ++index) {
        result[static_cast<int64_t>(index)] =
            position_dictionary(positions[index]);
    }
    return result;
}

bool contains_member(
    const std::vector<campaign::PersistentEntityId>& members,
    campaign::PersistentEntityId member
) {
    return std::find(members.begin(), members.end(), member) != members.end();
}

const char* side_name(sim::Side side) {
    return side == sim::Side::first ? "first" : "second";
}

const char* outcome_name(sim::Outcome outcome) {
    switch (outcome) {
        case sim::Outcome::ongoing: return "ongoing";
        case sim::Outcome::first_side_won: return "first_side_won";
        case sim::Outcome::second_side_won: return "second_side_won";
    }
    return "unknown";
}

const char* objective_kind_name(sim::ObjectiveKind kind) {
    switch (kind) {
        case sim::ObjectiveKind::defeat_all_opponents:
            return "defeat_all_opponents";
        case sim::ObjectiveKind::defeat_target: return "defeat_target";
        case sim::ObjectiveKind::protect_target: return "protect_target";
        case sim::ObjectiveKind::survive_rounds: return "survive_rounds";
    }
    return "unknown";
}

const char* turn_order_name(sim::TurnOrder order) {
    switch (order) {
        case sim::TurnOrder::alternating: return "alternating";
        case sim::TurnOrder::side_blocks: return "side_blocks";
        case sim::TurnOrder::initiative: return "initiative";
    }
    return "alternating";
}

const char* event_name(FrontendEvent::Kind kind) {
    switch (kind) {
        case FrontendEvent::Kind::dialogue: return "dialogue";
        case FrontendEvent::Kind::battle_begins: return "battle_begins";
        case FrontendEvent::Kind::battle_surroundings:
            return "battle_surroundings";
        case FrontendEvent::Kind::battle_visual_visibility:
            return "battle_visual_visibility";
        case FrontendEvent::Kind::definitions: return "definitions";
        case FrontendEvent::Kind::deployment_begins:
            return "deployment_begins";
        case FrontendEvent::Kind::presentation: return "presentation";
        case FrontendEvent::Kind::snapshot: return "snapshot";
        case FrontendEvent::Kind::refused: return "refused";
        case FrontendEvent::Kind::battle_ended: return "battle_ended";
        case FrontendEvent::Kind::campaign_ended: return "campaign_ended";
        case FrontendEvent::Kind::campaign_begun: return "campaign_begun";
        case FrontendEvent::Kind::checkpoint_saved: return "checkpoint_saved";
        case FrontendEvent::Kind::checkpoint_refused:
            return "checkpoint_refused";
        case FrontendEvent::Kind::management: return "management";
        case FrontendEvent::Kind::management_committed:
            return "management_committed";
        case FrontendEvent::Kind::session_finished: return "session_finished";
        case FrontendEvent::Kind::session_failed: return "session_failed";
    }
    return "unknown";
}

std::string hexadecimal(const std::uint8_t* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2U, '0');
    for (std::size_t index = 0; index < size; ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

std::string hexadecimal(std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0; index < 8U; ++index) {
        bytes[7U - index] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
    return hexadecimal(bytes, 8U);
}

std::string slot_failure_name(const client::SlotFailure& failure) {
    if (failure.storage != storage::StorageError::none) {
        return std::string(storage::storage_error_name(failure.storage));
    }
    if (failure.migration != campaign::MigrationError::none) {
        return std::string(campaign::migration_error_name(failure.migration));
    }
    if (failure.save != campaign::SaveError::none) {
        return std::string(campaign::save_error_name(failure.save));
    }
    if (failure.state != campaign::StateError::none) {
        return std::string(campaign::state_error_name(failure.state));
    }
    return failure.wrong_campaign ? "wrong_campaign" : "rejected";
}

const char* presentation_event_name(sim::EventType type) {
    switch (type) {
        case sim::EventType::unit_moved: return "unit_moved";
        case sim::EventType::unit_waited: return "unit_waited";
        case sim::EventType::unit_damaged: return "unit_damaged";
        case sim::EventType::unit_defeated: return "unit_defeated";
        case sim::EventType::encounter_completed:
            return "encounter_completed";
        case sim::EventType::unit_restored: return "unit_restored";
        case sim::EventType::activation_ended: return "activation_ended";
        case sim::EventType::attack_missed: return "attack_missed";
        case sim::EventType::item_used: return "item_used";
        case sim::EventType::item_dropped: return "item_dropped";
        case sim::EventType::unit_deployed: return "unit_deployed";
        case sim::EventType::deployment_ended: return "deployment_ended";
        case sim::EventType::unit_talked: return "unit_talked";
        case sim::EventType::unit_arrived: return "unit_arrived";
        case sim::EventType::unit_endured: return "unit_endured";
    }
    return "unknown";
}

const char* intent_name(client::IntentKind kind) {
    switch (kind) {
        case client::IntentKind::move_to: return "move";
        case client::IntentKind::attack: return "attack";
        case client::IntentKind::ability: return "ability";
        case client::IntentKind::use_item: return "item";
        case client::IntentKind::wait: return "wait";
        case client::IntentKind::deploy_to: return "deploy";
        case client::IntentKind::begin_battle: return "begin";
        case client::IntentKind::talk: return "talk";
        default: return "";
    }
}

sim::ContentId intent_content_id(const client::Intent& intent) {
    switch (intent.kind) {
        case client::IntentKind::attack: return intent.weapon_id;
        case client::IntentKind::ability: return intent.ability_id;
        case client::IntentKind::use_item: return intent.item_id;
        default: return 0;
    }
}

std::optional<MotionPresentation> plan_motion(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    sim::Position destination
) {
    const auto actor = std::find_if(
        snapshot.units.begin(), snapshot.units.end(),
        [unit_id](const sim::UnitSnapshot& unit) {
            return unit.id == unit_id;
        }
    );
    if (actor == snapshot.units.end() || !sim::on_board(*actor)) {
        return std::nullopt;
    }

    const int width = static_cast<int>(snapshot.width);
    const int height = static_cast<int>(snapshot.height);
    if (width <= 0 || height <= 0) return std::nullopt;
    const std::size_t cell_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (cell_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> crossable(cell_count, 0);
    const auto mark = [&crossable, width, height](sim::Position position) {
        if (position.x < 0 || position.y < 0 || position.x >= width ||
            position.y >= height) {
            return;
        }
        crossable[static_cast<std::size_t>(position.y * width + position.x)] = 1;
    };
    for (const sim::Position position : sim::reachable_tiles(snapshot, unit_id)) {
        mark(position);
    }
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == actor->id || unit.side != actor->side ||
            !sim::on_board(unit)) {
            continue;
        }
        mark(unit.position);
    }

    // The shared route stores one-byte wavefront distances, so 254 steps is
    // its longest representable walk. Source maps are at most 256 x 256; a
    // route outside this bound simply retains the renderer's straight fallback.
    constexpr int route_capacity =
        static_cast<int>(view::route_unreached) - 1;
    std::vector<view::RouteTile> storage(
        std::min(cell_count, static_cast<std::size_t>(route_capacity))
    );
    std::vector<std::uint8_t> distance(cell_count);
    view::Route route(storage.data(), static_cast<int>(storage.size()));
    const bool found = view::plan_route(
        actor->position.x, actor->position.y, destination.x, destination.y,
        width, height,
        [&crossable, width](int x, int y) {
            return crossable[static_cast<std::size_t>(y * width + x)] != 0;
        },
        distance.data(), route
    );
    if (!found) return std::nullopt;

    MotionPresentation result;
    result.unit_id = unit_id;
    result.route.reserve(static_cast<std::size_t>(route.size()));
    for (int index = 0; index < route.size(); ++index) {
        result.route.push_back({route[index].x, route[index].y});
    }
    constexpr int refresh_hz = 60;
    result.duration_msec =
        view::slide_frames_for(route.size()) * 1000 / refresh_hz;
    return result;
}

std::optional<MotionPresentation> plan_motion(
    const sim::EncounterSnapshot& snapshot, const client::Intent& intent
) {
    if (intent.kind != client::IntentKind::move_to) return std::nullopt;
    return plan_motion(snapshot, intent.unit_id, intent.destination);
}

}  // namespace

void AsyncPresenter::present_dialogue(const pr::Dialogue& dialogue) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::dialogue;
    event.dialogue = dialogue;
    push(std::move(event));
}

void AsyncPresenter::battle_begins(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster,
    sim::Side player_side,
    const std::vector<std::uint64_t>& terrain
) {
    static_cast<void>(roster);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        battle_active_ = true;
        moments_.clear();
        moment_placements_.clear();
        opening_moments_played_ = false;
    }
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_begins;
    event.snapshot = snapshot;
    event.terrain = terrain;
    event.player_side = player_side;
    push(std::move(event));
}

void AsyncPresenter::battle_route(
    std::uint64_t stage_id,
    std::uint64_t encounter_id,
    std::uint64_t map_id
) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_stage_id_ = stage_id;
    active_encounter_id_ = encounter_id;
    active_map_id_ = map_id;
}

void AsyncPresenter::battle_surroundings(
    const pr::MapSurroundings& surroundings
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_surroundings;
    event.surroundings = surroundings;
    push(std::move(event));
}

void AsyncPresenter::battle_visual_visibility(
    const pr::MapVisualVisibility& visibility
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_visual_visibility;
    event.visual_visibility = visibility;
    push(std::move(event));
}

void AsyncPresenter::battle_definitions(
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities,
    const std::vector<sim::ItemDefinition>& items,
    const std::vector<sim::ObjectiveDefinition>& objectives
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::definitions;
    event.weapons = weapons;
    event.abilities = abilities;
    event.items = items;
    event.objectives = objectives;
    push(std::move(event));
}

void AsyncPresenter::battle_moments(
    const std::vector<pr::EncounterMoment>& moments,
    const std::vector<pr::PlacementIdentity>& placements
) {
    std::lock_guard<std::mutex> lock(mutex_);
    moments_ = moments;
    moment_placements_ = placements;
    opening_moments_played_ = false;
}

void AsyncPresenter::deployment_begins(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster,
    const std::vector<sim::Position>& zone
) {
    static_cast<void>(roster);
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::deployment_begins;
    event.snapshot = snapshot;
    event.zone = zone;
    push(std::move(event));
}

void AsyncPresenter::draw(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster
) {
    static_cast<void>(roster);
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::snapshot;
    event.snapshot = snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.request_id = active_request_id_;
        active_request_id_ = 0;
    }
    push(std::move(event));
    play_moments(pr::MomentTrigger::stage_opens);
}

void AsyncPresenter::report(
    const sim::CommandResult& result,
    const client::Roster& roster
) {
    static_cast<void>(roster);
    FrontendEvent event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.action = active_action_;
        event.action_content_id = active_action_content_id_;
        event.motion = std::move(active_motion_);
        active_action_.clear();
        active_action_content_id_ = 0;
        active_motion_.reset();
    }
    if (result.events.empty()) return;
    event.kind = FrontendEvent::Kind::presentation;
    event.presentation_events = result.events;
    push(std::move(event));
    for (const sim::Event& presented : result.events) {
        if (presented.type == sim::EventType::unit_defeated) {
            play_moments(
                pr::MomentTrigger::character_falls, presented.unit_id
            );
        } else if (presented.type == sim::EventType::unit_talked) {
            play_moments(
                pr::MomentTrigger::character_talked, presented.unit_id
            );
        }
    }
}

void AsyncPresenter::refused(sim::CommandError error) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::refused;
    event.message = std::string(sim::error_name(error));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.request_id = active_request_id_;
        active_request_id_ = 0;
        active_action_.clear();
        active_action_content_id_ = 0;
        active_motion_.reset();
    }
    push(std::move(event));
}

void AsyncPresenter::show_state(
    const sim::EncounterSnapshot& snapshot,
    std::uint64_t canonical_hash,
    const std::vector<sim::ObjectiveDefinition>& objectives
) {
    static_cast<void>(objectives);
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::snapshot;
    event.snapshot = snapshot;
    event.canonical_hash = canonical_hash;
    push(std::move(event));
}

void AsyncPresenter::battle_ended(
    const sim::EncounterSnapshot& snapshot,
    std::uint64_t canonical_hash
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_ended;
    event.snapshot = snapshot;
    event.canonical_hash = canonical_hash;
    push(std::move(event));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        battle_active_ = false;
        moments_.clear();
        moment_placements_.clear();
        opening_moments_played_ = false;
    }
}

void AsyncPresenter::campaign_ended() {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::campaign_ended;
    push(std::move(event));
}

void AsyncPresenter::campaign_begun(
    const std::vector<client::RosterEntry>& roster,
    const std::vector<campaign::InventoryStack>& store,
    std::string_view slot,
    bool resumed
) {
    static_cast<void>(store);
    static_cast<void>(slot);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        campaign_roster_ = roster;
        campaign_board_.reset();
    }
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::campaign_begun;
    event.resumed = resumed;
    push(std::move(event));
}

void AsyncPresenter::slot_refused(const client::SlotFailure& failure) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::checkpoint_refused;
    event.message = slot_failure_name(failure);
    push(std::move(event));
}

void AsyncPresenter::board_prepared(const client::CampaignBoard& board) {
    std::lock_guard<std::mutex> lock(mutex_);
    campaign_board_ = board;
    campaign_roster_ = board.roster;
}

void AsyncPresenter::battle_aftermath(
    const client::BattleAftermath& aftermath
) {
    std::lock_guard<std::mutex> lock(mutex_);
    campaign_roster_ = aftermath.roster;
    campaign_board_.reset();
}

void AsyncPresenter::members_joined(
    const std::vector<client::RosterEntry>& joined
) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const client::RosterEntry& member : joined) {
        const auto present = std::find_if(
            campaign_roster_.begin(), campaign_roster_.end(),
            [&member](const client::RosterEntry& candidate) {
                return candidate.member == member.member;
            }
        );
        if (present == campaign_roster_.end()) {
            campaign_roster_.push_back(member);
        } else {
            *present = member;
        }
    }
}

void AsyncPresenter::campaign_saved(
    std::string_view slot, storage::StorageError error
) {
    static_cast<void>(slot);
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::checkpoint_saved;
    event.successful = error == storage::StorageError::none;
    event.message = std::string(storage::storage_error_name(error));
    push(std::move(event));
}

void AsyncPresenter::management_opened(
    const client::CompanyManagement& company
) {
    static_cast<void>(company);
}

void AsyncPresenter::management_committed(
    const client::ManagementCommit& result
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::management_committed;
    event.successful = static_cast<bool>(result);
    if (result.error != client::ManagementError::none) {
        event.message = std::string(client::management_error_name(result.error));
    } else if (result.application.error != campaign::OutcomeError::none) {
        event.message = std::string(
            campaign::outcome_error_name(result.application.error)
        );
    } else if (result.saved && result.save != storage::StorageError::none) {
        event.message = std::string(storage::storage_error_name(result.save));
    }
    push(std::move(event));
    if (result.saved) {
        FrontendEvent checkpoint;
        checkpoint.kind = FrontendEvent::Kind::checkpoint_saved;
        checkpoint.successful = result.save == storage::StorageError::none;
        checkpoint.message = std::string(
            storage::storage_error_name(result.save)
        );
        push(std::move(checkpoint));
    }
}

client::ManagementIntent AsyncPresenter::next_management_intent(
    const client::CompanyManagement& company
) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return {client::ManagementVerb::quit};
        campaign_roster_ = company.roster;
        management_intent_.reset();
        awaiting_management_ = true;
    }
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::management;
    event.company = company;
    push(std::move(event));

    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] {
        return stopping_ || management_intent_.has_value();
    });
    if (stopping_) {
        awaiting_management_ = false;
        return {client::ManagementVerb::quit};
    }
    const client::ManagementIntent intent = *management_intent_;
    management_intent_.reset();
    awaiting_management_ = false;
    return intent;
}

client::Intent AsyncPresenter::next_intent(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster
) {
    static_cast<void>(roster);
    return wait_for_intent(snapshot);
}

client::Intent AsyncPresenter::next_deployment_intent(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster
) {
    static_cast<void>(roster);
    return wait_for_intent(snapshot);
}

client::Intent AsyncPresenter::wait_for_intent(
    const sim::EncounterSnapshot& snapshot
) {
    // Mark the exact state an input will answer.  A render frame may already
    // carry it, but this event is the synchronization point: Godot can animate
    // as many presentation frames as it likes and submits one intent against
    // this authoritative snapshot.
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::snapshot;
    event.snapshot = snapshot;
    push(std::move(event));

    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] {
        return stopping_ || !intents_.empty();
    });
    if (stopping_) return {client::IntentKind::quit};
    const QueuedIntent queued = intents_.front();
    intents_.pop_front();
    active_request_id_ = queued.request_id;
    active_action_ = intent_name(queued.intent.kind);
    active_action_content_id_ = intent_content_id(queued.intent);
    active_motion_ = plan_motion(snapshot, queued.intent);
    return queued.intent;
}

void AsyncPresenter::submit(client::Intent intent, std::int64_t request_id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        intents_.push_back({intent, request_id});
    }
    condition_.notify_one();
}

bool AsyncPresenter::submit_management(client::ManagementIntent intent) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || !awaiting_management_ ||
            management_intent_.has_value()) {
            return false;
        }
        management_intent_ = std::move(intent);
    }
    condition_.notify_all();
    return true;
}

bool AsyncPresenter::awaiting_management() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return awaiting_management_ && !stopping_;
}

void AsyncPresenter::use_package(const pf::LoadedPackage* package) {
    std::lock_guard<std::mutex> lock(mutex_);
    package_ = package;
}

void AsyncPresenter::play_moments(
    pr::MomentTrigger trigger,
    sim::UnitId unit_id
) {
    std::vector<std::uint64_t> dialogues;
    const pf::LoadedPackage* package = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (trigger == pr::MomentTrigger::stage_opens) {
            if (opening_moments_played_) return;
            opening_moments_played_ = true;
        }
        std::uint64_t placement_id = 0;
        if (unit_id != 0) {
            const auto placement = std::find_if(
                moment_placements_.begin(), moment_placements_.end(),
                [unit_id](const pr::PlacementIdentity& candidate) {
                    return candidate.unit_id == unit_id;
                }
            );
            if (placement == moment_placements_.end()) return;
            placement_id = placement->unit_id;
        }
        for (const pr::EncounterMoment& moment : moments_) {
            if (moment.trigger != trigger) continue;
            if (trigger != pr::MomentTrigger::stage_opens &&
                moment.placement_id != placement_id) {
                continue;
            }
            dialogues.push_back(moment.dialogue_id);
        }
        package = package_;
    }
    if (package == nullptr) return;
    for (const std::uint64_t dialogue_id : dialogues) {
        const pr::DialogueLoadResult loaded =
            pr::load_dialogue(*package, dialogue_id);
        if (!loaded) continue;
        FrontendEvent event;
        event.kind = FrontendEvent::Kind::dialogue;
        event.dialogue = loaded.dialogue;
        event.dialogue_subject_id = unit_id;
        push(std::move(event));
    }
}

void AsyncPresenter::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        intents_.clear();
        management_intent_.reset();
        campaign_roster_.clear();
        campaign_board_.reset();
        active_stage_id_ = 0;
        active_encounter_id_ = 0;
        active_map_id_ = 0;
        moments_.clear();
        moment_placements_.clear();
        battle_active_ = false;
        opening_moments_played_ = false;
        awaiting_management_ = false;
        active_motion_.reset();
    }
    condition_.notify_all();
}

void AsyncPresenter::push(FrontendEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    if (event.kind == FrontendEvent::Kind::dialogue) {
        event.over_board = battle_active_;
    }
    event.campaign_roster = campaign_roster_;
    event.campaign_board = campaign_board_;
    event.stage_id = active_stage_id_;
    event.encounter_id = active_encounter_id_;
    event.map_id = active_map_id_;
    events_.push_back(std::move(event));
}

std::deque<FrontendEvent> AsyncPresenter::take_events() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<FrontendEvent> result;
    result.swap(events_);
    return result;
}

GrandleonSession::GrandleonSession() = default;

GrandleonSession::~GrandleonSession() {
    stop();
}

void GrandleonSession::_bind_methods() {
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "start", "package_path", "campaign_key", "local_players",
            "save_root", "language"
        ),
        &GrandleonSession::start,
        DEFVAL(1), DEFVAL(gd::String()), DEFVAL(gd::String())
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "start_package", "package_bytes", "campaign_key", "local_players",
            "save_root", "language"
        ),
        &GrandleonSession::start_package,
        DEFVAL(1), DEFVAL(gd::String()), DEFVAL(gd::String())
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("package_languages", "package_bytes"),
        &GrandleonSession::package_languages
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("package_presentation", "package_bytes"),
        &GrandleonSession::package_presentation
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("set_language", "language"),
        &GrandleonSession::set_language
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("language"), &GrandleonSession::language
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("localized_state"),
        &GrandleonSession::localized_state
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("localized_dialogue", "dialogue_id"),
        &GrandleonSession::localized_dialogue
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("has_progress"), &GrandleonSession::has_progress
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("checkpoint_slots"), &GrandleonSession::checkpoint_slots
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("checkpoint_save_ready"),
        &GrandleonSession::checkpoint_save_ready
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("save_checkpoint", "slot"),
        &GrandleonSession::save_checkpoint
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("load_checkpoint", "slot"),
        &GrandleonSession::load_checkpoint
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("reset_progress"), &GrandleonSession::reset_progress
    );
    gd::ClassDB::bind_method(gd::D_METHOD("stop"), &GrandleonSession::stop);
    gd::ClassDB::bind_method(
        gd::D_METHOD("running"), &GrandleonSession::running
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("last_error"), &GrandleonSession::last_error
    );
    gd::ClassDB::bind_method(gd::D_METHOD("poll"), &GrandleonSession::poll);
    gd::ClassDB::bind_method(
        gd::D_METHOD("query_tiles", "kind", "unit_id", "content_id"),
        &GrandleonSession::query_tiles,
        DEFVAL(0)
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "submit_intent", "kind", "unit_id", "target_id", "x", "y",
            "content_id", "request_id"
        ),
        &GrandleonSession::submit_intent,
        DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0)
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "submit_management", "kind", "member_id", "item_id", "stage_id"
        ),
        &GrandleonSession::submit_management,
        DEFVAL(0), DEFVAL(0), DEFVAL(0)
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("submit_player_name", "member_id", "name"),
        &GrandleonSession::submit_player_name
    );
}

bool GrandleonSession::start(
    const gd::String& package_path,
    const gd::String& campaign_key,
    std::int64_t local_players,
    const gd::String& save_root,
    const gd::String& language
) {
    reset_for_start();

    const std::string path = narrow(package_path);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "cannot open package: " + path;
        return false;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    return start_bytes(bytes, campaign_key, local_players, save_root, language);
}

bool GrandleonSession::start_package(
    const gd::PackedByteArray& package_bytes,
    const gd::String& campaign_key,
    std::int64_t local_players,
    const gd::String& save_root,
    const gd::String& language
) {
    reset_for_start();
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(package_bytes.size())
    );
    const std::uint8_t* const input = package_bytes.ptr();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = input[index];
    }
    return start_bytes(bytes, campaign_key, local_players, save_root, language);
}

gd::Dictionary GrandleonSession::package_languages(
    const gd::PackedByteArray& package_bytes
) const {
    gd::Dictionary result;
    result["ok"] = false;
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(package_bytes.size())
    );
    const std::uint8_t* const input = package_bytes.ptr();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = input[index];
    }
    auto loaded = pf::load_mock_package(
        bytes, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        result["error"] = text(pf::error_name(loaded.error));
        return result;
    }
    const std::vector<std::string_view> languages =
        pr::available_languages(loaded.package);
    gd::Array values;
    gd::Dictionary titles;
    const std::string_view source_title = pr::content_name(
        loaded.package, pf::SectionType::manifest,
        pf::manifest_title_record_id
    );
    for (const std::string_view language : languages) {
        values.push_back(text(language));
        titles[text(language)] = text(pr::localized_project_title(
            loaded.package, language, source_title
        ));
    }
    result["ok"] = true;
    result["source"] = text(pr::source_language(loaded.package));
    result["title"] = text(source_title);
    result["titles"] = titles;
    result["languages"] = values;
    return result;
}

gd::Dictionary GrandleonSession::package_presentation(
    const gd::PackedByteArray& package_bytes
) const {
    gd::Dictionary result;
    result["ok"] = false;
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(package_bytes.size())
    );
    const std::uint8_t* const input = package_bytes.ptr();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = input[index];
    }
    auto loaded = pf::load_mock_package(
        bytes, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        result["error"] = text(pf::error_name(loaded.error));
        return result;
    }
    const pr::PresentationLoadResult presentation =
        pr::load_presentation(loaded.package);
    if (!presentation) {
        result["error"] = text(pr::error_name(presentation.error));
        return result;
    }
    result["ok"] = true;
    result["battle_closeups"] = presentation.presentation.battle_closeups;
    return result;
}

void GrandleonSession::reset_for_start() {
    stop();
    storage_.reset();
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_.clear();
    has_snapshot_ = false;
    latest_weapons_.clear();
    latest_abilities_.clear();
    latest_snapshot_event_.reset();
    latest_definitions_event_.reset();
    latest_campaign_event_.reset();
    latest_management_event_.reset();
    latest_dialogue_events_.clear();
    progress_available_ = false;
    save_slot_.clear();
}

bool GrandleonSession::set_language(const gd::String& requested) {
    const std::string selected = narrow(requested);
    const std::vector<std::string_view> available =
        pr::available_languages(package_);
    const bool supported = std::find(
        available.begin(), available.end(), selected
    ) != available.end();
    if (!supported) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "game language is not available in this package";
        return false;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    language_ = selected;
    last_error_.clear();
    return true;
}

gd::String GrandleonSession::language() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return text(language_);
}

gd::Dictionary GrandleonSession::localized_state() const {
    std::optional<FrontendEvent> definitions;
    std::optional<FrontendEvent> snapshot;
    std::optional<FrontendEvent> campaign;
    std::optional<FrontendEvent> management;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        definitions = latest_definitions_event_;
        snapshot = latest_snapshot_event_;
        campaign = latest_campaign_event_;
        management = latest_management_event_;
    }
    gd::Dictionary result;
    result["ok"] = true;
    result["language"] = language();
    if (definitions.has_value()) {
        result["definitions"] = event_dictionary(*definitions);
    }
    if (snapshot.has_value()) {
        result["snapshot"] = event_dictionary(*snapshot);
    }
    if (campaign.has_value()) {
        result["campaign"] = event_dictionary(*campaign);
    }
    if (management.has_value()) {
        result["management"] = event_dictionary(*management);
    }
    return result;
}

gd::Dictionary GrandleonSession::localized_dialogue(
    std::int64_t dialogue_id
) const {
    std::optional<FrontendEvent> dialogue;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto current = latest_dialogue_events_.rbegin();
             current != latest_dialogue_events_.rend(); ++current) {
            if (signed_id(current->dialogue.id) == dialogue_id) {
                dialogue = *current;
                break;
            }
        }
    }
    if (!dialogue.has_value()) return {};
    return event_dictionary(*dialogue);
}

bool GrandleonSession::start_bytes(
    const std::vector<std::uint8_t>& package_bytes,
    const gd::String& campaign_key,
    std::int64_t local_players,
    const gd::String& save_root,
    const gd::String& language
) {
    auto loaded = pf::load_mock_package(
        package_bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "package rejected: " +
                      std::string(pf::error_name(loaded.error));
        return false;
    }
    auto presentation = pr::load_presentation(loaded.package);
    if (!presentation) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "presentation rejected: " +
                      std::string(pr::error_name(presentation.error));
        return false;
    }

    package_ = std::move(loaded.package);
    presentation_ = std::move(presentation.presentation);
    campaign_id_ = core::stable_content_id_v1(narrow(campaign_key));
    language_ = narrow(language);
    if (language_.empty()) {
        language_ = std::string(pr::source_language(package_));
    }
    local_players_ = static_cast<std::uint8_t>(
        std::clamp<std::int64_t>(local_players, 1, 2)
    );
    const std::string root = narrow(save_root);
    if (!root.empty()) {
        std::filesystem::path scoped = std::filesystem::u8path(root);
        scoped /= hexadecimal(package_.game_id.data(), package_.game_id.size());
        storage_ = std::make_unique<storage::FilesystemSlotStorage>(
            scoped.u8string()
        );
        if (!storage_->available()) {
            storage_.reset();
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = "campaign save storage is unavailable";
            return false;
        }
        save_slot_ = "campaign-" + hexadecimal(campaign_id_);
    }
    return launch_worker();
}

bool GrandleonSession::launch_worker() {
    presenter_ = std::make_shared<AsyncPresenter>();
    presenter_->use_package(&package_);
    const bool resume = storage_ && storage_->contains(save_slot_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = true;
        progress_available_ = resume;
    }
    const std::shared_ptr<AsyncPresenter> presenter = presenter_;
    storage::FilesystemSlotStorage* const device = storage_.get();
    const std::uint64_t campaign_id = campaign_id_;
    const std::uint8_t players = local_players_;
    const std::string slot = save_slot_;
    worker_ = std::thread([
        this, presenter, device, campaign_id, players, slot, resume
    ] {
        FrontendEvent event;
        bool succeeded = false;
        if (device != nullptr) {
            client::CampaignSessionOptions options;
            options.slot = slot;
            options.resume = resume;
            options.player_side = sim::Side::first;
            options.local_players = players;
            const client::CampaignSessionError status =
                client::run_persistent_campaign(
                    package_, campaign_id, *presenter, *presenter, *device,
                    options
                );
            succeeded = status == client::CampaignSessionError::none;
            if (!succeeded) {
                event.message = std::string(
                    client::campaign_session_error_name(status)
                );
            }
        } else {
            const client::SessionOptions options{players};
            const client::SessionError status = client::run_campaign(
                package_, campaign_id, sim::Side::first, *presenter, options
            );
            succeeded = status == client::SessionError::none;
            if (!succeeded) {
                event.message = std::string(client::error_name(status));
            }
        }
        if (succeeded) {
            event.kind = FrontendEvent::Kind::session_finished;
        } else {
            event.kind = FrontendEvent::Kind::session_failed;
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = event.message;
        }
        presenter->push(std::move(event));
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = false;
    });
    return true;
}

bool GrandleonSession::has_progress() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return progress_available_;
}

std::string GrandleonSession::manual_slot_name(std::int64_t slot) const {
    if (slot < 1 || slot > 3) return {};
    return "slot-" + hexadecimal(campaign_id_) + "-" + std::to_string(slot);
}

gd::Array GrandleonSession::checkpoint_slots() const {
    gd::Array slots;
    slots.resize(3);
    for (std::int64_t index = 1; index <= 3; ++index) {
        const std::string name = manual_slot_name(index);
        gd::Dictionary entry;
        entry["index"] = index;
        entry["exists"] = storage_ && storage_->contains(name);
        entry["active"] = name == save_slot_;
        slots[index - 1] = entry;
    }
    return slots;
}

bool GrandleonSession::checkpoint_save_ready() const {
    return storage_ && presenter_ && presenter_->awaiting_management() &&
           storage_->contains(save_slot_);
}

bool GrandleonSession::save_checkpoint(std::int64_t slot) {
    const std::string destination = manual_slot_name(slot);
    if (destination.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "checkpoint slot must be between 1 and 3";
        return false;
    }
    if (!checkpoint_save_ready()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "checkpoint saves are available between battles";
        return false;
    }
    const storage::StorageRead source = storage_->read(save_slot_);
    if (!source) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "current campaign checkpoint could not be read";
        return false;
    }
    const storage::StorageError written = storage_->write(
        destination, source.bytes
    );
    if (written != storage::StorageError::none) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "manual checkpoint could not be saved";
        return false;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_.clear();
    return true;
}

bool GrandleonSession::load_checkpoint(std::int64_t slot) {
    const std::string source = manual_slot_name(slot);
    if (source.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "checkpoint slot must be between 1 and 3";
        return false;
    }
    if (!storage_ || !storage_->contains(source)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "checkpoint slot is empty";
        return false;
    }
    stop();
    save_slot_ = source;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_.clear();
        has_snapshot_ = false;
        latest_weapons_.clear();
        latest_abilities_.clear();
        latest_snapshot_event_.reset();
        latest_definitions_event_.reset();
        latest_campaign_event_.reset();
        latest_management_event_.reset();
        latest_dialogue_events_.clear();
        progress_available_ = true;
    }
    return launch_worker();
}

bool GrandleonSession::reset_progress() {
    stop();
    if (!storage_) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "campaign progress is disabled";
        return false;
    }
    save_slot_ = "campaign-" + hexadecimal(campaign_id_);
    const storage::StorageError erased = storage_->erase(save_slot_);
    if (erased != storage::StorageError::none &&
        erased != storage::StorageError::not_found) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "campaign progress could not be reset";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_.clear();
        has_snapshot_ = false;
        latest_weapons_.clear();
        latest_abilities_.clear();
        latest_snapshot_event_.reset();
        latest_definitions_event_.reset();
        latest_campaign_event_.reset();
        latest_management_event_.reset();
        latest_dialogue_events_.clear();
        progress_available_ = false;
    }
    return launch_worker();
}

void GrandleonSession::join_worker() {
    if (worker_.joinable()) worker_.join();
}

void GrandleonSession::stop() {
    if (presenter_) presenter_->stop();
    join_worker();
    presenter_.reset();
    std::lock_guard<std::mutex> lock(state_mutex_);
    running_ = false;
}

bool GrandleonSession::running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return running_;
}

gd::String GrandleonSession::last_error() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return text(last_error_);
}

gd::Array GrandleonSession::poll() {
    gd::Array result;
    if (!presenter_) return result;
    std::deque<FrontendEvent> events = presenter_->take_events();
    result.resize(static_cast<int64_t>(events.size()));
    for (std::size_t index = 0; index < events.size(); ++index) {
        const FrontendEvent& event = events[index];
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (event.kind == FrontendEvent::Kind::battle_begins ||
                event.kind == FrontendEvent::Kind::deployment_begins ||
                event.kind == FrontendEvent::Kind::snapshot ||
                event.kind == FrontendEvent::Kind::battle_ended) {
                latest_snapshot_ = event.snapshot;
                latest_snapshot_event_ = event;
                has_snapshot_ = true;
            }
            if (event.kind == FrontendEvent::Kind::definitions) {
                latest_weapons_ = event.weapons;
                latest_abilities_ = event.abilities;
                latest_definitions_event_ = event;
            }
            if (event.kind == FrontendEvent::Kind::dialogue) {
                latest_dialogue_events_.erase(
                    std::remove_if(
                        latest_dialogue_events_.begin(),
                        latest_dialogue_events_.end(),
                        [&event](const FrontendEvent& previous) {
                            return previous.dialogue.id == event.dialogue.id;
                        }
                    ),
                    latest_dialogue_events_.end()
                );
                latest_dialogue_events_.push_back(event);
            }
            if (event.kind == FrontendEvent::Kind::campaign_begun) {
                latest_campaign_event_ = event;
            }
            if (event.kind == FrontendEvent::Kind::management ||
                event.kind == FrontendEvent::Kind::management_committed) {
                latest_management_event_ = event;
            }
            if (event.kind == FrontendEvent::Kind::campaign_begun) {
                progress_available_ = event.resumed;
            }
            if (event.kind == FrontendEvent::Kind::checkpoint_saved) {
                progress_available_ = event.successful;
            }
            if (event.kind == FrontendEvent::Kind::checkpoint_refused) {
                progress_available_ = false;
            }
        }
        result[static_cast<int64_t>(index)] = event_dictionary(events[index]);
    }
    return result;
}

gd::Array GrandleonSession::query_tiles(
    const gd::String& kind_value,
    std::int64_t unit_id_value,
    std::int64_t content_id_value
) const {
    sim::EncounterSnapshot snapshot;
    std::vector<sim::WeaponDefinition> weapons;
    std::vector<sim::AbilityDefinition> abilities;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!has_snapshot_) return {};
        snapshot = latest_snapshot_;
        weapons = latest_weapons_;
        abilities = latest_abilities_;
    }
    const std::string kind = narrow(kind_value);
    const sim::UnitId unit_id = static_cast<sim::UnitId>(unit_id_value);
    const sim::ContentId content_id =
        static_cast<sim::ContentId>(content_id_value);
    std::vector<sim::Position> positions;
    if (kind == "deploy") {
        positions = sim::deployable_tiles(snapshot, unit_id);
    } else if (kind == "move") {
        positions = sim::reachable_tiles(snapshot, unit_id);
    } else if (kind == "attack") {
        positions = sim::aimable_tiles(
            snapshot, unit_id, {sim::Gesture::strike, content_id, 0}, weapons,
            abilities
        );
    } else if (kind == "ability") {
        positions = sim::aimable_tiles(
            snapshot, unit_id, {sim::Gesture::cast, 0, content_id}, weapons,
            abilities
        );
    } else if (kind == "talk") {
        positions = sim::aimable_tiles(
            snapshot, unit_id, {sim::Gesture::talk, 0, 0}, weapons, abilities
        );
    }
    return positions_array(positions);
}

void GrandleonSession::submit_intent(
    const gd::String& kind_value,
    std::int64_t unit_id,
    std::int64_t target_id,
    std::int64_t x,
    std::int64_t y,
    std::int64_t content_id,
    std::int64_t request_id
) {
    if (!presenter_) return;
    const std::string kind = narrow(kind_value);
    client::Intent intent;
    if (kind == "move") {
        intent.kind = client::IntentKind::move_to;
    } else if (kind == "attack") {
        intent.kind = client::IntentKind::attack;
        intent.weapon_id = static_cast<std::uint64_t>(content_id);
    } else if (kind == "ability") {
        intent.kind = client::IntentKind::ability;
        intent.ability_id = static_cast<std::uint64_t>(content_id);
    } else if (kind == "item") {
        intent.kind = client::IntentKind::use_item;
        intent.item_id = static_cast<std::uint64_t>(content_id);
    } else if (kind == "talk") {
        intent.kind = client::IntentKind::talk;
    } else if (kind == "deploy") {
        intent.kind = client::IntentKind::deploy_to;
    } else if (kind == "begin") {
        intent.kind = client::IntentKind::begin_battle;
    } else if (kind == "state") {
        intent.kind = client::IntentKind::show_state;
    } else if (kind == "quit") {
        intent.kind = client::IntentKind::quit;
    } else {
        intent.kind = client::IntentKind::wait;
    }
    intent.unit_id = static_cast<std::uint64_t>(unit_id);
    intent.target_id = static_cast<std::uint64_t>(target_id);
    intent.destination = {
        static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)
    };
    presenter_->submit(intent, request_id);
}

bool GrandleonSession::submit_management(
    const gd::String& kind_value,
    std::int64_t member_id,
    std::int64_t item_id,
    std::int64_t stage_id
) {
    if (!presenter_) return false;
    const std::string kind = narrow(kind_value);
    client::ManagementIntent intent;
    if (kind == "give") {
        intent.verb = client::ManagementVerb::give;
    } else if (kind == "take") {
        intent.verb = client::ManagementVerb::take;
    } else if (kind == "field") {
        intent.verb = client::ManagementVerb::field;
    } else if (kind == "bench") {
        intent.verb = client::ManagementVerb::bench;
    } else if (kind == "proceed") {
        intent.verb = client::ManagementVerb::proceed;
    } else if (kind == "jump") {
        intent.verb = client::ManagementVerb::jump;
    } else {
        return false;
    }
    intent.member = campaign::PersistentEntityId{
        static_cast<std::uint64_t>(member_id)
    };
    intent.item = campaign::DefinitionRef{
        package_.game_id,
        core::ContentCategory::item,
        static_cast<std::uint64_t>(item_id)
    };
    intent.stage = static_cast<std::uint64_t>(stage_id);
    return presenter_->submit_management(std::move(intent));
}

bool GrandleonSession::submit_player_name(
    std::int64_t member_id,
    const gd::String& name
) {
    if (!presenter_) return false;
    client::ManagementIntent intent;
    intent.verb = client::ManagementVerb::rename;
    intent.member = campaign::PersistentEntityId{
        static_cast<std::uint64_t>(member_id)
    };
    intent.name = narrow(name);
    return presenter_->submit_management(std::move(intent));
}

std::vector<client::RosterEntry> GrandleonSession::localized_roster(
    const std::vector<client::RosterEntry>& roster
) const {
    std::vector<client::RosterEntry> result = roster;
    for (client::RosterEntry& member : result) {
        if (member.has_player_name) continue;
        const std::string_view authored = member.authored_name.empty()
                                              ? std::string_view{member.name}
                                              : std::string_view{
                                                    member.authored_name
                                                };
        member.name = pr::localized_campaign_member_name(
            package_, language_, campaign_id_, member.placement_source_key,
            authored
        );
    }
    return result;
}

gd::Dictionary GrandleonSession::snapshot_dictionary(
    const sim::EncounterSnapshot& snapshot,
    const std::vector<client::RosterEntry>& roster,
    const std::optional<client::CampaignBoard>& board
) const {
    const std::vector<client::RosterEntry> display_roster =
        localized_roster(roster);
    gd::Dictionary result;
    result["width"] = snapshot.width;
    result["height"] = snapshot.height;
    result["active_side"] = side_name(snapshot.active_side);
    result["active_unit_id"] = signed_id(snapshot.active_unit_id);
    result["remaining_action_points"] = snapshot.remaining_action_points;
    result["round"] = static_cast<std::int64_t>(snapshot.round);
    result["activation_count"] = signed_id(snapshot.activation_count);
    result["outcome"] = outcome_name(snapshot.outcome);
    result["turn_order"] = turn_order_name(snapshot.turn_order);
    result["deploying"] = snapshot.deploying;

    gd::Array units;
    units.resize(static_cast<int64_t>(snapshot.units.size()));
    for (std::size_t index = 0; index < snapshot.units.size(); ++index) {
        const sim::UnitSnapshot& unit = snapshot.units[index];
        gd::Dictionary row;
        row["id"] = signed_id(unit.id);
        row["unit_type_id"] = signed_id(unit.unit_type_id);
        row["side"] = side_name(unit.side);
        row["player"] = unit.local_player;
        row["x"] = unit.position.x;
        row["y"] = unit.position.y;
        row["health"] = unit.health;
        row["maximum_health"] = unit.maximum_health;
        row["movement"] = unit.movement;
        row["action_points"] = unit.action_points;
        row["spent_action_points"] = unit.spent_action_points;
        row["has_acted"] = unit.has_acted;
        row["has_moved"] = unit.has_moved;
        row["departed"] = unit.departed;
        row["arrived"] = unit.arrived;
        row["on_board"] = sim::on_board(unit);
        row["minimum_reach"] = unit.minimum_reach;
        row["maximum_reach"] = unit.maximum_reach;
        const char* member_name =
            board.has_value()
                ? client::member_name_on_board(
                      board->binding, display_roster, unit.id
                  )
                : nullptr;
        if (member_name != nullptr) {
            row["name"] = text(member_name);
            const campaign::PersistentEntityId persistent =
                board->binding.persistent_of(
                    campaign::BattleEntityId{unit.id}
                );
            row["member_id"] = signed_id(persistent.value);
        } else {
            const std::string_view placement_name = pr::content_name(
                package_, pf::SectionType::placement_names, unit.id
            );
            if (!placement_name.empty()) {
                row["name"] = text(pr::localized_placement_name(
                    package_, language_, unit.id, placement_name
                ));
            } else {
                const std::string_view name = pr::content_name(
                    package_, pf::SectionType::unit_types, unit.unit_type_id
                );
                row["name"] = name.empty()
                                  ? gd::String("Unit")
                                  : text(pr::localized_content_name(
                                        package_, language_,
                                        pf::SectionType::unit_types,
                                        unit.unit_type_id, name
                                    ));
            }
        }
        row["colour"] = presentation_.colour_of_unit_type(unit.unit_type_id);
        row["archetype"] =
            presentation_.archetype_of_unit_type(unit.unit_type_id);
        row["style"] = presentation_.character_style_of_unit_type(
            unit.unit_type_id
        );
        row["figure"] = presentation_.character_figure_of_unit_type(
            unit.unit_type_id
        );

        gd::Array abilities;
        for (const std::uint64_t id : unit.ability_ids) {
            abilities.push_back(signed_id(id));
        }
        row["abilities"] = abilities;
        gd::Array weapons;
        for (const std::uint64_t id : unit.weapon_ids) {
            weapons.push_back(signed_id(id));
        }
        row["weapons"] = weapons;
        gd::Array items;
        for (std::size_t item = 0; item < unit.item_ids.size(); ++item) {
            gd::Dictionary carried;
            carried["id"] = signed_id(unit.item_ids[item]);
            carried["count"] = unit.item_counts[item];
            items.push_back(carried);
        }
        row["items"] = items;
        units[static_cast<int64_t>(index)] = row;
    }
    result["units"] = units;
    result["deployment_tiles"] = positions_array(snapshot.deployment_tiles);
    return result;
}

gd::Dictionary GrandleonSession::management_dictionary(
    const client::CompanyManagement& company
) const {
    const auto inventory = [this](
        const std::vector<campaign::InventoryStack>& stacks
    ) {
        gd::Array result;
        for (const campaign::InventoryStack& stack : stacks) {
            gd::Dictionary value;
            value["id"] = signed_id(stack.item.stable_id);
            const std::string_view name = pr::content_name(
                package_, pf::SectionType::items, stack.item.stable_id
            );
            value["name"] = name.empty()
                                ? gd::String("Item")
                                : text(pr::localized_content_name(
                                      package_, language_,
                                      pf::SectionType::items,
                                      stack.item.stable_id, name
                                  ));
            value["count"] = static_cast<std::int64_t>(stack.quantity);
            result.push_back(value);
        }
        return result;
    };

    gd::Dictionary result;
    result["node_id"] = signed_id(company.node.stable_id);
    result["encounter_id"] = signed_id(company.encounter_id);
    const std::string_view stage_name = pr::content_name(
        package_, pf::SectionType::encounters, company.encounter_id
    );
    result["stage_name"] = text(pr::localized_campaign_node_name(
        package_, language_, campaign_id_, company.node.stable_id,
        stage_name
    ));
    result["capacity"] = company.capacity;
    result["has_deployment_options"] = company.has_deployment_options;
    result["fielded_count"] = static_cast<std::int64_t>(
        company.fielded.size()
    );
    result["refused"] = text(
        campaign_runtime::roster_error_name(company.refused)
    );
    result["store"] = inventory(company.store);

    gd::Array stages;
    for (const client::CampaignStage& stage : company.stages) {
        gd::Dictionary value;
        value["node_id"] = signed_id(stage.node_id);
        value["encounter_id"] = signed_id(stage.encounter_id);
        value["name"] = text(pr::localized_campaign_node_name(
            package_, language_, campaign_id_, stage.node_id, stage.name
        ));
        value["reached"] = stage.reached;
        value["standing"] = stage.standing;
        stages.push_back(value);
    }
    result["stages"] = stages;

    gd::Array roster;
    for (const client::RosterEntry& member : company.roster) {
        gd::Dictionary value;
        value["member_id"] = signed_id(member.member.value);
        const std::string_view authored = member.authored_name.empty()
                                              ? std::string_view{member.name}
                                              : std::string_view{
                                                    member.authored_name
                                                };
        const std::string_view localized_default =
            pr::localized_campaign_member_name(
                package_, language_, campaign_id_,
                member.placement_source_key, authored
            );
        value["authored_name"] = text(authored);
        value["default_name"] = text(localized_default);
        value["name"] = member.has_player_name
                            ? text(member.name)
                            : text(localized_default);
        value["player_nameable"] = member.player_nameable;
        value["has_player_name"] = member.has_player_name;
        value["availability"] = text(
            campaign::availability_name(member.availability)
        );
        value["level"] = member.progression.level;
        value["experience"] = static_cast<std::int64_t>(
            member.progression.experience
        );
        value["placeable"] = contains_member(company.placeable, member.member);
        value["fielded"] = contains_member(company.fielded, member.member);
        value["carried"] = inventory(member.carried);
        roster.push_back(value);
    }
    result["roster"] = roster;
    return result;
}

gd::Dictionary GrandleonSession::event_dictionary(
    const FrontendEvent& event
) const {
    gd::Dictionary result;
    result["type"] = event_name(event.kind);
    if (!event.message.empty()) result["message"] = text(event.message);
    if (event.request_id != 0) result["request_id"] = event.request_id;

    switch (event.kind) {
        case FrontendEvent::Kind::dialogue: {
            const std::vector<client::RosterEntry> display_roster =
                localized_roster(event.campaign_roster);
            result["id"] = signed_id(event.dialogue.id);
            result["name"] = text(pr::localized_dialogue_name(
                package_, language_, event.dialogue.id, event.dialogue.name
            ));
            result["backdrop"] = event.dialogue.backdrop;
            // Captured when the presenter queues the dialogue, so a creator
            // preview without persistent campaign state gets the same exact
            // on-map answer as an ordinary saved campaign.
            result["over_board"] = event.over_board;
            result["subject_unit_id"] = signed_id(
                event.dialogue_subject_id
            );
            gd::Array lines;
            for (std::size_t line_index = 0;
                 line_index < event.dialogue.lines.size(); ++line_index) {
                const pr::DialogueLine& line = event.dialogue.lines[line_index];
                gd::Dictionary value;
                const std::string_view localized_speaker =
                    pr::localized_dialogue_line(
                        package_, language_, event.dialogue.id, line_index,
                        "speaker", line.speaker
                    );
                const std::string_view localized_line =
                    pr::localized_dialogue_line(
                        package_, language_, event.dialogue.id, line_index,
                        "text", line.text
                    );
                value["speaker"] = text(
                    client::substitute_campaign_member_names(
                        localized_speaker, display_roster
                    )
                );
                value["text"] = text(
                    client::substitute_campaign_member_names(
                        localized_line, display_roster
                    )
                );
                value["cast_entry"] = line.cast_entry;
                if (const std::uint64_t* unit_type =
                        event.dialogue.speaker_unit_type(line)) {
                    value["unit_type_id"] = signed_id(*unit_type);
                    value["colour"] =
                        presentation_.colour_of_unit_type(*unit_type);
                    value["archetype"] =
                        presentation_.archetype_of_unit_type(*unit_type);
                    value["style"] =
                        presentation_.character_style_of_unit_type(*unit_type);
                    value["figure"] =
                        presentation_.character_figure_of_unit_type(*unit_type);
                }
                lines.push_back(value);
            }
            result["lines"] = lines;
            break;
        }
        case FrontendEvent::Kind::battle_begins: {
            result["snapshot"] = snapshot_dictionary(
                event.snapshot, event.campaign_roster, event.campaign_board
            );
            result["player_side"] = side_name(event.player_side);
            result["theme"] = presentation_.theme;
            gd::Array terrain;
            gd::Array terrain_features;
            for (const std::uint64_t id : event.terrain) {
                terrain.push_back(
                    presentation_.kind_of_terrain(id)
                );
                terrain_features.push_back(terrain_feature_name(id));
            }
            result["terrain"] = terrain;
            result["terrain_features"] = terrain_features;
            if (event.stage_id != 0) {
                result["stage_id"] = text(hexadecimal(event.stage_id));
            }
            if (event.encounter_id != 0) {
                result["encounter_id"] =
                    text(hexadecimal(event.encounter_id));
            }
            if (event.map_id != 0) {
                result["map_id"] = text(hexadecimal(event.map_id));
            }
            break;
        }
        case FrontendEvent::Kind::battle_surroundings: {
            result["mode"] = event.surroundings.mode ==
                                     pr::MapSurroundingMode::solid
                                 ? "solid"
                             : event.surroundings.mode ==
                                       pr::MapSurroundingMode::automatic
                                 ? "automatic"
                             : event.surroundings.mode ==
                                       pr::MapSurroundingMode::manual
                                 ? "manual"
                             : event.surroundings.mode ==
                                       pr::MapSurroundingMode::fill
                                 ? "fill"
                             : event.surroundings.mode ==
                                       pr::MapSurroundingMode::blend
                                 ? "blend"
                                 : "none";
            result["colour"] = static_cast<std::int64_t>(
                event.surroundings.colour
            );
            result["depth"] = event.surroundings.depth;
            gd::Array terrain;
            for (const std::uint64_t id : event.surroundings.terrain) {
                terrain.push_back(presentation_.kind_of_terrain(id));
            }
            result["terrain"] = terrain;
            break;
        }
        case FrontendEvent::Kind::battle_visual_visibility: {
            result["reveal_radius"] = event.visual_visibility.reveal_radius;
            gd::Array cells;
            for (const std::uint32_t cell :
                 event.visual_visibility.hidden_cells) {
                cells.push_back(static_cast<std::int64_t>(cell));
            }
            result["hidden_cells"] = cells;
            break;
        }
        case FrontendEvent::Kind::definitions: {
            gd::Array weapons;
            for (const sim::WeaponDefinition& weapon : event.weapons) {
                gd::Dictionary value;
                value["id"] = signed_id(weapon.id);
                const std::string_view name = pr::content_name(
                    package_, pf::SectionType::weapons, weapon.id
                );
                value["name"] = text(pr::localized_content_name(
                    package_, language_, pf::SectionType::weapons,
                    weapon.id, name
                ));
                value["power"] = weapon.power;
                value["minimum_reach"] = weapon.minimum_reach;
                value["maximum_reach"] = weapon.maximum_reach;
                value["accuracy"] = weapon.accuracy;
                weapons.push_back(value);
            }
            result["weapons"] = weapons;
            gd::Array abilities;
            for (const sim::AbilityDefinition& ability : event.abilities) {
                gd::Dictionary value;
                value["id"] = signed_id(ability.id);
                const std::string_view name = pr::content_name(
                    package_, pf::SectionType::abilities, ability.id
                );
                value["name"] = text(pr::localized_content_name(
                    package_, language_, pf::SectionType::abilities,
                    ability.id, name
                ));
                value["kind"] = ability.kind == sim::AbilityKind::damage
                                      ? "damage"
                                      : "restore";
                value["damage_type"] = ability.damage_type == sim::DamageType::magical
                                           ? "magical" : "physical";
                value["power"] = ability.power;
                value["minimum_reach"] = ability.minimum_reach;
                value["maximum_reach"] = ability.maximum_reach;
                value["radius"] = ability.radius;
                value["accuracy"] = ability.accuracy;
                abilities.push_back(value);
            }
            result["abilities"] = abilities;
            gd::Array items;
            for (const sim::ItemDefinition& item : event.items) {
                gd::Dictionary value;
                value["id"] = signed_id(item.id);
                const std::string_view name = pr::content_name(
                    package_, pf::SectionType::items, item.id
                );
                value["name"] = text(pr::localized_content_name(
                    package_, language_, pf::SectionType::items,
                    item.id, name
                ));
                value["kind"] = item.kind == sim::ItemKind::restore
                                      ? "restore"
                                      : "none";
                value["power"] = item.power;
                items.push_back(value);
            }
            result["items"] = items;
            const std::vector<client::RosterEntry> display_roster =
                localized_roster(event.campaign_roster);
            gd::Array objectives;
            for (const sim::ObjectiveDefinition& objective : event.objectives) {
                gd::Dictionary value;
                value["id"] = signed_id(objective.id);
                value["kind"] = objective_kind_name(objective.kind);
                value["side"] = side_name(objective.side);
                value["target_unit_id"] = signed_id(objective.target_unit_id);
                value["round_count"] = static_cast<std::int64_t>(
                    objective.round_count
                );
                const std::string_view source_name = pr::content_name(
                    package_, pf::SectionType::objectives, objective.id
                );
                const std::string_view localized_name =
                    pr::localized_objective_name(
                        package_, language_, objective.id, source_name
                    );
                value["name"] = text(
                    client::substitute_campaign_member_names(
                        localized_name, display_roster
                    )
                );
                if (objective.target_unit_id != 0U &&
                    event.campaign_board.has_value()) {
                    const char* target_name = client::member_name_on_board(
                        event.campaign_board->binding, display_roster,
                        objective.target_unit_id
                    );
                    if (target_name != nullptr) {
                        value["target_name"] = text(target_name);
                        const campaign::PersistentEntityId persistent =
                            event.campaign_board->binding.persistent_of(
                                campaign::BattleEntityId{
                                    objective.target_unit_id
                                }
                            );
                        value["target_member_id"] = signed_id(persistent.value);
                    } else {
                        const std::string_view placement_name =
                            pr::content_name(
                                package_, pf::SectionType::placement_names,
                                objective.target_unit_id
                            );
                        if (!placement_name.empty()) {
                            value["target_name"] = text(
                                pr::localized_placement_name(
                                    package_, language_,
                                    objective.target_unit_id, placement_name
                                )
                            );
                        }
                    }
                }
                objectives.push_back(value);
            }
            result["objectives"] = objectives;
            break;
        }
        case FrontendEvent::Kind::deployment_begins:
            result["snapshot"] = snapshot_dictionary(
                event.snapshot, event.campaign_roster, event.campaign_board
            );
            result["zone"] = positions_array(event.zone);
            break;
        case FrontendEvent::Kind::presentation: {
            result["action"] = text(event.action);
            result["action_content_id"] = signed_id(event.action_content_id);
            gd::Array values;
            for (const sim::Event& source : event.presentation_events) {
                gd::Dictionary value;
                value["kind"] = presentation_event_name(source.type);
                value["unit_id"] = signed_id(source.unit_id);
                value["related_unit_id"] =
                    signed_id(source.related_unit_id);
                value["x"] = source.position.x;
                value["y"] = source.position.y;
                value["amount"] = source.amount;
                value["content_id"] = signed_id(source.content_id);
                value["outcome"] = outcome_name(source.outcome);
                if (source.type == sim::EventType::unit_moved &&
                    event.motion.has_value() &&
                    event.motion->unit_id == source.unit_id &&
                    !event.motion->route.empty() &&
                    event.motion->route.back().x == source.position.x &&
                    event.motion->route.back().y == source.position.y) {
                    value["route"] = positions_array(event.motion->route);
                    value["duration_msec"] = event.motion->duration_msec;
                }
                values.push_back(value);
            }
            result["events"] = values;
            break;
        }
        case FrontendEvent::Kind::snapshot:
        case FrontendEvent::Kind::battle_ended:
            result["snapshot"] = snapshot_dictionary(
                event.snapshot, event.campaign_roster, event.campaign_board
            );
            if (event.canonical_hash != 0) {
                result["canonical_hash"] = signed_id(event.canonical_hash);
            }
            break;
        case FrontendEvent::Kind::refused:
        case FrontendEvent::Kind::campaign_ended: {
            break;
        }
        case FrontendEvent::Kind::campaign_begun: {
            result["resumed"] = event.resumed;
            const std::string_view name = pr::content_name(
                package_, pf::SectionType::campaigns, campaign_id_
            );
            result["campaign_name"] = text(pr::localized_campaign_name(
                package_, language_, campaign_id_, name
            ));
            break;
        }
        case FrontendEvent::Kind::checkpoint_saved:
            result["successful"] = event.successful;
            break;
        case FrontendEvent::Kind::checkpoint_refused:
            break;
        case FrontendEvent::Kind::management:
            result["company"] = management_dictionary(event.company);
            break;
        case FrontendEvent::Kind::management_committed:
            result["successful"] = event.successful;
            break;
        case FrontendEvent::Kind::session_finished:
        case FrontendEvent::Kind::session_failed:
            break;
    }
    return result;
}

}  // namespace grandleon::godot_frontend
