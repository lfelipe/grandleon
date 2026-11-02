// SPDX-License-Identifier: MIT
#include <grandleon/godot/grandleon_session.hpp>

#include <grandleon/client/session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/names.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
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
        case FrontendEvent::Kind::definitions: return "definitions";
        case FrontendEvent::Kind::deployment_begins:
            return "deployment_begins";
        case FrontendEvent::Kind::snapshot: return "snapshot";
        case FrontendEvent::Kind::refused: return "refused";
        case FrontendEvent::Kind::battle_ended: return "battle_ended";
        case FrontendEvent::Kind::campaign_ended: return "campaign_ended";
        case FrontendEvent::Kind::ai_turn: return "ai_turn";
        case FrontendEvent::Kind::session_finished: return "session_finished";
        case FrontendEvent::Kind::session_failed: return "session_failed";
    }
    return "unknown";
}

const char* command_name(sim::CommandType type) {
    switch (type) {
        case sim::CommandType::move: return "move";
        case sim::CommandType::attack: return "attack";
        case sim::CommandType::ability: return "ability";
        case sim::CommandType::use_item: return "item";
        case sim::CommandType::wait: return "wait";
        case sim::CommandType::deploy: return "deploy";
        case sim::CommandType::begin_battle: return "begin";
        case sim::CommandType::talk: return "talk";
    }
    return "wait";
}

gd::Dictionary command_dictionary(const sim::Command& command) {
    gd::Dictionary result;
    result["kind"] = command_name(command.type);
    result["unit_id"] = signed_id(command.unit_id);
    result["target_id"] = signed_id(command.target_id);
    result["x"] = command.destination.x;
    result["y"] = command.destination.y;
    result["ability_id"] = signed_id(command.ability_id);
    result["weapon_id"] = signed_id(command.weapon_id);
    result["item_id"] = signed_id(command.item_id);
    return result;
}

bool same_command(const sim::Command& lhs, const sim::Command& rhs) {
    return lhs.type == rhs.type && lhs.unit_id == rhs.unit_id &&
           lhs.destination == rhs.destination && lhs.target_id == rhs.target_id &&
           lhs.ability_id == rhs.ability_id && lhs.weapon_id == rhs.weapon_id &&
           lhs.item_id == rhs.item_id;
}

void append_candidate(
    std::vector<sim::Command>& candidates, const sim::Command& command
) {
    if (candidates.size() >= 96U) return;
    if (std::none_of(
            candidates.begin(), candidates.end(),
            [&command](const sim::Command& value) {
                return same_command(value, command);
            }
        )) {
        candidates.push_back(command);
    }
}

std::vector<sim::Command> legal_candidates(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    const sim::Command& fallback,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities
) {
    std::vector<sim::Command> result;
    append_candidate(result, fallback);
    append_candidate(result, {sim::CommandType::wait, unit_id});
    for (const sim::Position position : sim::reachable_tiles(snapshot, unit_id)) {
        sim::Command move{sim::CommandType::move, unit_id};
        move.destination = position;
        append_candidate(result, move);
    }

    const sim::UnitSnapshot* actor = nullptr;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == unit_id) actor = &unit;
    }
    if (actor == nullptr) return result;

    std::vector<sim::ContentId> weapon_ids{0};
    if (actor->weapon_ids.size() > 1U) {
        weapon_ids.insert(
            weapon_ids.end(), actor->weapon_ids.begin() + 1,
            actor->weapon_ids.end()
        );
    }
    for (const sim::UnitSnapshot& target : snapshot.units) {
        if (!sim::on_board(target) || target.id == unit_id) continue;
        for (const sim::ContentId weapon_id : weapon_ids) {
            const auto forecast = sim::forecast_attack(
                snapshot, unit_id, target.id, weapons, weapon_id
            );
            if (!forecast) continue;
            sim::Command attack{sim::CommandType::attack, unit_id};
            attack.target_id = target.id;
            attack.weapon_id = weapon_id;
            append_candidate(result, attack);
        }
        if (sim::forecast_talk(snapshot, unit_id, target.id)) {
            sim::Command talk{sim::CommandType::talk, unit_id};
            talk.target_id = target.id;
            append_candidate(result, talk);
        }
    }
    for (const sim::ContentId ability_id : actor->ability_ids) {
        const auto tiles = sim::aimable_tiles(
            snapshot, unit_id, {sim::Gesture::cast, 0, ability_id}, weapons,
            abilities
        );
        for (const sim::Position position : tiles) {
            sim::Command ability{sim::CommandType::ability, unit_id};
            ability.destination = position;
            ability.ability_id = ability_id;
            append_candidate(result, ability);
        }
    }
    return result;
}

}  // namespace

AsyncPresenter::AsyncPresenter(bool model_tactics_enabled)
    : model_tactics_enabled_(model_tactics_enabled) {}

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
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_begins;
    event.snapshot = snapshot;
    event.terrain = terrain;
    event.player_side = player_side;
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
}

void AsyncPresenter::report(
    const sim::CommandResult& result,
    const client::Roster& roster
) {
    static_cast<void>(result);
    static_cast<void>(roster);
    // The session draws immediately after every accepted command.  Publishing
    // that resulting snapshot is one complete event and avoids a transient
    // frame in which the report and board disagree.
}

void AsyncPresenter::refused(sim::CommandError error) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::refused;
    event.message = std::string(sim::error_name(error));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.request_id = active_request_id_;
        active_request_id_ = 0;
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
}

void AsyncPresenter::campaign_ended() {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::campaign_ended;
    push(std::move(event));
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

std::optional<sim::Command> AsyncPresenter::choose_command(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    const sim::Command& fallback,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities
) {
    if (!model_tactics_enabled_) return std::nullopt;
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::ai_turn;
    event.snapshot = snapshot;
    event.candidates = legal_candidates(
        snapshot, unit_id, fallback, weapons, abilities
    );
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return std::nullopt;
        event.request_id = next_ai_request_id_++;
        awaiting_ai_request_id_ = event.request_id;
        ai_choice_ = -1;
    }
    push(event);

    std::unique_lock<std::mutex> lock(mutex_);
    const bool answered = condition_.wait_for(
        lock, std::chrono::seconds(12), [this] {
            return stopping_ || ai_choice_ >= 0;
        }
    );
    if (!answered || stopping_ || ai_choice_ < 0 ||
        static_cast<std::size_t>(ai_choice_) >= event.candidates.size()) {
        awaiting_ai_request_id_ = 0;
        return std::nullopt;
    }
    const sim::Command selected =
        event.candidates[static_cast<std::size_t>(ai_choice_)];
    awaiting_ai_request_id_ = 0;
    ai_choice_ = -1;
    return selected;
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

void AsyncPresenter::submit_ai_choice(
    std::int64_t request_id, std::int64_t choice
) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || request_id != awaiting_ai_request_id_) return;
        ai_choice_ = choice;
    }
    condition_.notify_all();
}

void AsyncPresenter::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        intents_.clear();
    }
    condition_.notify_all();
}

void AsyncPresenter::push(FrontendEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stopping_) events_.push_back(std::move(event));
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
        gd::D_METHOD("start", "package_path", "campaign_key", "model_tactics"),
        &GrandleonSession::start,
        DEFVAL(false)
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
        gd::D_METHOD("submit_ai_choice", "request_id", "choice"),
        &GrandleonSession::submit_ai_choice
    );
}

bool GrandleonSession::start(
    const gd::String& package_path,
    const gd::String& campaign_key,
    bool model_tactics
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_.clear();
        has_snapshot_ = false;
        latest_weapons_.clear();
        latest_abilities_.clear();
    }

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
    auto loaded = pf::load_mock_package(
        bytes, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
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
    presenter_ = std::make_shared<AsyncPresenter>(model_tactics);
    const std::uint64_t campaign_id =
        core::stable_content_id_v1(narrow(campaign_key));
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = true;
    }
    const std::shared_ptr<AsyncPresenter> presenter = presenter_;
    worker_ = std::thread([this, presenter, campaign_id, model_tactics] {
        const client::SessionOptions options{
            model_tactics ? presenter.get() : nullptr
        };
        const client::SessionError status = client::run_campaign(
            package_, campaign_id, sim::Side::first, *presenter, options
        );
        FrontendEvent event;
        if (status == client::SessionError::none) {
            event.kind = FrontendEvent::Kind::session_finished;
        } else {
            event.kind = FrontendEvent::Kind::session_failed;
            event.message = std::string(client::error_name(status));
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = event.message;
        }
        presenter->push(std::move(event));
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = false;
    });
    return true;
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
                event.kind == FrontendEvent::Kind::battle_ended ||
                event.kind == FrontendEvent::Kind::ai_turn) {
                latest_snapshot_ = event.snapshot;
                has_snapshot_ = true;
            }
            if (event.kind == FrontendEvent::Kind::definitions) {
                latest_weapons_ = event.weapons;
                latest_abilities_ = event.abilities;
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

void GrandleonSession::submit_ai_choice(
    std::int64_t request_id, std::int64_t choice
) {
    if (presenter_) presenter_->submit_ai_choice(request_id, choice);
}

gd::Dictionary GrandleonSession::snapshot_dictionary(
    const sim::EncounterSnapshot& snapshot
) const {
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
        const std::string_view name = pr::content_name(
            package_, pf::SectionType::unit_types, unit.unit_type_id
        );
        row["name"] = name.empty() ? gd::String("Unit") : text(name);
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

gd::Dictionary GrandleonSession::event_dictionary(
    const FrontendEvent& event
) const {
    gd::Dictionary result;
    result["type"] = event_name(event.kind);
    if (!event.message.empty()) result["message"] = text(event.message);
    if (event.request_id != 0) result["request_id"] = event.request_id;

    switch (event.kind) {
        case FrontendEvent::Kind::dialogue: {
            result["id"] = signed_id(event.dialogue.id);
            result["name"] = text(event.dialogue.name);
            result["backdrop"] = event.dialogue.backdrop;
            gd::Array lines;
            for (const pr::DialogueLine& line : event.dialogue.lines) {
                gd::Dictionary value;
                value["speaker"] = text(line.speaker);
                value["text"] = text(line.text);
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
            result["snapshot"] = snapshot_dictionary(event.snapshot);
            result["player_side"] = side_name(event.player_side);
            result["theme"] = presentation_.theme;
            gd::Array terrain;
            for (const std::uint64_t id : event.terrain) {
                terrain.push_back(
                    presentation_.kind_of_terrain(id)
                );
            }
            result["terrain"] = terrain;
            break;
        }
        case FrontendEvent::Kind::definitions: {
            gd::Array weapons;
            for (const sim::WeaponDefinition& weapon : event.weapons) {
                gd::Dictionary value;
                value["id"] = signed_id(weapon.id);
                value["name"] = text(pr::content_name(
                    package_, pf::SectionType::weapons, weapon.id
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
                value["name"] = text(pr::content_name(
                    package_, pf::SectionType::abilities, ability.id
                ));
                value["kind"] = ability.kind == sim::AbilityKind::damage
                                      ? "damage"
                                      : "restore";
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
                value["name"] = text(pr::content_name(
                    package_, pf::SectionType::items, item.id
                ));
                value["kind"] = item.kind == sim::ItemKind::restore
                                      ? "restore"
                                      : "none";
                value["power"] = item.power;
                items.push_back(value);
            }
            result["items"] = items;
            break;
        }
        case FrontendEvent::Kind::deployment_begins:
            result["snapshot"] = snapshot_dictionary(event.snapshot);
            result["zone"] = positions_array(event.zone);
            break;
        case FrontendEvent::Kind::snapshot:
        case FrontendEvent::Kind::battle_ended:
            result["snapshot"] = snapshot_dictionary(event.snapshot);
            if (event.canonical_hash != 0) {
                result["canonical_hash"] = signed_id(event.canonical_hash);
            }
            break;
        case FrontendEvent::Kind::refused:
        case FrontendEvent::Kind::campaign_ended:
        case FrontendEvent::Kind::ai_turn: {
            if (event.kind != FrontendEvent::Kind::ai_turn) break;
            result["snapshot"] = snapshot_dictionary(event.snapshot);
            gd::Array candidates;
            for (const sim::Command& command : event.candidates) {
                candidates.push_back(command_dictionary(command));
            }
            result["candidates"] = candidates;
            break;
        }
        case FrontendEvent::Kind::session_finished:
        case FrontendEvent::Kind::session_failed:
            break;
    }
    return result;
}

}  // namespace grandleon::godot_frontend
