// SPDX-License-Identifier: MIT

// Migration steps that exist only to be walked by a test.
//
// `1.0.0` is the first version of the source format, so `sourceMigrations()`
// ships no steps and there is nothing real to run. That makes the tests the
// whole proof this registry works, and a proof needs something to be about:
// two steps that move a project a visible amount, a chain with a hole in it, a
// step that refuses, and the sentences an author would read for each.
//
// The chain runs *up to* the current version rather than past it: `0.8.0` to
// `0.9.0` to `1.0.0`. That is the one detail worth explaining. A chain
// ending at some invented `1.1.0` would leave a project declaring a version the
// bundled schema refuses, so the only thing a test could check is that the
// steps ran. Ending where the format actually is means the project that comes
// out is a real project: it validates, it opens, and the editor can play it.
// The claim being proved is "an out-of-date game becomes a playable one", and
// only a chain that lands on solid ground can prove it.
//
// Nothing outside a test may import this. `0.8.0` and `0.9.0` are versions the
// format never had, and registering these on the shipped registry would tell an
// author their game needs bringing up to date for a reason that is not true.
//
// Plain arrays rather than built registries, and this module imports
// `migration.mjs` not at all. The editor's suite reaches these steps through a
// test double that replaces `sourceMigrations`, and a double whose factory has
// to import a module that imports the module being replaced never finishes
// building. Steps are data; the registry is what a caller makes of them.
//
// Kept here rather than written twice. Both suites that exercise the registry
// (this directory's `test.mjs` under ctest, and the editor's vitest run) have
// to agree about what the chain does, and two copies of a chain is how two
// suites come to test two different things under one name.

/** The version the example chain starts from. Older than the format has been. */
export const EXAMPLE_OLDEST = "0.8.0";

/** What the first step tells the author. */
export const FIRST_CHANGE = "a game says which season its maps are drawn in";

/** What the second step tells the author. */
export const SECOND_CHANGE = "who acts first is written down rather than assumed";

/** What the third step tells the author. */
export const THIRD_CHANGE = "a game may be spoken over while it is being played";

/** What the fourth step tells the author. */
export const FOURTH_CHANGE = "a company may be handed a weapon and not only an item";

/**
 * Four steps ending at the current version. Each does something a test can see
 * and each leaves a project the schema still accepts.
 *
 * Ending at the current version is the point rather than an accident: a chain
 * stopping short would leave a project declaring a version the bundled schema
 * refuses, and then the only thing a test could say is that the steps ran. So
 * this grows by one step every time the format does, which is the cheapest
 * possible reminder that a format change costs something.
 */
export const EXAMPLE_STEPS = [
  {
    from: EXAMPLE_OLDEST,
    to: "0.9.0",
    changed: FIRST_CHANGE,
    apply: (project) => {
      project.themeId ??= "temperate";
      return project;
    }
  },
  {
    from: "0.9.0",
    to: "1.0.0",
    changed: SECOND_CHANGE,
    apply: (project) => {
      project.defaultTurnOrder ??= "sideBlocks";
      return project;
    }
  },
  {
    from: "1.0.0",
    to: "1.1.0",
    changed: THIRD_CHANGE,
    apply: (project) => {
      project.notes ??= "";
      return project;
    }
  },
  {
    from: "1.1.0",
    to: "1.2.0",
    changed: FOURTH_CHANGE,
    apply: (project) => {
      project.contentRevision ??= "0.0.0";
      return project;
    }
  }
];

/**
 * A chain with a hole in the middle of it: a step out of `0.8.0` and a step out
 * of `1.0.0`, and nothing out of `0.9.0`.
 *
 * This is the shape a registry takes when somebody bumps the version and
 * forgets the step, and the whole point of walking one version at a time is
 * that it is caught and named rather than leapt over. A project at `0.8.0`
 * against these gets `missing_step` stopped at `0.9.0`, rather than a silent
 * skip to the far step or the far step run against the near step's fields.
 */
export const EXAMPLE_STEPS_WITH_A_HOLE = [
  {
    from: EXAMPLE_OLDEST,
    to: "0.9.0",
    changed: FIRST_CHANGE,
    apply: (project) => project
  },
  {
    from: "1.0.0",
    to: "1.1.0",
    changed: "a step whose turn never comes, because the chain stops short",
    apply: (project) => project
  }
];

/** A step that refuses. Its chain must leave the caller's project untouched. */
export const EXAMPLE_STEPS_THAT_THROW = [
  {
    from: EXAMPLE_OLDEST,
    to: "0.9.0",
    changed: FIRST_CHANGE,
    apply: (project) => {
      project.title = "half way";
      throw new Error("this project was not what the step expected");
    }
  },
  {
    from: "0.9.0",
    to: "1.0.0",
    changed: SECOND_CHANGE,
    apply: (project) => project
  }
];
