// SPDX-License-Identifier: MIT
#include <grandleon/client/session.hpp>

#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/tactics/policy.hpp>

#include <string>
#include <utility>
#include <vector>

namespace grandleon::client {
namespace {

namespace pr = package_runtime;
namespace sim = simulation;

// Presents a node's whole dialogue sequence, in authored order. A cutscene is
// a story node with several dialogues; the presenter sees one at a time.
void present_if_present(
    const package_format::LoadedPackage& package,
    const std::vector<std::uint64_t>& dialogue_ids,
    Presenter& presenter
) {
    for (const std::uint64_t dialogue_id : dialogue_ids) {
        if (dialogue_id == 0) continue;
        const auto loaded = pr::load_dialogue(package, dialogue_id);
        if (!loaded) continue;
        presenter.present_dialogue(loaded.dialogue);
    }
}

// Runs the side nobody is steering until control returns to the player.
//
// Bounded rather than looped until done: a behaviour that cannot act would
// otherwise spin, and a battle that will not hand the turn back is a bug worth
// surfacing as a stuck board rather than a hang.
void play_opposing_side(
    sim::Encounter& encounter,
    const std::vector<pr::UnitBehaviorBinding>& behaviors,
    sim::Side player_side,
    const Roster& roster,
    Presenter& presenter,
    std::vector<sim::Event>& log
) {
    for (int guard = 0; guard < 256; ++guard) {
        const auto snapshot = encounter.snapshot();
        if (snapshot.outcome != sim::Outcome::ongoing) return;
        if (snapshot.active_side == player_side) return;

        bool acted = false;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.side != snapshot.active_side || !sim::on_board(unit)) {
                continue;
            }
            // Somebody who has already had their turn this round is not a
            // candidate. It matters under `side_blocks`, where the engine names
            // the side and leaves the choice open: without this the loop would
            // keep proposing the block's first character, be refused by name,
            // and walk the whole roster to find the next one on every single
            // activation.
            if (unit.has_acted) continue;
            // And whoever holds an activation holds it alone, under the two
            // orders that hand one out. Inert under `side_blocks`, which names
            // no actor and commits to nobody: there a character that has walked
            // and not yet struck comes back round to this loop, and the policy
            // answers it with the strike or the wait its walk was for.
            if (snapshot.active_unit_id != 0 &&
                snapshot.active_unit_id != unit.id) {
                continue;
            }
            tactics::Behavior behavior = tactics::Behavior::hold;
            std::vector<sim::Position> patrol;
            for (const pr::UnitBehaviorBinding& binding : behaviors) {
                if (binding.unit_id != unit.id) continue;
                behavior = binding.behavior;
                patrol = binding.patrol;
            }
            const auto plan = tactics::decide(
                snapshot, unit.id, behavior, patrol, encounter.abilities(),
                encounter.weapons()
            );
            if (!plan.actionable) continue;
            auto result = encounter.apply(plan.command);
            if (!result) {
                // Behaviour is policy; the engine is the authority. A refused
                // proposal falls back to waiting rather than stalling.
                result = encounter.apply(
                    {sim::CommandType::wait, unit.id, {}, 0, 0}
                );
            }
            if (result) {
                log.insert(log.end(), result.events.begin(), result.events.end());
                presenter.report(result, roster);
                // And the board the command produced, on exactly the terms
                // every other accepted command in this file is drawn on: a
                // report animates the event out of the state it happened in,
                // and the draw that follows is what puts the tokens where the
                // engine now says they stand. Without it a front end animates a
                // walk and then repaints the board it last drew, so the
                // opposing side's characters snap back to where they started
                // and stay there until the player's own next command draws the
                // current state.
                presenter.draw(encounter.snapshot(), roster);
                acted = true;
                break;
            }
        }
        if (!acted) return;
    }
}

}  // namespace

sim::UnitId unfinished_unit(
    const sim::EncounterSnapshot& snapshot, sim::Side side
) noexcept {
    if (snapshot.outcome != sim::Outcome::ongoing) return 0;
    // Arranging is not a turn: no side is acting and no activation is open, so
    // there is nothing for a side to have left over.
    if (snapshot.deploying) return 0;
    if (snapshot.active_side != side) return 0;
    // An activation already open belongs to whoever opened it, and the engine
    // refuses every other character while it is. Naming anybody else would be
    // asking for a refusal this function exists to avoid.
    if (snapshot.active_unit_id != 0) {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id != snapshot.active_unit_id) continue;
            if (unit.side != side || !sim::on_board(unit) || unit.has_acted) {
                return 0;
            }
            return unit.id;
        }
        return 0;
    }
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side != side || !sim::on_board(unit) || unit.has_acted) {
            continue;
        }
        return unit.id;
    }
    return 0;
}

namespace {

// The character this snapshot carries, or null.
const sim::UnitSnapshot* find_unit(
    const sim::EncounterSnapshot& snapshot, sim::UnitId unit_id
) noexcept {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == unit_id) return &unit;
    }
    return nullptr;
}

// Whether this character could aim that gesture at anything at all. The engine
// answers; all this adds is the emptiness test, which is the whole question.
bool gesture_lands_somewhere(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    const sim::AimedGesture& gesture,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities
) {
    return !sim::aimable_tiles(snapshot, unit_id, gesture, weapons, abilities)
                .empty();
}

// Whether a cast of `ability_id` could be aimed somewhere it would change
// somebody. `aimable_tiles` gives the band, `area_tiles` gives what a centre
// covers, and the only judgement here is the one `apply`'s own loop makes about
// each covered character: a restoring cast passes over anybody who is not short
// of health, and a damaging one passes over the caster's own side, whose health
// it cannot move.
//
// **That second clause has to be the engine's clause and not a near miss of
// it.** A damaging cast counted by coverage alone would keep a character busy
// on a board where the only thing its spell can reach is a fellow it can no
// longer hurt, and the player would be handed a turn with nothing in it. The
// other way round is just as wrong: count only opponents for a restoring cast
// as well and a medic standing beside a wounded ally would be told its turn
// was over.
//
// The band is walked rather than the roster because the band is the shorter of
// the two on every board this ships, and because walking it is the only order
// that can stop at the first tile that answers.
bool cast_would_change_anybody(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    sim::ContentId ability_id,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities
) {
    const sim::AbilityDefinition* record = nullptr;
    for (const sim::AbilityDefinition& candidate : abilities) {
        if (candidate.id == ability_id) record = &candidate;
    }
    // An identity the registry cannot resolve is refused by the engine too, so
    // there is nothing here for the character to do with it.
    if (record == nullptr) return false;
    const sim::UnitSnapshot* caster = find_unit(snapshot, unit_id);
    if (caster == nullptr) return false;
    const std::vector<sim::Position> band = sim::aimable_tiles(
        snapshot, unit_id, {sim::Gesture::cast, 0, ability_id}, weapons,
        abilities
    );
    for (const sim::Position& centre : band) {
        // `area_tiles` is empty for a single-tile ability, because a splash of
        // one tile is the tile already named. That is its documented answer
        // rather than a failure, so the centre stands in for it.
        std::vector<sim::Position> covered =
            sim::area_tiles(snapshot, ability_id, centre, abilities);
        if (covered.empty()) covered.push_back(centre);
        for (const sim::Position& tile : covered) {
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (!sim::on_board(unit)) continue;
                if (!(unit.position == tile)) continue;
                if (record->kind != sim::AbilityKind::restore) {
                    if (unit.side != caster->side) return true;
                    continue;
                }
                if (unit.health < unit.maximum_health) return true;
            }
        }
    }
    return false;
}

}  // namespace

bool nothing_left_to_do(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities,
    const std::vector<sim::ItemDefinition>& items
) {
    if (snapshot.outcome != sim::Outcome::ongoing) return false;
    // Arranging is not a turn, so there is no turn of anybody's to close.
    if (snapshot.deploying) return false;
    const sim::UnitSnapshot* actor = find_unit(snapshot, unit_id);
    if (actor == nullptr) return false;
    if (!sim::on_board(*actor)) return false;
    // Already finished: the turn this would end is over.
    if (actor->has_acted) return false;
    if (actor->side != snapshot.active_side) return false;
    // Somebody else's open activation locks this character out, and the engine
    // refuses every command it could send. That is not the same fact as having
    // nothing to do, and answering it here would end a turn the player never
    // had.
    if (snapshot.active_unit_id != 0 && snapshot.active_unit_id != unit_id) {
        return false;
    }

    if (gesture_lands_somewhere(
            snapshot, unit_id, {sim::Gesture::walk, 0, 0}, weapons, abilities
        )) {
        return false;
    }
    // The weapon in hand is named by naming nothing, exactly as an attack
    // command names it, and every other weapon the character carries is named
    // in turn. `weapon_ids[0]` *is* the weapon in hand, so the loop starts past
    // it rather than asking the same question twice.
    if (gesture_lands_somewhere(
            snapshot, unit_id, {sim::Gesture::strike, 0, 0}, weapons, abilities
        )) {
        return false;
    }
    for (std::size_t i = 1; i < actor->weapon_ids.size(); ++i) {
        if (gesture_lands_somewhere(
                snapshot, unit_id,
                {sim::Gesture::strike, actor->weapon_ids[i], 0}, weapons,
                abilities
            )) {
            return false;
        }
    }
    if (gesture_lands_somewhere(
            snapshot, unit_id, {sim::Gesture::talk, 0, 0}, weapons, abilities
        )) {
        return false;
    }
    for (const sim::ContentId ability_id : actor->ability_ids) {
        if (cast_would_change_anybody(
                snapshot, unit_id, ability_id, weapons, abilities
            )) {
            return false;
        }
    }
    for (const sim::ContentId item_id : actor->item_ids) {
        // Zero is the acting character, which is the only hand an item reaches.
        const sim::ItemForecast forecast =
            sim::forecast_item(snapshot, unit_id, 0, items, item_id);
        if (!forecast) continue;
        // A restore that restores nothing is the one accepted use the engine
        // itself prices at zero. Anything else the engine accepts is something
        // the character can do, whatever a client might think of it.
        if (forecast.kind != sim::ItemKind::restore || forecast.restored > 0) {
            return false;
        }
    }
    return true;
}

namespace {

// How far apart two tiles are, on the engine's own orthogonal metric.
int steps_between(sim::Position lhs, sim::Position rhs) noexcept {
    const int dx = lhs.x > rhs.x ? lhs.x - rhs.x : rhs.x - lhs.x;
    const int dy = lhs.y > rhs.y ? lhs.y - rhs.y : rhs.y - lhs.y;
    return dx + dy;
}

}  // namespace

sim::Position nearest_aim_tile(
    const std::vector<sim::Position>& tiles, sim::Position from
) noexcept {
    const sim::Position* best = nullptr;
    int best_distance = 0;
    for (const sim::Position& tile : tiles) {
        const int distance = steps_between(tile, from);
        // Strictly nearer, so an equally near tile later in the list loses and
        // the engine's row-major order is the tie-break without being restated.
        if (best == nullptr || distance < best_distance) {
            best = &tile;
            best_distance = distance;
        }
    }
    return best == nullptr ? from : *best;
}

sim::Position next_aim_tile(
    const std::vector<sim::Position>& tiles,
    sim::Position from,
    int dx,
    int dy
) noexcept {
    // Exactly one axis, which is what a d-pad press is. A diagonal or a
    // no-press moves nothing rather than being guessed at.
    if ((dx == 0) == (dy == 0)) return from;
    const int step = dx != 0 ? dx : dy;
    const sim::Position* ahead = nullptr;
    int ahead_along = 0;
    int ahead_across = 0;
    const sim::Position* behind = nullptr;
    int behind_along = 0;
    int behind_across = 0;
    for (const sim::Position& tile : tiles) {
        const int along =
            (dx != 0 ? tile.x - from.x : tile.y - from.y) * step;
        const int offset = dx != 0 ? tile.y - from.y : tile.x - from.x;
        const int across = offset < 0 ? -offset : offset;
        if (along > 0) {
            if (ahead == nullptr || along < ahead_along ||
                (along == ahead_along && across < ahead_across)) {
                ahead = &tile;
                ahead_along = along;
                ahead_across = across;
            }
        } else if (along < 0) {
            // The wrap candidate is the one furthest the other way, so that
            // running off one end comes back on at the other.
            const int back = -along;
            if (behind == nullptr || back > behind_along ||
                (back == behind_along && across < behind_across)) {
                behind = &tile;
                behind_along = back;
                behind_across = across;
            }
        }
    }
    if (ahead != nullptr) return *ahead;
    if (behind != nullptr) return *behind;
    return from;
}

std::string_view error_name(SessionError error) noexcept {
    switch (error) {
        case SessionError::none: return "none";
        case SessionError::package_rejected: return "package_rejected";
        case SessionError::campaign_rejected: return "campaign_rejected";
        case SessionError::encounter_rejected: return "encounter_rejected";
        case SessionError::flow_stalled: return "flow_stalled";
    }
    return "unknown";
}

SessionError play_battle(
    const pr::EncounterLoadResult& loaded,
    sim::Side player_side,
    Presenter& presenter,
    BattleReport& report
) {
    auto created = sim::create_encounter(loaded.definition);
    if (!created) return SessionError::encounter_rejected;

    Roster roster;
    roster.rebuild(created.encounter.snapshot());
    presenter.battle_begins(
        created.encounter.snapshot(), roster, player_side, loaded.terrain
    );
    presenter.battle_definitions(
        created.encounter.weapons(), created.encounter.abilities(),
        created.encounter.items(), created.encounter.objectives()
    );
    // The phase before the first activation, where there is one. It is run
    // here rather than inside the battle loop because it is not a turn: no side
    // is acting, no activation is open, and the opposing side has nothing to do
    // until the player says the battle has begun.
    //
    // A player who does not command the first side is not asked. There is no
    // second-side region to arrange, and the board they open on is the board
    // the content authored. That is what every surface that renders without
    // steering opens on.
    if (created.encounter.snapshot().deploying) {
        if (player_side == sim::Side::first) {
            presenter.deployment_begins(
                created.encounter.snapshot(), roster,
                created.encounter.snapshot().deployment_tiles
            );
            presenter.draw(created.encounter.snapshot(), roster);
        }
        while (created.encounter.snapshot().deploying) {
            const auto snapshot = created.encounter.snapshot();
            const Intent intent = player_side == sim::Side::first
                                      ? presenter.next_deployment_intent(
                                            snapshot, roster
                                        )
                                      : Intent{IntentKind::begin_battle};
            if (intent.kind == IntentKind::quit) {
                report.quit = true;
                report.final_snapshot = created.encounter.snapshot();
                report.canonical_hash = created.encounter.canonical_hash();
                return SessionError::none;
            }
            if (intent.kind == IntentKind::show_state) {
                presenter.show_state(
                    snapshot,
                    created.encounter.canonical_hash(),
                    created.encounter.objectives()
                );
                continue;
            }
            if (intent.kind == IntentKind::redraw) {
                presenter.draw(snapshot, roster);
                continue;
            }
            if (intent.kind != IntentKind::deploy_to &&
                intent.kind != IntentKind::begin_battle) {
                continue;
            }
            sim::Command command{};
            command.unit_id = intent.unit_id;
            if (intent.kind == IntentKind::deploy_to) {
                command.type = sim::CommandType::deploy;
                command.destination = intent.destination;
            } else {
                command.type = sim::CommandType::begin_battle;
            }
            const auto result = created.encounter.apply(command);
            if (!result) {
                presenter.refused(result.error);
                continue;
            }
            report.events.insert(
                report.events.end(), result.events.begin(), result.events.end()
            );
            presenter.report(result, roster);
            presenter.draw(created.encounter.snapshot(), roster);
        }
    }

    presenter.draw(created.encounter.snapshot(), roster);

    while (true) {
        auto snapshot = created.encounter.snapshot();
        if (snapshot.outcome != sim::Outcome::ongoing) break;

        if (snapshot.active_side != player_side) {
            play_opposing_side(
                created.encounter, loaded.behaviors, player_side, roster,
                presenter, report.events
            );
            snapshot = created.encounter.snapshot();
            if (snapshot.outcome != sim::Outcome::ongoing) break;
            // No draw here. Every command the opposing side had accepted was
            // drawn where it was reported, so the last of them is already the
            // board control comes back to; drawing again would be the same
            // frame twice, and a front end that names a frame by whose turn it
            // followed would name the second one the player's.
            continue;
        }

        const Intent intent = presenter.next_intent(snapshot, roster);
        switch (intent.kind) {
            case IntentKind::quit:
                report.quit = true;
                report.final_snapshot = created.encounter.snapshot();
                report.canonical_hash = created.encounter.canonical_hash();
                return SessionError::none;
            case IntentKind::show_state:
                presenter.show_state(
                    snapshot,
                    created.encounter.canonical_hash(),
                    created.encounter.objectives()
                );
                continue;
            case IntentKind::none:
            case IntentKind::help:
            case IntentKind::list_units:
                continue;
            case IntentKind::redraw:
                presenter.draw(snapshot, roster);
                continue;
            default:
                break;
        }

        sim::Command command{};
        command.unit_id = intent.unit_id;
        if (intent.kind == IntentKind::move_to) {
            command.type = sim::CommandType::move;
            command.destination = intent.destination;
        } else if (intent.kind == IntentKind::attack) {
            command.type = sim::CommandType::attack;
            command.target_id = intent.target_id;
            command.weapon_id = intent.weapon_id;
        } else if (intent.kind == IntentKind::ability) {
            command.type = sim::CommandType::ability;
            command.destination = intent.destination;
            command.ability_id = intent.ability_id;
        } else if (intent.kind == IntentKind::use_item) {
            command.type = sim::CommandType::use_item;
            command.item_id = intent.item_id;
        } else if (intent.kind == IntentKind::talk) {
            command.type = sim::CommandType::talk;
            command.target_id = intent.target_id;
        } else {
            command.type = sim::CommandType::wait;
        }

        const auto result = created.encounter.apply(command);
        if (!result) {
            presenter.refused(result.error);
            continue;
        }
        report.events.insert(
            report.events.end(), result.events.begin(), result.events.end()
        );
        presenter.report(result, roster);
        presenter.draw(created.encounter.snapshot(), roster);
    }

    report.final_snapshot = created.encounter.snapshot();
    report.canonical_hash = created.encounter.canonical_hash();
    report.outcome = report.final_snapshot.outcome;
    report.objectives = report.final_snapshot.objectives;
    presenter.battle_ended(report.final_snapshot, report.canonical_hash);
    return SessionError::none;
}

void present_dialogue_sequence(
    const package_format::LoadedPackage& package,
    const std::vector<std::uint64_t>& dialogue_ids,
    Presenter& presenter
) {
    present_if_present(package, dialogue_ids, presenter);
}

SessionError run_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    sim::Side player_side,
    Presenter& presenter
) {
    auto campaign = pr::load_campaign(package, campaign_id);
    if (!campaign) return SessionError::campaign_rejected;
    pr::CampaignCursor cursor(std::move(campaign.definition));

    for (int guard = 0; guard < 256; ++guard) {
        const pr::CampaignNode& node = cursor.current();
        if (node.kind == pr::CampaignNodeKind::terminal) {
            present_if_present(package, node.dialogue_ids, presenter);
            presenter.campaign_ended();
            return SessionError::none;
        }
        if (node.kind == pr::CampaignNodeKind::story) {
            present_if_present(package, node.dialogue_ids, presenter);
            if (cursor.advance_story() != pr::CampaignError::none) {
                return SessionError::flow_stalled;
            }
            continue;
        }

        present_if_present(package, node.dialogue_ids, presenter);
        const auto loaded = pr::load_encounter(package, node.encounter_id);
        if (!loaded) return SessionError::encounter_rejected;
        BattleReport battle;
        const SessionError status =
            play_battle(loaded, player_side, presenter, battle);
        if (status != SessionError::none) return status;
        if (battle.quit || battle.outcome == sim::Outcome::ongoing) {
            return SessionError::none;
        }
        if (cursor.advance_after(battle.outcome, battle.objectives) !=
            pr::CampaignError::none) {
            return SessionError::flow_stalled;
        }
    }
    return SessionError::flow_stalled;
}

}  // namespace grandleon::client
