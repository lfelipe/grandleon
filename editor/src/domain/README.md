# Editor domain contracts

The domain directory is framework-neutral TypeScript. It must not import Vue,
browser UI components, or platform storage APIs.

Editor source-document types in `../generated/source-v1.ts` are generated
directly from the canonical repository JSON Schema. The production build checks
that this file is current, so the schema is the contract authority rather than
a handwritten editor model. Regenerate it with `npm run --prefix editor
generate:source-types` after changing a source schema.

`source-migration.ts` places a game before the schema does. It is the editor's
half of `tools/source_schema/migration.mjs`, which holds the version this build
writes and every step between versions and is shared with the command-line
upgrade tool, so a game brought up here and a game brought up on disk take the
same steps in the same order. The editor imports that module rather than
restating the version. That is why `allowJs` is on. Asking how old a game is
before validating it is what turns `must be equal to constant` into an offer;
this module also turns every refusal into a sentence written for somebody making
a game, and it is the only place that does. Nothing outside it, and nothing an
author reads, spells a version rule out. The upgrade is to the open game and
never to the file: the author's save is what commits it.

Source parsing, schema validation, semantic checks, and normalized indexing run
behind a module-worker client in `../analysis`. Request identifiers safely
correlate concurrent work, failures remain isolated from the UI, and closing the
client rejects pending requests. The analyzer is independently unit-testable;
the worker is only a scheduling boundary. Browser conformance tests consume the
same repository fixtures as the command-line source validator and compare
acceptance plus stable diagnostic code/path pairs.

The diagnostic panel announces aggregate project health, exposes stable codes,
and uses ordinary buttons for source navigation so every reported issue remains
keyboard-operable. Diagnostic messages describe the problem while source path
and JSON instance path provide an exact editor navigation target.

`target-budget.ts` says what one authored game would cost an old console, and
shares the diagnostic panel without being a diagnostic. A project names one
character style and one terrain theme and a console build embeds those alone,
so a console's limits are spent by the game's own variety: the characters it
holds, the colours its sides wear, and the ground its maps name, all of which
the editor already has. The limits and the art's cost are measured rather than
asserted: the palette a drawing spends is counted by
`tools/placeholder_art` and reaches the editor through
`../generated/board-art.ts`, and every console figure cites the measurement it
came from. What a target pays is those measured colours against its own bank
size, so what would overrun a machine is the game's own variety.
A note carries no severity, no path, and nothing to navigate to,
because a game that overruns a console is still a game: nothing here reaches
Save, Export, Play, or the schema, and the menus stay whole whatever it says.

`ProjectStore` exposes a virtual tree of ordinary project files with canonical
relative paths, defensive byte ownership, monotonic revisions, optimistic
write/delete checks, deterministic listing, and complete snapshots. Every storage
adapter runs the same contract suite. Platform-specific concerns such as
IndexedDB transactions, filesystem permissions, and archive downloads stay in
adapters.

The IndexedDB adapter stores committed file revisions and a project revision in
one transaction, recovers them after reopen, upgrades the legacy file-only
database without data loss, and converts predictable storage budgets into a
stable `QUOTA_EXCEEDED` error without replacing prior content.

`SourceProjectDocument` is the application-facing lifecycle for the canonical
`project.json`. It owns UTF-8 serialization, schema and semantic checks before
import, optimistic file revisions, and conversion between a stored project and
a portable archive. `App.vue` keeps the active `SourceProject` snapshot and file
revision together: edits become dirty, Save commits to IndexedDB, startup
recovers that draft, and Validate analyzes the exact active snapshot. An invalid
archive is fully checked before it can replace the current document.

A project is validated before it is written, and the rule is the rule that
decides whether it can be read back: `SourceProjectDocument.save` refuses text
that is not JSON and JSON the source schema rejects, and refuses it by throwing
before the store is touched, so a save can never produce a file the next open
cannot. Everything else the analyzer reports (a reference naming nothing, a
company with no founding member) is a problem in a project that still opens,
and belongs on the diagnostics page rather than in the way of an author saving
work in progress. Export never waits on that gate: it archives the project on
screen, overriding whatever the store holds, because a refused save and a file
another tab is holding are exactly the moments an archive is the only road out.

Portable project archives are written as deterministic, standards-compatible ZIP
files with UTF-8 stored entries. Import validates the complete central
directory, paths, duplicates, methods, sizes, offsets, and CRCs before returning
an immutable candidate snapshot, so a failed import cannot partially mutate the
open project. It also reads a stored archive whose entries carry ASCII names and
no UTF-8 flag, which is what an ordinary command-line zip produces, and it reads
a bare `project.json`. That is the shape the draft-recovery banner hands out,
and it must be a shape the editor takes back. Compression is intentionally
rejected in the reader: this keeps expansion budgets enforceable before
allocating entry output and avoids ZIP-bomb behavior.

The progressive filesystem adapter requests browser permission explicitly and
uses the same project-store contract. It fingerprints fresh file bytes before
optimistic writes or deletes, turning external edits into `REVISION_CONFLICT`
instead of silently overwriting them. **Nothing in the editor reaches it**:
archive import and export are the only road a project takes in or out.

`ProjectIndex` normalizes definitions by `(category, source key)`, keeps source
and semantic locations, tracks typed outbound/inbound references, and produces
stable duplicate or unresolved-reference diagnostics. Replacing one document
rebuilds affected navigation and diagnostics without requiring callers to reopen
the project.

`ProjectEditSession` applies rename and delete commands against complete
in-memory project snapshots. Renames update typed references across documents as
one transaction, deletes are rejected while inbound references exist, and undo
or redo restores the entire affected state rather than one file at a time.
**Nothing in the editor drives it**: `SourceProjectSession` below is the
command boundary the application uses.

`SourceProjectSession` is the structured editor's command boundary for canonical
source documents. Components receive defensive snapshots and submit grouped
metadata or record commands; they never mutate the session's parsed model.
Stable-identifier changes use explicit reference-safe rename commands, referenced
deletes report their inbound semantic paths, and all successful transactions are
fully undoable and redoable.

Campaign encounter placements reference unit types through the same normalized
reference boundary as other structured content. Renaming a unit type updates
every placement atomically, deletion remains guarded, and browser plus CLI
analysis agree on duplicate placement identities, missing unit types, and known
map bounds. Placement sides and coordinates are plain logical data; the editor
does not embed runtime-derived stats in them.

`source-form-model.ts` derives labels, required state, numeric/string bounds,
nested fields, and reference roles from the generated canonical schemas plus
semantic reference rules. `SchemaRecordForm` consumes those descriptors for
project metadata and every v1 definition category. Typed selectors search only
the requested category, retain and explain missing legacy references, distinguish
compatible/incompatible/unknown equipment choices, and can create a related
record without requiring users to type its stable identifier.

Collection navigation renders at most 100 record rows at once and searches the
normalized in-memory collection before paging. The maintained 10,000-item
component fixture must render, switch to Items, and locate an exact stable ID in
under 500 ms in the headless CI browser. This is an interaction regression
budget, not a source-format collection limit; production-browser benchmarks may
tighten it later.

`MapEditSession` is the renderer-independent logical terrain command boundary.
It validates complete map shape and coordinates before mutation, groups
deduplicated paint and connected fill operations into deterministic transactions,
previews every clipped cell before resize/shift/crop, and requires explicit
clipping confirmation. Snapshots and undo/redo remain canonical source maps; no
canvas or renderer state enters the document.

Script bindings cross the editor boundary as inert typed records. The editor
validates their closed value variants, duplicate names/slots, lexical paths, and
typed content references against canonical schemas, but it does not evaluate
expressions, load modules, invoke entry points, or render parameter strings as
markup. Without a script-interface registry it cannot validate entry-point
signatures; bindings are preserved while remaining unsupported and
non-executable. This is not a plugin sandbox or signature system: package
signing, provenance, permissions, and runtime capability security are outside
the current editor contract.

Paths reject absolute forms, empty segments, traversal, backslashes, and NUL
characters before reaching an adapter. This is a logical project path contract,
not an operating-system path abstraction.
