# Campaign

`grandleon_campaign` owns the campaign that outlives a battle: the roster,
what its members have become, what the party carries, what the world
remembers, and the atomic batches of consequence that change any of it.
Permadeath removes from a roster, recruitment adds to it, growth changes what
is in it, and deployment chooses from it. This module is the roster at rest.

## Why it is its own module

The obvious home was `engine/package_runtime`, which holds `CampaignCursor`.
Two reasons it is not:

- **The dependency would point the wrong way.** A roster is a set of
  identities and numbers whether or not a package is mounted or a battle is
  in progress. Putting it in `package_runtime` would give the save layer and
  every platform storage adapter a transitive dependency on the rules engine,
  just to read a list of names.
- **`CampaignCursor` is not durable state.** It walks an *authored* flow
  decoded out of a package section: which node is next, which branch its
  predicates take. That is content, and it belongs with the other decoded
  content. What lives here is what the *player* accumulated, which no package
  contains and no recompile may overwrite. The two meet in
  `engine/campaign_runtime`, which links both and adds a dependency to
  neither.

So the target links `grandleon::core` and nothing else. The simulation does
not know this module exists, and must not: a battle is battle-local, and the
day it reads a roster is the day a canonical hash depends on a save file.
There is no filesystem, no clock, no platform API, and no randomness. The one
thing said about dice is said without throwing any: `derive_growth_seed` folds
an `OutcomeSource` (encounter, battle hash, sequence) into the seed a
level-up's rolls come from. Who draws from it, and against which authored
chances, is `engine/campaign_runtime`'s: those are content questions.

## The three identity levels

`identity.hpp`. Each answers a different question:

| Type | Question | Lifetime |
| --- | --- | --- |
| `DefinitionRef` | what kind of thing is this? | the package's |
| `PersistentEntityId` | which one of them is this? | the campaign's |
| `BattleEntityId` | who is this on the board? | one encounter's |

`DefinitionRef` is `core::ContentRef` (package id, category, stable id) under
the design's name for it. The other two are wrappers over
`std::uint64_t` so a persistent id cannot be passed where a battle id is
wanted. Zero is reserved in both and means "no entity": the shared store
uses it as an owner, and a summoned unit maps back to it.

Around them:

- `DefinitionRegistry` detects the collision `engine/core/README.md` calls
  "the compiler's problem": two authored keys resolving to one stable id are
  two characters a roster could never tell apart, and both directions are
  refused.
- `DefinitionRenameTable` resolves a stored reference through explicit,
  package-provided rename mappings, following chains and refusing cycles. A
  loader that guessed would silently repoint a dead character's record; a
  reference that survives the renames and still names nothing mounted is
  `missing_definition`, never a silent blank.
- `BattleBinding` joins a board to the campaign, partial in one direction on
  purpose: every deployed member has a battle id, but summons and the
  opposing side have no campaign identity, and asking about one answers
  "nobody" rather than failing.

A campaign whose roster is full loads a board that hashes identically to the
one the package alone produces, so attaching a campaign costs the content
nothing. The rules these types impose on a game author are stated in
`games/template/README.md` under "Identity ownership and compatibility".

## The state

`state.hpp`. `CampaignState` is plain data with a stated canonical order:
every collection sorted, so two campaigns told the same things are the same
campaign byte for byte. A `PersistentUnit` carries identity, definition,
availability, progression, and what it holds; beside the roster sit the
shared store, objective records, typed world values, and the ids of every
committed outcome.

- `Availability` distinguishes `retired` from `dead` because a rule may
  reverse the first and no rule here reverses the second: a permanently dead
  character cannot reappear because a later map lists them.
- **Script state is world state.** Nothing executes `scriptBindings`, so a
  script that one day wants a durable variable asks for a world value keyed
  by its own definition reference, and the save format learns nothing new.
- **A dead member carries nothing.** Their kit returns to the shared store
  when the death commits, and `validate` enforces it. The alternative is
  durable state no rule can reach again.
- **A kit is filled and drained by the two operations that already exist**:
  `add_item` and `consume_item` against a member. What goes in is decided
  above, in `engine/campaign_runtime`, the only layer that may read a
  package and a roster at once.

`CampaignProgress` is where the campaign stands in its graph and the route it
walked. The route is the one unsorted collection, because there the order *is*
the data: two campaigns that reached one node through different predecessors
agree about the node and disagree about the route, and that difference must
survive a save. `canonical_hash` folds the whole campaign in save order; it is
a comparison diagnostic, and nothing pins a literal value for it.

## The outcomes

`outcome.hpp`. A battle produces a `CampaignOutcomeBatch`: an id, and ordered
typed operations.

- **Deterministic.** `derive_outcome_id` is FNV-1a-64 over the source (the
  encounter, the battle's canonical hash, which completion this is) and then
  over every operation. The operations are in it deliberately: an id from the
  source alone would name where an outcome came from but not what it said, so
  a corrected batch would be mistaken for one already committed.
- **Idempotent.** The campaign records committed ids; applying one twice is
  `already_applied` and changes nothing, and a batch rebuilt after a restart
  is recognised as the same outcome.
- **Atomic.** `apply_outcome` copies the state, applies every operation,
  validates the whole candidate, and only then swaps it in. It judges the
  resulting state rather than the steps, because individually legal
  operations can still reach an arrangement no legal sequence could.

Operations are flat fixed-width tagged records, not a `std::variant`: a save
writes these fields and a Nintendo 64 reads them back, and no
standard-library layout crosses that boundary. Typed constructor functions
(`recruit_unit`, `record_permanent_death`, `consume_item` and the rest) keep
the flat record an encoding detail.

## The graph

`graph.hpp`. Where a campaign goes next.

- **Two commits, in a stated order.** `complete_node` commits the outcome
  batch first, because a fought battle's consequences are facts whether or
  not the author left an edge out. The resulting campaign is the one snapshot
  every predicate reads. The chosen edge, active node and route then commit
  together. A node whose conditions all decline and which has no fallback
  keeps its outcome and stays put: `blocked`, an answer rather than a
  half-move.
- **Priority decides, and nothing else may.** Conditional edges carry
  explicit unique integer priorities; `validate_graph` refuses a repeated
  one, so an author's array order is genuinely irrelevant. The single
  unconditional fallback is eligible only when nothing conditional matched.
- **A cycle is legal and walked once per completion.** Every route step
  names the outcome id that caused it, and a completion already in the route
  advances nothing. A retry after a crash is recognised and the edge back is
  not taken twice.
- **`jump_to_node` is the same move with the choosing taken out.** It stands
  the campaign on a node it was told, rather than one it selected, and shares
  every refusal and both commits with `complete_node`. It exists for checking
  a game on hardware: reaching the fifth battle to look at one thing should
  not cost the four before it. It records the jump as an ordinary route step,
  caused by its own batch, so a save written after one resumes like any other
  and nothing downstream learns that jumping exists. **What it does not do is
  the honest half:** it moves the campaign and changes nothing else, so a
  stage reached this way has not recorded the objectives, set the world flags
  or gained the recruits the ordinary route would have. A transition out of it
  that asks about any of that will not match, and the battle itself may be
  unwinnable. Inventing those facts would be wrong differently at every
  branch; whoever offers the jump says so to the player.

The predicate vocabulary is the source schema's: an objective's recorded
result, an inventory count, a typed world value. The inventory count has no
package encoding; `engine/campaign_runtime/README.md` says what that leaves.

## The save

`save.hpp`. The header gives the envelope field by field; the shape:

- **Sectioned, each section versioned on its own.** Six sections (roster,
  store, objectives, world, outcome history, progression), each with its own
  schema version, because a roster and an outcome history change shape at
  different times. The roster section is at schema 3 and migrates one
  version at a time against committed fixture bytes; the other five sit at
  1.0.
- **The progression section is optional in both directions**: written only
  by a campaign that has entered a graph, so one campaign has exactly one
  byte encoding, and marked optional so a build that does not know it
  retains its bytes.
- **Fixed-width little-endian, never a struct's memory.**
  `definition_ref_encoded_size` is twenty-eight bytes;
  `sizeof(DefinitionRef)` is whatever a host ABI chose; the encoder writes
  the first.
- **Two levels of FNV-1a-64 integrity:** one over header, package table and
  directory, one per section. A save whose roster is damaged can say *roster*
  rather than *something*. They detect **corruption, not tampering**: anyone
  editing a save can recompute both, authenticity would need a key with
  somewhere to keep it, and a single-player save has no threat model a
  keyless MAC improves. The save is the player's file.
- **Bounded decode, because save bytes are untrusted input.** Every count
  and length is checked against the bytes that remain and against
  `SaveLimits` before anything is reserved. The tests feed every prefix of a
  valid save, overstated lengths, and counts patched to nonsense.
- **Decode, then interpret.** `decode_save_envelope` asks "are these bytes an
  intact save?"; `interpret_save` asks "does this build understand them?";
  `validate` judges the candidate with the same function a commit is judged
  by; `load_campaign_into` assigns the live session only at the end, so a
  rejected load leaves the session holding what it held. The migration
  registry slots between the first two calls.
- **Unknown sections have two answers, and the older build does not
  choose**: refused if marked required, retained byte-for-byte if not. That
  is what stops an older build from deleting a newer build's data on the next
  save, and why `save(load(bytes)) == bytes` can be asked for. The layout is
  canonical (ascending section type, four-byte aligned, no gaps), so one
  campaign has exactly one encoding.

A representative campaign (twelve members carrying two stacks each, eight
store stacks, eight objectives, eight world values, twenty-two committed
outcomes, one package requirement) encodes to **2868 bytes** before it enters
a graph and **3128** standing three nodes into one; both are asserted in
`tests/campaign/save_test.cpp`, so an adapter author inherits a number.
Where the bytes go is `platform/storage`'s business, which nothing in
`engine/` links.

## The migrations

`migration.hpp`. A save goes out of date twice over, and only one of them is
a version number. The **format** can move: a section learns a field, its
schema version says so, and a section step rewrites the bytes before
interpretation. The **content** can move: the package renamed a definition,
nothing about the bytes is wrong, and a content step repoints references in
the interpreted candidate through the `DefinitionRenameTable`. Neither can
stand in for the other.

- **Version by version.** A step goes from one version to the next; a save
  at 1 reaching a build at 3 runs 1→2 then 2→3. `plan_section_migration`
  stops at the first version with no step out of it and names it:
  `missing_step`, never a silent skip. Backwards is `downgrade_refused`,
  because the only honest transform downwards is a lossy one.
- **Old bytes in, new bytes out.** No step mutates anything; the caller's
  originals are untouched whatever happens, which is what makes the load
  transactional. A migrated save goes through the same `interpret_save` and
  `validate` every save goes through.
- **The engine ships the seam; a package ships its own renames.** The
  default registry holds the roster's own chain; content steps are
  deliberately absent, and a host registers its packages' renames.
  `games/template/README.md` states that contract for the person shipping
  one.
- **Two golden fixtures pin it.** `tests/campaign/fixtures/` holds two saves
  this build's writer cannot produce: one at roster schema 0, one naming
  renamed-away definitions. Both are committed as bytes and loaded through
  the registry into a campaign asserted field by field. Regenerating one to
  make a test pass would turn it inside out; it would then pin nothing.

## Tests

```sh
cmake --build build
ctest --test-dir build -R "grandleon.campaign|grandleon.storage" --output-on-failure
```
