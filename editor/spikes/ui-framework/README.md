# UI framework selection spike

**This is a completed evaluation, not live code.** Nothing here is built,
tested, or shipped by the repository; the three prototypes are kept because they
are what makes the scored comparison reproducible. The result is
[`DECISION.md`](DECISION.md), and the editor that came out of it is
`editor/`.

The protocol below was frozen before any candidate was implemented, which is
what makes the scores comparable. It compares React, Vue, and Svelte for the
statically deployed Grandleon Editor. It does not compare starter templates. Each candidate must implement the
same small editor shell, use the same fixture and CSS intent, and pass the same
production-build and interaction tests.

## Identical candidate surface

Each candidate implements:

- a skip link and semantic header with product/project name
- an unsaved-changes status
- project navigation for Content, Maps, and Diagnostics
- a labeled text input and native select
- an accessible validation summary linked to an invalid field
- Save with an `aria-live` result
- a keyboard-opened modal with initial focus, focus containment, Escape close,
  and focus restoration

No candidate may add a router, state library, component kit, icon library,
network client, PWA layer, or framework-specific optimization unavailable to the
others. Framework runtime code is included in measurements.

## Reproducibility controls

- One npm workspace with exact direct dependency versions and a committed lockfile
- Pinned Node major and common Vite, TypeScript, minifier, target, base-path,
  sourcemap, CSS, fixture, and lazy-loading policy
- Strict type checking plus identical component and shared browser scenarios
- Production builds run three times in interleaved order on one recorded machine
- Raw measurements retained in machine-readable JSON; medians are derived output
- Test/build commands operate from a clean checkout with `npm ci`

## Hard gates

Every candidate must:

- pass type, component, keyboard/focus, semantic-name, and shared end-to-end tests
- produce zero serious/critical automated accessibility violations
- work from a configured non-root static base path
- have zero high/critical production dependency advisories
- emit no more than 75 KiB gzip initial JavaScript and 90 KiB gzip combined
  initial JavaScript/CSS for the fixed shell

Candidates failing a gate are not scored until the failure is corrected or
documented as an architectural rejection.

## Measurements and rubric

Record raw/gzip/Brotli initial JS and CSS bytes, initial requests, three clean
build times, test times, candidate-only source lines/files, exact production
dependency count, advisory counts, tool versions, OS, CPU, and date.

Passing candidates receive a documented 0–5 score:

| Criterion | Weight | Evidence |
|---|---:|---|
| Accessibility | 35% | automated/manual flows, compiler guidance, ease of correct semantics |
| Testability | 25% | shared results, isolation, diagnostics, official test guidance |
| Maintenance | 25% | official stability/versioning and maintained TypeScript/tooling guidance |
| Bundle | 15% | normalized gzip initial JS+CSS within the accepted range |

If weighted scores differ by less than 0.25/5, React remains the baseline unless
another candidate shows a material accessibility or maintenance advantage.
Bundle size alone never selects the framework.

## Primary-source evidence

The qualitative half of the score rests on these, each read from the framework's
own documentation rather than from a comparison article:

- React supports TypeScript/TSX and Vite static SPAs, and exposes native
  HTML/ARIA primitives, but deliberately leaves test-stack selection to the
  ecosystem:
  [TypeScript](https://react.dev/learn/typescript),
  [from-scratch setup](https://react.dev/learn/build-a-react-app-from-scratch),
  [DOM attributes](https://react.dev/reference/react-dom/components/common).
- Vue provides first-class TypeScript, official Vite scaffolding, unusually
  complete official accessibility guidance, and prescriptive official
  unit/component/E2E testing guidance:
  [TypeScript](https://vuejs.org/guide/typescript/overview),
  [accessibility](https://vuejs.org/guide/best-practices/accessibility.html),
  [testing](https://vuejs.org/guide/scaling-up/testing.html).
- Svelte provides TypeScript, official testing guidance, static deployment, and
  the strongest built-in accessibility compiler diagnostics:
  [TypeScript](https://svelte.dev/docs/svelte/typescript),
  [testing](https://svelte.dev/docs/svelte/testing),
  [compiler warnings](https://svelte.dev/docs/svelte/compiler-warnings),
  [static adapter](https://svelte.dev/docs/kit/adapter-static).

Vue led the qualitative half because its official accessibility, TypeScript,
and testing guidance are the most balanced for a long-lived authoring tool; the
quantitative report and the final selection are in
[`DECISION.md`](DECISION.md).
