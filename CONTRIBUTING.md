# Contributing to Grandleon

Contributions are welcome: engine and tooling changes, editor work, sample
games, and corrections to the documentation.

## Licence terms

**Everything contributed to this repository is accepted under the
[MIT Licence](LICENSE), the same terms the rest of it carries.** By opening a
pull request you confirm that you wrote what you are submitting, or that you
have the right to release it under those terms, and that you are releasing it
under them.

That applies to a sample project exactly as it applies to code: it is covered by
the same licence, and it must be your own work. Nothing traced from, derived
from, or built against another game's content can be accepted here.

## Proposing a change

1. **Open an issue first for anything that changes observable behaviour.** A
   rule the engine enforces, a package or save format, a schema, a module
   boundary. These are agreements that outlive one implementation, and it is
   cheaper to agree on them before the code exists. Typo fixes, test additions
   and self-contained bug fixes need no issue.
2. **Work on a branch and open a pull request.** Describe what the change does
   and how you know it works. One change per pull request.
3. **Read the module's README before changing it.** Every module under
   `engine/`, `platform/`, `tools/`, `games/` and `editor/` carries one, and it
   states the contract that module holds and the reasoning behind it. A change
   that contradicts a README needs to change the README in the same commit.

[CODING.md](CODING.md) is the contributor's guide: prerequisites, the build, the
reproducible console toolchains, and the specification workflow.
[docs/SETUP.md](docs/SETUP.md) takes a bare machine to a green gate.

## Changing the source schema

Any change to `schemas/source/v1/` bumps `schemaVersion` and registers a
migration step. Not only a breaking one: the schemas set
`additionalProperties: false`, so an added optional field already makes a new
file unreadable by an older editor.

The version lives in `tools/source_schema/migration.mjs`, derived from the
chain: a version exists because a step arrives at it. Add the step there, and
move the schema's `const`, the native compiler's `supported_source_schema`, and
both `games/*/source/project.json` with it; `tools/source_schema/test.mjs`
fails until they agree.

Omitted, empty, defaulted, legacy and unsupported values mean different things
and must stay distinguishable: a validator or an editor never inserts a default
into an imported project merely by opening it.

Every step carries `changed`, one sentence a non-technical author can read:
*"turn order moved onto the Stage"*, not a field name. The editor lists those
sentences and asks before touching anything, so a step without one cannot be
registered.

## The gate

Nothing runs automatically on a pull request. One command decides whether a
change is ready, and you run it:

```sh
scripts/local-ci.sh --preview-port 4521
```

It clones `HEAD` into a temporary directory and runs the whole gate there, so
untracked files and stale generated output cannot make a change look ready. Give
it a preview port nobody else on the machine is using.

The console checks build cross toolchains and emulators from source, so they are
behind flags:

```sh
scripts/local-ci.sh --preview-port 4521 --consoles
```

Run them from the repository root. From a subdirectory, `build` resolves
somewhere else and every console check silently does nothing.

A change is not complete until the behaviour it claims is demonstrably true, not
merely until it compiles. Never weaken an assertion to make a run pass; if one
stops making sense, replace it with one that pins the new truth just as tightly.

## Art

The replacement mechanism works and is checked, but it still needs work to be
done in a way that is future proof: animation and the 3D mesh story have to be
settled before this repository can say what a submission would have to be.

## Style

Match what is already there. Commit messages are plain imperative sentences
describing what the commit does. Documentation states what is true and why,
never what it used to be. The prose here is for someone who has never seen the
repository's history.
