# UI framework decision

Selected framework: **Vue 3 with TypeScript and Vite**.

All three candidates produced the identical editor-shell journey from a relative
static base and passed the checked-in shell/build contract. Production runtime
dependencies reported zero high/critical advisories. The raw report is
`results.raw.json`; repeated clean-build timing is informational rather than a
selection factor.

| Candidate | JS gzip | CSS gzip | Build runs (ms) | A11y | Testability | Maintenance | Bundle | Weighted |
|---|---:|---:|---|---:|---:|---:|---:|---:|
| React 19.2.8 | 59,748 | 293 | 375/367/376 | 4.0 | 4.0 | 4.75 | 3.0 | 4.04 |
| Vue 3.5.40 | 24,942 | 293 | 406/423/412 | 4.5 | 4.5 | 4.5 | 4.25 | 4.46 |
| Svelte 5.56.8 | 13,098 | 293 | 533/537/529 | 4.5 | 4.25 | 4.0 | 5.0 | 4.39 |

Scores use the frozen 35% accessibility, 25% testability, 25% maintenance, and
15% bundle rubric. All bundles pass the size gates, so the smallest bundle is not
automatically preferred.

Vue wins because it combines:

- first-class TypeScript and template checking
- unusually complete official accessibility guidance
- an official component-test utility and prescriptive unit/E2E guidance
- direct Vite/static deployment
- substantially less initial runtime code than the React candidate

Svelte's compiler accessibility warnings and smallest output are attractive, but
Vue currently provides the more conservative balance for a large, long-lived
authoring application. React has the broadest ecosystem and maintenance strength,
but the project does not yet need a React-only editor widget, and its official
core documentation leaves more testing/accessibility integration choices to the
application.

This decision covers UI components only. Domain, schema, project-store, command,
validation, map-model, and build-adapter code remain framework-neutral. If a
future required editor primitive is viable only in another framework, rerun this
same spike and record the evidence before changing the selection.

Full browser accessibility and focus tests remain required in the editor
scaffolding and browser-hardening tasks. The spike contract checks prove candidate
parity and static build shape; they do not replace assistive-technology testing.
