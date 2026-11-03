// SPDX-License-Identifier: MIT
#include <grandleon/godot/grandleon_session.hpp>

#include <grandleon/client/session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/view/motion.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
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
        case FrontendEvent::Kind::battle_surroundings:
            return "battle_surroundings";
        case FrontendEvent::Kind::definitions: return "definitions";
        case FrontendEvent::Kind::deployment_begins:
            return "deployment_begins";
        case FrontendEvent::Kind::presentation: return "presentation";
        case FrontendEvent::Kind::snapshot: return "snapshot";
        case FrontendEvent::Kind::refused: return "refused";
        case FrontendEvent::Kind::battle_ended: return "battle_ended";
        case FrontendEvent::Kind::campaign_ended: return "campaign_ended";
        case FrontendEvent::Kind::session_finished: return "session_finished";
        case FrontendEvent::Kind::session_failed: return "session_failed";
    }
    return "unknown";
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
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_begins;
    event.snapshot = snapshot;
    event.terrain = terrain;
    event.player_side = player_side;
    push(std::move(event));
}

void AsyncPresenter::battle_surroundings(
    const pr::MapSurroundings& surroundings
) {
    FrontendEvent event;
    event.kind = FrontendEvent::Kind::battle_surroundings;
    event.surroundings = surroundings;
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

void AsyncPresenter::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        intents_.clear();
        active_motion_.reset();
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
        gd::D_METHOD(
            "start", "package_path", "campaign_key", "local_players"
        ),
        &GrandleonSession::start,
        DEFVAL(1)
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "start_package", "package_bytes", "campaign_key", "local_players"
        ),
        &GrandleonSession::start_package,
        DEFVAL(1)
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
}

bool GrandleonSession::start(
    const gd::String& package_path,
    const gd::String& campaign_key,
    std::int64_t local_players
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
    return start_bytes(bytes, campaign_key, local_players);
}

bool GrandleonSession::start_package(
    const gd::PackedByteArray& package_bytes,
    const gd::String& campaign_key,
    std::int64_t local_players
) {
    reset_for_start();
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(package_bytes.size())
    );
    const std::uint8_t* const input = package_bytes.ptr();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = input[index];
    }
    return start_bytes(bytes, campaign_key, local_players);
}

void GrandleonSession::reset_for_start() {
    stop();
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_.clear();
    has_snapshot_ = false;
    latest_weapons_.clear();
    latest_abilities_.clear();
}

bool GrandleonSession::start_bytes(
    const std::vector<std::uint8_t>& package_bytes,
    const gd::String& campaign_key,
    std::int64_t local_players
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
    presenter_ = std::make_shared<AsyncPresenter>();
    const std::uint64_t campaign_id =
        core::stable_content_id_v1(narrow(campaign_key));
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = true;
    }
    const std::shared_ptr<AsyncPresenter> presenter = presenter_;
    const std::uint8_t players = static_cast<std::uint8_t>(
        std::clamp<std::int64_t>(local_players, 1, 2)
    );
    worker_ = std::thread([this, presenter, campaign_id, players] {
        const client::SessionOptions options{players};
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
                event.kind == FrontendEvent::Kind::battle_ended) {
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
            result["snapshot"] = snapshot_dictionary(event.snapshot);
            if (event.canonical_hash != 0) {
                result["canonical_hash"] = signed_id(event.canonical_hash);
            }
            break;
        case FrontendEvent::Kind::refused:
        case FrontendEvent::Kind::campaign_ended: {
            break;
        }
        case FrontendEvent::Kind::session_finished:
        case FrontendEvent::Kind::session_failed:
            break;
    }
    return result;
}

}  // namespace grandleon::godot_frontend
