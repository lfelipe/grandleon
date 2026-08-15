# Package runtime

`grandleon_package_runtime` translates a structurally validated package into
validated runtime definitions. It owns binary payload decoding and cross-record
reference checks; it does not own gameplay rules.

The encounter loader resolves one encounter, its map, objectives, placements,
unit types, weapons, abilities, and classes into a simulation
`EncounterDefinition`. Terrain arrives in three forms and they are deliberately
different things: the passability the simulation enforces, the movement cost it
charges, and the terrain identities a renderer draws from. A map record may end
before either gameplay block; absent passability is an all-open board and an
absent cost is a board where every step costs one. Unsupported objective kinds
are rejected before a definition is published.

`CampaignCursor` walks the authored flow. A story node presents its dialogue and
advances; an encounter node advances on a completed battle outcome; a terminal
node has no outgoing edge. A node's branches are sorted by ascending priority
and the first whose predicates match is taken. A predicate is `all`, `any` or
`none` over the results objectives actually recorded, so an objective the
encounter never reported cannot hold. With no branch matching, the node's
unconditional target is taken; with neither, the flow is refused as
`unsupported_flow`. A world-flag predicate decodes into a kind tag and a typed
value, but the cursor holds no campaign state, so it never matches and the
unconditional thread is taken; evaluating it against a real campaign is
`engine/campaign_runtime`'s graph. An inventory predicate has no package
encoding at all, and the compiler refuses it by name.

`load_presentation` decodes the package's presentation section: the season the
game's ground is drawn in, the colour each faction's characters wear, the
art-library terrain kind each terrain identity draws as, and the archetype each
unit type wears. It also performs the one join a client would otherwise have to
do itself, pairing each unit type with its faction's colour, so a renderer asks
"what colour does this unit wear?" and never walks two sections to answer it.
Four questions, four lookups: `colour_of_faction`, `colour_of_unit_type`,
`kind_of_terrain`, `archetype_of_unit_type`. None of this is gameplay data: the
simulation never sees it, and changing it cannot move a canonical hash.

The last two are what make a package drawable at all by a client that has no
source project. A cell carries a hash of its authored terrain name and a unit
carries a unit type identity; the name a keyword convention would have to match
against is gone by then, so the compiler resolves both and the package carries
the answers.

Absence is not an error, one record at a time. A package with no presentation
section resolves to the default theme and to no faction colours. A package
whose presentation section holds only the project record resolves every terrain
identity and every unit type to "unresolved", `0xFF`, distinct from any
art-library index and from the registry's own "no keyword matched" value. A
record that is present but truncated, whose declared count its bytes could not
hold, which carries bytes beyond what it declares, or which names one identity
twice is reported as `malformed_payload` and nothing partial is published.

## What every decoder here owes

Two rules, held by a test rather than by convention, because they are the two
that a reader added later is most likely to break quietly.

**A count read out of a file is measured against the bytes actually present
before anything is reserved.** Every repeated thing in this format has a
smallest possible size, so a count is checked as
`count > remaining / smallest`. It divides rather than multiplies, because the
product of a hostile count and a stride is not representable everywhere the
quotient is. Without this a hundred and twenty byte file can ask a loader for
sixty-five kilobytes and a few hundred bytes can ask for megabytes, and every
one of those refusals arrives *after* the allocation.

**Nothing decoded out of a package holds a pointer into it.** A `LoadedPackage`
may be copied, and whoever assembled one may append to its bytes, and a pointer
captured while a record was decoded survives neither. `RecordView` addresses its
payload by offset and `CampaignMember` addresses its name the same way, so both
are resolved against `byte_data()` at the moment of asking.

`tests/package_runtime/hostile_package_test.cpp` holds both. It replaces the
global allocator so a decode can be asked how much memory it wanted and so
freed memory answers something recognisably wrong, and it drives one valid
package covering all seventeen section types through both entry points and
every reader, with every two- and four-byte field in it in turn set to its
largest value, and with every truncation of it. A decoder that believes a
number instead of checking it fails that sweep whatever section it belongs to.

## Tests

Tests compile public semantic source, structurally load the resulting package,
decode the encounter, run it through the simulation, and advance the maintained
campaign to its terminal node. A separate suite compiles a project that chooses
a theme and colours, reads them back, and hands the reader hand-built damaged
sections to prove they are refused rather than misread.
