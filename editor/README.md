# Grandleon Editor

The editor is a static, local-first Vue 3 and TypeScript application. The UI
framework was selected through the checked-in comparison under
`spikes/ui-framework/`; domain, storage, validation, command, and build contracts
remain framework-neutral.

```sh
npm ci --prefix editor
npm run --prefix editor dev
```

The development server listens on all local-network interfaces and advertises
the machine hostname. Open
`http://<machine-name>.local:5173/` from this computer or another device on the
same network. Building a console image — a Nintendo 64 ROM or a PlayStation
disc — is the one thing that address cannot do: the build service answers only
a `localhost` address, whatever port the editor is on, because the machine's own
hostname is also what a peer on the network sends.

`--port` moves the server, and the advertised address, the HMR connection and
the printed line move with it:

```sh
npm run --prefix editor dev -- --port 4891
```

To serve the production build instead, which is what the browser suite and
every deployment check run against:

```sh
npm run --prefix editor build
npm run --prefix editor preview -- --port 4891
```

Pick a bundled sample and choose **Open this example** to open it immediately:
The Tarnholt Line, a six-Stage campaign with story nodes and a branch, or The
Bridge at Dawn, the maintained two-team conformance demo. Samples are bundled directly from their
canonical sources under `games/`, so loading works in offline and sub-path
deployments and cannot drift from what the native builds compile. Loading
creates an unsaved working copy: use **Save** for browser recovery storage or
**Export** for a portable archive.

**Play** opens a full-screen surface without editor chrome: the player steers
the first side, the other side acts from the behaviour authored on each
placement, and a visible control returns to editing.

Play runs the **campaign you wrote**, not a walk through its Stages. The
company persists from one Stage to the next, and pressing Play exercises every
campaign rule an author can write:

- **Permadeath.** A character who falls is gone. The next Stage's board comes
  through the roster, and the surface names who could not take the field,
  however plainly the map still lists them.
- **Experience and levels.** Defeating a character grants the experience its unit
  type is worth to whoever felled it, and the screen between Stages states who
  earned what and who reached which level.
- **Growth rates.** A level-up rolls each stat's authored chance once, and the
  screen states what each level actually granted, per stat. That is the number an
  author needs to balance a growth block, and it is the roll the compiled game
  will make, not an average of it.
- **Drops.** What a defeated character left behind goes into the company's
  supplies, and what was drunk during the Stage comes out of them; both are
  listed.
- **Where the story went.** The next chapter's authored name, or the reason the
  campaign could not move on.

Every number on that screen is derived by the engine (`platform/client`'s
campaign session, compiled to WebAssembly) and read by the editor. The
information sheet ("About them") gains a LEVEL and EXPERIENCE row for characters
the campaign holds, the same row the two consoles and the terminal draw.

The campaign is written to a save slot after each Stage, so **Pick up where I
left off** resumes a playtest you left. The slot lives in the WebAssembly
module's memory: it survives leaving Play and coming back, and it does not
survive reloading the page. **Start over** founds a fresh company. A game with
more than one campaign is offered a choice of which to play.

The first roster is the campaign's authored `roster`, and members a node
`recruits` join in that node's own completion batch. These are the rules the
compiled game runs.

The **Browser playtest** panel under **Diagnostics** runs the first Stage of
the current, potentially unsaved project. It reports on the game rather than
configuring it. The engine's own canonical hash, printed beside the board, is
why it sits beside the validation problems and the console budgets rather than
on the page an author lands on. Select a unit on the active side and use Move,
Attack, or Wait; legal squares are highlighted, HP and turn state update after
each activation, and a victory follows the Stage's unconditional campaign
transition to a terminal node. Both Play and the playtest panel run the
authoritative portable C++ simulation compiled to WebAssembly (see
`platform/web/README.md`), not a JavaScript reimplementation, and the canonical
hash they report is checked against the native suite. Restart the Stage after
editing its map, placements, unit type, class stats, or campaign flow.

The Stage is rendered as a responsive SVG tactical board: terrain and units
are `<image>` elements drawn from the generated art sheets with nearest-neighbour
scaling, over a colour underlay that also drives the labels, plus health bars,
movement markers, and attack targets. A custom terrain name that matches no
art-library keyword still receives a deterministic non-black colour, so an
unrecognised name is visible rather than invisible. Equivalent keyboard and
screen-reader controls are provided beside the board.

The map editor draws the same autotiled terrain sheets through the same lookup
and in the project's own theme, behind the cell's button, with the colour
underlay beneath it and a glyph per terrain kind above it: a terrain the art
library has no sheet for has no sprite to fall back on, and the mark is what
keeps terrain from being carried by colour alone.

## The disciplines this editor holds itself to

These are requirements, not habits. A change that breaks one of them is a
regression even if every test still passes.

**A refusal names what is wrong and what would fix it.** Deleting a referenced
record does not report an error code: `describeReferences` in
`ContentWorkspace.vue` turns a path like `/unitTypes/3/classId` into "Kestrel
(the character)", and the message names the edits that would let the delete
through. Rename previews every affected location before it touches one. The
rule generalises: a surface that says no owes the author the sentence that
turns the no into a yes.

**An author cannot lose typing by clicking away.** `leaveEditors` commits open
drafts and is called by every section change, collection change and record
selection; Save flushes drafts before persisting; unsaved work survives every
road out of the workspace and warns before the tab closes. A stored draft that
no longer decodes is never overwritten. The raw bytes are held, Save is paused,
and the author is offered download-then-discard, because a broken draft is still
the only copy of somebody's work.

**Accessibility is not a coat of paint.** The catalogue shelves are radio groups
with a roving tabindex and arrow keys. The map grid is one tab stop with
arrow-key movement, and so is the play board. Play makes the editor beneath it
`inert`. Terrain carries a glyph as well as a colour, precisely so that no state
is signalled by colour alone. Every board surface an author can reach with a
mouse is reachable with a keyboard and describable to a screen reader, and a new
one that is not is not finished.

**The vocabulary is the author's, not the schema's.** One word per concept,
chosen for a person who has never read the source format. A unit type is a
character, a dialogue is a scene, an encounter node is a **Stage**. That rule
has exactly one home, `src/domain/author-words.ts`, and `nodeKindWord` there is
the only place a stored keyword becomes an author's word: the surface says
Stage while the format keeps `encounter`, because the node kind is a value
inside every authored file and renaming it would rewrite content and move
package bytes. The section hints use those words and so do the refusals.

**Drawing ground and setting up a fight are two entries on the rail.** A map is
terrain, a name and a size; it holds nobody. A **Stage** is a fight on a map:
the ground it uses, who stands where, what is said on the way in, what winning
means, who joins the company and what it is given. One map can be fought over
as many times as an author likes, so a Stage points at a map and never the
other way round. **Stages** is the one place a Stage is set up; **Maps** lists
the Stages fought on the open map only as a way to reach them; **Flow** strings
Stages together and does not open the board. Two doors onto one node would be
two places it could disagree with itself.

**The first-Stage checklist is computed from the project, not stencilled.** Its
four steps (make a character, draw a map, make a Stage with both sides placed,
press Play) are each checked by looking for the thing itself, so the third step
asks whether an encounter node really has a placement on each side rather than
counting campaigns. A checklist that can be satisfied without the work being
done is a decoration.

If another device cannot resolve the hostname, use the LAN IP printed by Vite.
The advertised hostname and port can be overridden from the environment as
well, which is what a script that starts the server wants:

```sh
GRANDLEON_EDITOR_HOSTNAME=editor.local \
GRANDLEON_EDITOR_PORT=4173 \
npm run --prefix editor dev
```

`--port` on the command line wins over `GRANDLEON_EDITOR_PORT`, and a value
that is not a port number is refused rather than ignored.

Run the complete editor checks with:

```sh
npm run --prefix editor typecheck
npm test --prefix editor
npm run --prefix editor build
npm run --prefix editor test:startup
npm run --prefix editor test:deployment
npm run --prefix editor test:offline
npm run --prefix editor test:browser
npm run --prefix editor test:dev
```

`test:browser` drives the production build in a real Chromium profile and
needs the one-time browser install described in `CODING.md`. `test:dev` needs
the same Chromium and is the only check that loads the **development** server:
it starts one on `GRANDLEON_EDITOR_DEV_PORT` (4188 by default), reads a
computed colour off the page to prove the stylesheets applied, and fails on any
console error, Content Security Policy refusal included. The rest run without a
browser install.

The document policy in `index.html` is the one a deployment is served under,
and Vite in development hands every stylesheet to the page as an inline
`<style>` element, which that policy blocks. `vite.config.ts` adds
`'unsafe-inline'` to `style-src` for the dev server alone, through a plugin
declared `apply: "serve"`, so `dist/index.html` carries the strict policy
unchanged.

Set `GRANDLEON_EDITOR_BASE` when deploying under a sub-path:

```sh
GRANDLEON_EDITOR_BASE=/tools/grandleon/ npm run --prefix editor build
```

The production output is `editor/dist/` and contains static assets only.
Projects can be recovered from local IndexedDB and imported or exported as
portable archives. Browser storage is recovery storage, not a substitute for
keeping exported canonical sources under version control.

The startup smoke test loads the production module in a browser DOM, verifies
that the editor mounts, loads the demo, runs its first Stage, and checks that
every terrain and unit token has an explicit non-black fill. It also rejects
eval, `Function` constructor, and CommonJS `require` calls in built browser
scripts because the production CSP does not permit runtime code generation.
The deployment smoke test runs that startup gate for both `/` and
`/tools/grandleon/`, fetches every referenced asset, verifies content-hashed
immutable assets and a non-cached entry document, and checks the baseline CSP.
Production hosts should also send the CSP as an HTTP header and add
`frame-ancestors 'none'`, which cannot be enforced from a document `<meta>`
policy.

The production build registers a base-path-scoped service worker and ships a web
app manifest. The service worker precaches the generated shell and same-origin
entry assets, serves a cached shell for offline navigation, and never intercepts
non-GET or cross-origin requests. Browser storage is still recovery storage;
offline support does not remove the need to export canonical projects.
