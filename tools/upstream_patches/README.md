# Console engine patches

Pyrite and Mipsync are upstream projects. Grandleon should consume pinned,
reviewed versions of them—not quietly grow private engine forks inside the game
repository.

This directory keeps the few engine fixes we need as ordinary `git
format-patch` files. Each engine has:

- `upstream.env`: the repository and exact base commit;
- `series`: patch filenames in application order;
- one patch per upstream-ready commit.

Grandleon-specific adapters, art conversion, campaign loading and game UI stay
under `platform/`. They do not belong in these patches.

## Making an engine fix

1. Clone the upstream repository (or your fork) separately.
2. Check out the revision in that engine's `upstream.env` and create a topic
   branch.
3. Make one generic fix per commit. Add the smallest upstream test or static
   check that proves it.
4. Run the upstream project's own checks.
5. Export the topic without rewriting this directory yet:

   ```sh
   tools/upstream_patches/manage.sh export mipsync \
     /path/to/Mipsync /tmp/mipsync-patches
   ```

6. Review the generated patches and `export-metadata.env`. Copy the approved
   patches into `engines/mipsync/`, replace its `series`, then verify:

   ```sh
   tools/upstream_patches/manage.sh verify mipsync
   tools/upstream_patches/manage.sh check mipsync /path/to/clean/Mipsync
   ```

Use `pyrite` instead of `mipsync` for N64 work. `check` clones into a temporary
directory, resets to the pin and applies the whole series with `git am`; it does
not modify the source checkout supplied to it.

## Commit boundaries

Keep these changes reviewable as separate Grandleon commits:

1. add or update the upstream patch series;
2. update the pin if a deliberate rebase is required;
3. integrate the patched engine into Grandleon;
4. add or update game assets and captures.

That split lets us submit the engine commits upstream later without dragging in
Grandleon paths, generated assets or campaign behavior. Until an upstream fix
is accepted and included in a new pinned revision, Grandleon applies the local
series reproducibly. After acceptance, rebase the series onto the new pin and
remove only patches that are actually present upstream.

## Applying a series in a build

The build wrapper should create a clean checkout at the pin, then run:

```sh
tools/upstream_patches/manage.sh apply pyrite /tmp/pyrite-source
```

`apply` refuses a dirty checkout or the wrong base revision. This prevents a
successful local build from depending on unrecorded engine edits.

## Production cutover

Parity work may keep the legacy console player as an explicit comparison
baseline. It must never become a silent fallback for the new player. The
cutover is one deliberate change that:

- points the platform's export command only at the new engine;
- fails the export when that engine or its pinned inputs are unavailable;
- embeds a stable renderer identity in every ROM or disc executable;
- deletes the old renderer's source, art and build references;
- runs a source-and-artifact gate proving those conditions.

Add a small platform-owned TSV contract when each console is ready. Run it on
the exact release artifact:

```sh
tools/upstream_patches/cutover-audit.sh \
  platform/nintendo64/pyrite/cutover-contract.tsv "$PWD" game.z64
```

The contract format is tab-separated:

```text
engine	pyrite
required-path	platform/nintendo64/pyrite
forbidden-path	platform/nintendo64/src/old_renderer.cpp
forbidden-text	platform/nintendo64/scripts	old_renderer|legacy_renderer
artifact-text	GRANDLEON_RENDERER=pyrite
```

`required-path` proves the replacement is present. `forbidden-path` and
`forbidden-text` prove the old path cannot be reached. `artifact-text` checks a
literal marker embedded in the built binary. The audit refuses incomplete
contracts, missing artifacts and missing identity markers, so a broken new
renderer cannot quietly ship an old one instead.
