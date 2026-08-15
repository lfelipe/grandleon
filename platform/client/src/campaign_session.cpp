// SPDX-License-Identifier: MIT
#include <grandleon/client/campaign_session.hpp>

#include <grandleon/client/session.hpp>
#include <grandleon/package_runtime/campaign.hpp>

#include <algorithm>
#include <utility>

namespace grandleon::client {
namespace {

namespace cr = campaign_runtime;
namespace pr = package_runtime;
namespace sim = simulation;

campaign::ObjectiveOutcome outcome_of(sim::ObjectiveState state) noexcept {
    return state == sim::ObjectiveState::failed
               ? campaign::ObjectiveOutcome::failed
               : campaign::ObjectiveOutcome::satisfied;
}

// What the save says it needs, and what is actually mounted. The same package,
// stated twice, because the two are different questions and the load path
// compares them: one is what the bytes believed, the other is what is here.
campaign::SavePackageRequirement requirement_of(
    const package_format::LoadedPackage& package
) {
    campaign::SavePackageRequirement requirement;
    requirement.package = package.game_id;
    requirement.content_revision = package.content_revision;
    return requirement;
}

campaign::MountedContent mounted_content(
    const package_format::LoadedPackage& package
) {
    campaign::MountedContent mounted;
    campaign::MountedPackage present;
    present.package = package.game_id;
    present.content_revision = package.content_revision;
    mounted.mount(present);
    return mounted;
}

}  // namespace

std::string_view campaign_session_error_name(
    CampaignSessionError error
) noexcept {
    switch (error) {
        case CampaignSessionError::none: return "none";
        case CampaignSessionError::graph_rejected: return "graph_rejected";
        case CampaignSessionError::board_rejected: return "board_rejected";
        case CampaignSessionError::invalid_slot: return "invalid_slot";
        case CampaignSessionError::roster_rejected: return "roster_rejected";
        case CampaignSessionError::progression_rejected:
            return "progression_rejected";
        case CampaignSessionError::flow_stalled: return "flow_stalled";
    }
    return "unknown";
}

std::string_view management_error_name(ManagementError error) noexcept {
    switch (error) {
        case ManagementError::none: return "none";
        case ManagementError::not_managing: return "not_managing";
    }
    return "unknown";
}

pr::EncounterLoadResult PackageBoards::board(std::uint64_t encounter_id) const {
    return pr::load_encounter(*package_, encounter_id);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

CampaignSession::CampaignSession(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    const CampaignBoards& boards,
    storage::SlotStorage& device,
    const CampaignSessionOptions& options
)
    : package_{&package},
      campaign_id_{campaign_id},
      boards_{&boards},
      device_{&device},
      options_{options} {}

CampaignSessionError CampaignSession::begin(
    SlotFailure& failure,
    bool& refused,
    bool& resumed
) {
    refused = false;
    resumed = false;
    if (!storage::is_valid_slot_name(options_.slot)) {
        return CampaignSessionError::invalid_slot;
    }

    // The authored flow, twice: as a graph the persistent layer evaluates, and
    // as the decoded definition that says which encounter a node is fought at.
    // Two questions, two structures, and neither answers the other's.
    const cr::CampaignGraphLoad flow =
        cr::load_campaign_graph(*package_, campaign_id_);
    if (!flow) return CampaignSessionError::graph_rejected;
    pr::CampaignLoadResult authored = pr::load_campaign(*package_, campaign_id_);
    if (!authored) return CampaignSessionError::graph_rejected;
    graph_ = flow.source.graph;
    authored_ = std::move(authored.definition);

    // The company the author wrote, in the order they wrote it. The persistent
    // ids are one-based and follow that order: founding members first, then
    // each node's recruits in flow order. So the same content founds the same
    // company every time, and a save written by one run is read by the next.
    members_.clear();
    assignments_.clear();
    // What the author wrote about individual characters, read once out of the
    // campaign record and handed to every board this session prepares. It is
    // content, not state, and never changes during a campaign, so it is
    // computed here beside the company rather than on each board.
    specificities_ = cr::member_specificities(authored_);
    for (const pr::CampaignMember& authored : authored_.members) {
        AuthoredMember member;
        member.entry.member = campaign::PersistentEntityId{
            static_cast<std::uint64_t>(members_.size() + 1U)
        };
        member.entry.placement_source_key = authored.id;
        member.entry.name = authored.name_in(*package_);
        member.entry.unit_type = campaign::DefinitionRef{
            package_->game_id,
            core::ContentCategory::unit_type,
            authored.unit_type_id
        };
        member.entry.availability = campaign::Availability::unrecruited;
        member.join_node_id = authored.join_node_id;
        // Everybody is assigned, including those who have not joined yet: a
        // board that places a future recruit leaves them off, because the
        // campaign does not hold them, rather than fielding a stranger.
        assignments_.push_back({authored.id, member.entry.member});
        members_.push_back(std::move(member));
    }

    // The live session, which is what a failed load must leave standing.
    live_ = campaign::make_campaign_save(
        campaign::CampaignState{}, {requirement_of(*package_)}
    );
    {
        std::vector<campaign::CampaignOutcomeOperation> founding =
            recruitment_at(0U);
        // A campaign that authors nobody to start with is refused by name. The
        // company a player commands is content, and a client that made one up
        // would be answering a question only the author can answer. Asked
        // before the store is stocked, so that a starting store can never
        // answer the roster's question for it.
        if (founding.empty()) return CampaignSessionError::roster_rejected;
        // And what the company owns before it has fought anything, in the same
        // batch and after the members, because the store's stock is a
        // consequence of the founding rather than a separate event and a
        // campaign founded twice must derive the same batch either time.
        const std::vector<campaign::CampaignOutcomeOperation> stock =
            grants_at(0U);
        founding.insert(founding.end(), stock.begin(), stock.end());
        const campaign::CampaignOutcomeBatch batch = campaign::make_outcome_batch(
            {campaign::DefinitionRef{
                 package_->game_id,
                 core::ContentCategory::campaign,
                 campaign_id_
             },
             0U,
             0U},
            founding
        );
        if (!campaign::apply_outcome(live_.state, batch)) {
            return CampaignSessionError::roster_rejected;
        }
        if (campaign::begin_campaign(live_.state, graph_) !=
            campaign::ProgressionError::none) {
            return CampaignSessionError::roster_rejected;
        }
    }
    loaded_ = true;

    // Resume, or say why not and keep playing what was just founded.
    if (options_.resume) {
        failure = SlotFailure{};
        failure.slot = options_.slot;
        const storage::StorageRead read = device_->read(options_.slot);
        if (!read) {
            failure.storage = read.error;
            refused = true;
        } else {
            // A candidate rather than a swap. `load_campaign_migrated_into`
            // would already have replaced the live campaign by the time the
            // check below ran, and a save that is a perfectly good campaign of
            // somebody else's is exactly the case that check exists for: the
            // bytes are valid, the state is valid, and resuming it would stand
            // the player on a node this flow does not contain.
            const campaign::MigratedLoad restored =
                campaign::load_campaign_migrated(
                    read.bytes,
                    campaign::SaveLoadOptions{},
                    campaign::standard_save_migrations(),
                    mounted_content(*package_)
                );
            if (!restored) {
                failure.migration = restored.migration.error;
                failure.save = restored.load.error;
                failure.state =
                    restored.load.state_error != campaign::StateError::none
                        ? restored.load.state_error
                        : restored.migration.state_error;
                refused = true;
            } else if (!(restored.save.state.progress.active &&
                         restored.save.state.progress.campaign ==
                             graph_.campaign)) {
                failure.wrong_campaign = true;
                refused = true;
            } else {
                live_ = restored.save;
                resumed = true;
            }
        }
    }
    return CampaignSessionError::none;
}

std::vector<RosterEntry> CampaignSession::roster() const {
    return roster_now();
}

std::vector<campaign::InventoryStack> CampaignSession::store() const {
    return live_.state.store;
}

std::vector<RosterEntry> CampaignSession::roster_now() const {
    std::vector<RosterEntry> entries;
    for (const AuthoredMember& member : members_) {
        const campaign::PersistentUnit* const unit =
            campaign::find_unit(live_.state, member.entry.member);
        if (unit == nullptr) continue;
        RosterEntry entry = member.entry;
        entry.availability = unit->availability;
        entry.progression = unit->progression;
        entry.carried = unit->carried;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<campaign::CampaignOutcomeOperation> CampaignSession::recruitment_at(
    std::uint64_t node_id
) const {
    std::vector<campaign::CampaignOutcomeOperation> operations;
    for (const AuthoredMember& member : members_) {
        if (member.join_node_id != node_id) continue;
        operations.push_back(
            campaign::recruit_unit(member.entry.member, member.entry.unit_type)
        );
        operations.push_back(campaign::set_availability(
            member.entry.member, campaign::Availability::available
        ));
        // And what their unit type says they arrive with, in the same batch, so
        // a member is stocked exactly when they join and never again. A unit
        // type the package cannot read is left unstocked rather than guessed
        // at: `begin` refuses a founding it could not build, and a node that
        // recruits a type this package does not carry has a bigger problem than
        // an empty satchel.
        const cr::StartingKit kit = cr::starting_kit(
            *package_, member.entry.member, member.entry.unit_type.stable_id
        );
        operations.insert(
            operations.end(), kit.operations.begin(), kit.operations.end()
        );
    }
    return operations;
}

std::vector<campaign::CampaignOutcomeOperation> CampaignSession::grants_at(
    std::uint64_t node_id
) const {
    return cr::node_item_grants(package_->game_id, authored_, node_id);
}

std::vector<RosterEntry> CampaignSession::recruited_at(
    std::uint64_t node_id
) const {
    std::vector<RosterEntry> joined;
    for (const AuthoredMember& member : members_) {
        if (member.join_node_id != node_id) continue;
        const campaign::PersistentUnit* const unit =
            campaign::find_unit(live_.state, member.entry.member);
        if (unit == nullptr) continue;
        RosterEntry entry = member.entry;
        entry.availability = unit->availability;
        entry.progression = unit->progression;
        joined.push_back(std::move(entry));
    }
    return joined;
}

const pr::CampaignNode* CampaignSession::node_at(
    const campaign::DefinitionRef& where
) const noexcept {
    const pr::CampaignNode* found = nullptr;
    for (const pr::CampaignNode& candidate : authored_.nodes) {
        if (cr::campaign_node_ref(package_->game_id, candidate.id) == where) {
            found = &candidate;
        }
    }
    return found;
}

CampaignSession::Standing CampaignSession::standing() const {
    Standing where;
    if (!loaded_ || !live_.state.progress.active) {
        where.error = CampaignSessionError::flow_stalled;
        return where;
    }
    where.node = live_.state.progress.active_node;
    const pr::CampaignNode* const node = node_at(where.node);
    if (node == nullptr) {
        where.error = CampaignSessionError::flow_stalled;
        return where;
    }
    where.kind = node->kind;
    where.dialogue_ids = node->dialogue_ids;
    if (node->kind == pr::CampaignNodeKind::encounter) {
        where.encounter_id = cr::encounter_of_node(authored_, where.node);
    }
    return where;
}

CampaignSessionError CampaignSession::advance_story(
    std::vector<RosterEntry>& joined
) {
    joined.clear();
    const Standing where = standing();
    if (where.error != CampaignSessionError::none) return where.error;
    const pr::CampaignNode* const node = node_at(where.node);
    if (node == nullptr) return CampaignSessionError::flow_stalled;
    // A story node fights nothing, so the only consequence it can carry is one
    // the author wrote onto it. It is still completed through the graph rather
    // than stepped past, because the history a save resumes from is the history
    // the graph wrote, and a recruitment rides in that same batch so that it
    // commits when the node commits and not otherwise. So does a grant, for
    // exactly the same reason: what the abbot gives the company is a
    // consequence of passing the abbey and nothing else.
    std::vector<campaign::CampaignOutcomeOperation> consequences =
        recruitment_at(node->id);
    const std::vector<campaign::CampaignOutcomeOperation> given =
        grants_at(node->id);
    consequences.insert(consequences.end(), given.begin(), given.end());
    const campaign::NodeCompletion moved = campaign::complete_node(
        live_.state,
        graph_,
        campaign::make_outcome_batch(
            {where.node,
             0U,
             static_cast<std::uint64_t>(live_.state.progress.history.size())},
            consequences
        )
    );
    if (!moved || !(moved.advanced || moved.already_advanced)) {
        return CampaignSessionError::progression_rejected;
    }
    joined = recruited_at(node->id);
    return CampaignSessionError::none;
}

CampaignSession::PreparedBoard CampaignSession::prepare_board() {
    PreparedBoard prepared;
    const Standing where = standing();
    if (where.error != CampaignSessionError::none) {
        prepared.error = where.error;
        return prepared;
    }
    prepared_ready_ = false;
    // Which completion this is within the campaign. `OutcomeSource`'s third
    // field is what separates fighting the same node twice, to the same end,
    // from fighting it once. A cycle in a campaign graph makes that ordinary
    // rather than exotic. The progression history is the counter to use because
    // it is the one the campaign already keeps and the one a save carries, so a
    // resumed campaign counts on from where it stopped rather than starting
    // again at nought and colliding with itself.
    prepared_sequence_ =
        static_cast<std::uint64_t>(live_.state.progress.history.size());
    prepared_node_ = where.node;
    prepared_node_id_ = 0U;
    if (const pr::CampaignNode* const node = node_at(where.node)) {
        prepared_node_id_ = node->id;
    }
    prepared_encounter_ = where.encounter_id;

    // The board, with what the author wrote about the characters attached to
    // it. Attached here rather than inside `CampaignBoards::board`, because a
    // board provider knows a package and this session knows which campaign is
    // being played. The join reads the table off the board precisely so that a
    // front end which built its board from uncompiled content can attach its
    // own and go through this same pass.
    pr::EncounterLoadResult loaded = boards_->board(where.encounter_id);
    loaded.member_specificities = specificities_;
    // And whether this campaign has declared that its company cannot be felled.
    // Attached here for the reason the specificities are attached here: the join
    // takes a board rather than a package, precisely so that a front end holding
    // content that was never compiled goes through the same pass, and this
    // session is the thing that knows which campaign is being played.
    loaded.company_endures = authored_.invulnerable_for_testing;
    cr::CampaignEncounter board =
        cr::join_campaign_roster(std::move(loaded), live_.state, assignments_);
    if (!board) {
        prepared.error = CampaignSessionError::board_rejected;
        prepared.roster_error = board.error;
        return prepared;
    }

    prepared.board.node = where.node;
    prepared.board.encounter_id = where.encounter_id;
    prepared.board.excluded = board.excluded;
    prepared.board.roster = roster_now();
    prepared.board.store = live_.state.store;
    prepared.board.binding = board.binding;
    prepared.board.character_loss = authored_.character_loss;
    prepared_binding_ = board.binding;
    prepared_unit_types_ =
        cr::board_unit_types(board.encounter.definition);
    prepared_ready_ = true;
    // The board goes to whoever is going to fight it. The session keeps the
    // binding and the unit-type table above, which is all a commit reads of it,
    // rather than a second whole encounter.
    prepared.encounter = std::move(board);
    return prepared;
}

CampaignSessionError CampaignSession::commit_battle(
    const BattleReport& battle,
    BattleAftermath& aftermath
) {
    if (!prepared_ready_) return CampaignSessionError::board_rejected;

    // What the battle did, as campaign consequences. The deaths are read off
    // the board through the binding; everything else is the campaign runtime's.
    const campaign::OutcomeSource source{
        campaign::DefinitionRef{
            package_->game_id,
            core::ContentCategory::encounter,
            prepared_encounter_
        },
        battle.canonical_hash,
        prepared_sequence_
    };
    const cr::BattleProgression progression = cr::derive_battle_progression(
        *package_, live_.state, prepared_unit_types_, prepared_binding_,
        battle.events, source
    );
    if (!progression) return CampaignSessionError::progression_rejected;

    // What the characters did, first. A spend comes out of the spender's own
    // kit, and `apply_outcome` refuses every operation against a permanently
    // dead member. A character who drinks their last draught and then falls has
    // to be charged for it while they are still alive, or the batch refuses
    // itself. It is also the true sequence: the draught was drunk before the
    // blow landed.
    std::vector<campaign::CampaignOutcomeOperation> operations =
        progression.operations;

    // Then who did not get up.
    //
    // Who *fell* is read off the board either way, because it happened either
    // way: a character at zero health left the battlefield when they reached it,
    // and no rule this campaign might have chosen changes that. What the rule
    // changes is only what is written down about it afterwards.
    //
    // Under the permanent rule that is a death: their kit returns to the store,
    // whatever is left of it after the spends above. That is
    // `record_permanent_death`'s own rule and not this file's.
    //
    // Under the recoverable rule it is no operation at all. Nothing needs
    // undoing, because a battle never wrote anything about them into the
    // campaign in the first place: health is battle-local and a member's record
    // holds their availability, their progression and their satchel, none of
    // which a fall touched. So they are simply still available, still carrying
    // what the battle left them with, and still on the next board. That the
    // softer rule is spelled by *omitting* an operation rather than by adding a
    // reviving one is the reason this option costs a byte and not a migration.
    const bool losses_are_permanent =
        authored_.character_loss == pr::CharacterLoss::permanent;
    std::vector<campaign::PersistentEntityId> fallen;
    for (const sim::UnitSnapshot& unit : battle.final_snapshot.units) {
        // Alive, and deliberately not `sim::on_board`. This asks who *fell*,
        // not who was standing at the bell: a character talked off the board
        // walked away with the health it had, and a wave the battle ended
        // before never arrived. Neither is a death, and recording either as one
        // is exactly the confusion the departure and arrival bits exist to
        // prevent. Health is the whole of the question here.
        if (unit.health > 0) continue;
        const campaign::PersistentEntityId member =
            prepared_binding_.persistent_of(campaign::BattleEntityId{unit.id});
        if (member.value == 0U) continue;
        fallen.push_back(member);
        if (losses_are_permanent) {
            operations.push_back(campaign::record_permanent_death(member));
        }
    }
    for (const sim::ObjectiveResult& objective : battle.objectives) {
        if (objective.state == sim::ObjectiveState::pending) continue;
        operations.push_back(campaign::record_objective(
            campaign::DefinitionRef{
                package_->game_id,
                core::ContentCategory::objective,
                objective.id
            },
            outcome_of(objective.state)
        ));
    }

    // Whoever this node was written to bring in joins in the battle's own
    // batch: one commit, one atomic set of consequences, and a recruit that
    // survives a save for exactly the same reason a death does.
    const std::vector<campaign::CampaignOutcomeOperation> joining =
        recruitment_at(prepared_node_id_);
    operations.insert(operations.end(), joining.begin(), joining.end());

    // And whatever winning this board was written to be worth, last, so that
    // adding it moved no operation that was already there. A reward is a grant
    // like any other: it goes into the store, and a node a route returns to
    // pays it again because its batch's sequence has moved.
    const std::vector<campaign::CampaignOutcomeOperation> given =
        grants_at(prepared_node_id_);
    operations.insert(operations.end(), given.begin(), given.end());

    const campaign::NodeCompletion completion = campaign::complete_node(
        live_.state, graph_, campaign::make_outcome_batch(source, operations)
    );

    aftermath = BattleAftermath{};
    aftermath.node = prepared_node_;
    aftermath.encounter_id = prepared_encounter_;
    aftermath.outcome = battle.outcome;
    aftermath.canonical_hash = battle.canonical_hash;
    aftermath.progression = progression;
    aftermath.fallen = std::move(fallen);
    aftermath.character_loss = authored_.character_loss;
    aftermath.recruited =
        completion ? recruited_at(prepared_node_id_) : std::vector<RosterEntry>{};
    aftermath.roster = roster_now();
    aftermath.store = live_.state.store;
    aftermath.binding = prepared_binding_;
    aftermath.completion = completion;
    // What was kept of the board stays where it is rather than being given
    // back: `prepared_ready_` is what says a commit may read it, and the next
    // `prepare_board` overwrites both in place. On the allocator this exists
    // for, a free followed by an allocation of the same size is a worse trade
    // than reusing the capacity.
    prepared_ready_ = false;

    if (!completion) return CampaignSessionError::progression_rejected;
    if (!completion.advanced && !completion.already_advanced) {
        return CampaignSessionError::progression_rejected;
    }
    return CampaignSessionError::none;
}

// ---------------------------------------------------------------------------
// The company, between battles
// ---------------------------------------------------------------------------

CompanyManagement CampaignSession::management() {
    CompanyManagement company;
    const Standing where = standing();
    if (where.error != CampaignSessionError::none) {
        company.error = where.error;
        return company;
    }
    company.node = where.node;
    company.encounter_id = where.encounter_id;
    company.roster = roster_now();
    company.store = live_.state.store;

    // Which members the next board has somewhere to stand. A property of the
    // board rather than of the campaign, so it is decoded once per node and
    // read for every gesture after that.
    if (where.encounter_id == 0U) {
        placeable_encounter_ = 0U;
        placeable_.clear();
        return company;
    }
    if (placeable_encounter_ != where.encounter_id) {
        placeable_ = cr::members_a_board_places(
            boards_->board(where.encounter_id), assignments_
        );
        placeable_capacity_ =
            boards_->board(where.encounter_id).deployment_capacity;
        placeable_encounter_ = where.encounter_id;
    }
    company.placeable = placeable_;
    company.capacity = placeable_capacity_;
    // Who would actually go, re-read on every gesture rather than cached with
    // the board: the placements are a property of the board and do not move,
    // and who is deployable is a property of the campaign and moves with every
    // bench.
    company.fielded = cr::members_a_board_fields(
        boards_->board(where.encounter_id), live_.state, assignments_
    );
    return company;
}

ManagementCommit CampaignSession::commit_management(
    std::vector<campaign::CampaignOutcomeOperation> operations
) {
    ManagementCommit result;
    if (!loaded_ || !live_.state.progress.active) {
        result.error = ManagementError::not_managing;
        return result;
    }
    // Where the company is standing, what produced the batch, and how much this
    // campaign has already done. The node reference rather than an encounter
    // reference is what keeps a management batch and the battle fought at that
    // node from ever sharing an identity; the zero hash is honest because no
    // battle produced this; and the count of committed outcomes is what makes
    // the second identical gesture a second move rather than a retry of the
    // first. That count is already campaign state, already saved, and already
    // monotone, so nothing here keeps a counter of its own.
    const campaign::OutcomeSource source{
        live_.state.progress.active_node,
        0U,
        static_cast<std::uint64_t>(live_.state.applied_outcomes.size())
    };
    result.batch = campaign::make_outcome_batch(source, std::move(operations));
    result.application = campaign::apply_outcome(live_.state, result.batch);
    if (!result.application) return result;
    // Committed, so written. Management happens between battles, which is when
    // saves happen, and a screen that showed a moved draught the slot did not
    // hold would be showing a player something that will not be there tomorrow.
    result.save = save();
    result.saved = true;
    return result;
}

ManagementCommit CampaignSession::give_item(
    campaign::PersistentEntityId member,
    const campaign::DefinitionRef& item
) {
    // Out of the store before into the hands. The batch is atomic either way,
    // since `apply_outcome` validates a complete candidate before it swaps. All
    // the order decides is which name the player is shown, and "the store has
    // none of those" is the one they need.
    return commit_management(
        {campaign::consume_item(campaign::PersistentEntityId{}, item, 1U),
         campaign::add_item(member, item, 1U)}
    );
}

ManagementCommit CampaignSession::take_item(
    campaign::PersistentEntityId member,
    const campaign::DefinitionRef& item
) {
    return commit_management(
        {campaign::consume_item(member, item, 1U),
         campaign::add_item(campaign::PersistentEntityId{}, item, 1U)}
    );
}

ManagementCommit CampaignSession::set_fielded(
    campaign::PersistentEntityId member,
    bool fielded
) {
    return commit_management({campaign::set_availability(
        member,
        fielded ? campaign::Availability::available
                : campaign::Availability::retired
    )});
}

std::vector<std::uint8_t> CampaignSession::save_bytes() {
    live_.packages = {requirement_of(*package_)};
    return campaign::save_campaign(live_);
}

storage::StorageError CampaignSession::save() {
    return device_->write(options_.slot, save_bytes());
}

// ---------------------------------------------------------------------------
// The terminal's driver
// ---------------------------------------------------------------------------

CampaignSessionError run_persistent_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    Presenter& presenter,
    CampaignNarrator& narrator,
    storage::SlotStorage& device,
    const CampaignSessionOptions& options
) {
    const PackageBoards boards{package};
    CampaignSession session{package, campaign_id, boards, device, options};

    SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    const CampaignSessionError begun = session.begin(failure, refused, resumed);
    if (begun != CampaignSessionError::none) return begun;
    if (refused) narrator.slot_refused(failure);

    narrator.campaign_begun(
        session.roster(), session.store(), options.slot, resumed
    );

    for (int guard = 0; guard < 256; ++guard) {
        const CampaignSession::Standing where = session.standing();
        if (where.error != CampaignSessionError::none) return where.error;

        present_dialogue_sequence(package, where.dialogue_ids, presenter);

        if (where.kind == pr::CampaignNodeKind::terminal) {
            presenter.campaign_ended();
            return CampaignSessionError::none;
        }
        if (where.kind == pr::CampaignNodeKind::story) {
            std::vector<RosterEntry> joined;
            const CampaignSessionError moved = session.advance_story(joined);
            if (moved != CampaignSessionError::none) return moved;
            if (!joined.empty()) narrator.members_joined(joined);
            continue;
        }

        // The company, before the board. The stage stands here and nowhere
        // else, which is what makes it reached after a battle, after a story
        // node, on a resume and before the first board without four rules
        // saying so. Every gesture inside it commits and saves itself, so
        // leaving in the middle of it loses nothing.
        //
        // The board is inside the same loop because a board the roster refuses
        // sends the player back to the company. `prepare_board` publishes
        // nothing when it refuses and commits nothing either way, so a company
        // that benched everybody, or benched the character an objective names,
        // is told the roster's own word for it and given the chance to field
        // somebody again.
        CampaignSession::PreparedBoard prepared;
        bool published = false;
        for (int attempts = 0; attempts < 64 && !published; ++attempts) {
            bool proceeding = false;
            bool leaving = false;
            for (int gestures = 0; gestures < 4096 && !proceeding; ++gestures) {
                CompanyManagement company = session.management();
                if (company.error != CampaignSessionError::none) {
                    return company.error;
                }
                company.refused = prepared.roster_error;
                if (gestures == 0) narrator.management_opened(company);
                const ManagementIntent intent =
                    narrator.next_management_intent(company);
                switch (intent.verb) {
                    case ManagementVerb::none:
                        continue;
                    case ManagementVerb::quit:
                        leaving = true;
                        proceeding = true;
                        continue;
                    case ManagementVerb::proceed:
                        proceeding = true;
                        continue;
                    case ManagementVerb::give:
                        narrator.management_committed(
                            session.give_item(intent.member, intent.item)
                        );
                        continue;
                    case ManagementVerb::take:
                        narrator.management_committed(
                            session.take_item(intent.member, intent.item)
                        );
                        continue;
                    case ManagementVerb::field:
                        narrator.management_committed(
                            session.set_fielded(intent.member, true)
                        );
                        continue;
                    case ManagementVerb::bench:
                        narrator.management_committed(
                            session.set_fielded(intent.member, false)
                        );
                        continue;
                }
            }
            if (leaving) return CampaignSessionError::none;

            prepared = session.prepare_board();
            published = prepared.error == CampaignSessionError::none;
            // Only a roster refusal is a company the player can change. A board
            // the package could not decode is not, and is reported rather than
            // asked about again.
            if (!published &&
                (prepared.error != CampaignSessionError::board_rejected ||
                 prepared.roster_error == cr::RosterError::none)) {
                return prepared.error;
            }
        }
        if (!published) return prepared.error;
        narrator.board_prepared(prepared.board);

        BattleReport battle;
        if (play_battle(
                prepared.encounter.encounter, options.player_side, presenter,
                battle
            ) != SessionError::none) {
            return CampaignSessionError::board_rejected;
        }
        // An unfinished fight is not an outcome. The slot still holds the
        // campaign as it stood after the last battle, which is the honest
        // meaning of leaving in the middle of one.
        if (battle.quit || battle.outcome == sim::Outcome::ongoing) {
            return CampaignSessionError::none;
        }

        BattleAftermath aftermath;
        const CampaignSessionError committed =
            session.commit_battle(battle, aftermath);
        if (committed == CampaignSessionError::board_rejected) return committed;
        // The aftermath is narrated and the campaign is saved whether or not
        // the campaign moved. A node the graph has no route out of still buried
        // somebody and still taught somebody something, and losing that because
        // the author wrote no losing branch would be the client disagreeing
        // with the rules about what happened.
        if (committed != CampaignSessionError::progression_rejected ||
            aftermath.encounter_id != 0U) {
            narrator.battle_aftermath(aftermath);
            if (!aftermath.recruited.empty()) {
                narrator.members_joined(aftermath.recruited);
            }
            narrator.campaign_saved(options.slot, session.save());
        }
        if (committed != CampaignSessionError::none) return committed;
    }
    return CampaignSessionError::flow_stalled;
}

}  // namespace grandleon::client
