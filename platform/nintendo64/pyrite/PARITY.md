# Nintendo 64 renderer audit

This is the release-facing checklist for the sole Nintendo 64 player. A row
marked **ready** has both a real runtime path and a focused test or exact-4-MB
ares checkpoint.

| Release contract | Status | Evidence |
| --- | --- | --- |
| 1P and 2P launch | Ready | The packaged `choice` policy drives the launch screen. Each mode has its own SRAM slot, and the two controllers keep independent cursors and selection ownership. |
| Manual board actions | Ready | Move, Attack, every equipped Ability, usable carried items, Talk, Wait and Cancel enter the authoritative `Encounter` command boundary. Physical takeover leaves the review driver and opposing turns continue through the packaged tactics policy. |
| Story and battle moments | Ready | Campaign story nodes and packaged stage-open/fall/talk dialogue are loaded from the GPK and shown as advanceable framed dialogue. |
| Deployment | Ready | Authored tiles are marked on the live board. Each controller can select and place only its own characters through the shared simulation, and **Start** begins battle. Fixed formations skip this step. A focused rules test and the production ROM cross-build cover the path. |
| Maps, objectives and aftermath | Ready within the bounded export contract | The demo's one battle and Steel and Cedar's five battles load from their distinct packages, commit reports and reach their authored terminal nodes. |
| Authored hidden areas | Ready | The current exact-4-MB Steel ROM records Twin Gate's 18-cell room as closed, crosses its doorway through routed movement, reveals the room, and captures both states under real-RSP ares. |
| Save and resume | Ready | Project-derived 1P/2P slot identities keep games separate. A third settings slot preserves presentation choices. |
| Help and settings | Ready | Title Help plus in-battle Help cover the controls. Close view, reduced motion, audio and FPS display are persistent settings; FPS remains off by default. |
| Optional close presentation | Ready | Full, reduced, 3x and skip paths keep physical sides stable and restore the exact tactical camera/hash. |
| Audio | Ready baseline | Original deterministic ambience, UI, impact and victory cues are generated at build time and converted by the pinned libdragon toolchain. |
| Exact hardware-style budget | Ready for two projects | Distinct demo and Steel ROMs pass in real-RSP ares with Expansion Pak disabled and renderer identity `pyrite-tiny3d`. |
| Normal creator/export path | Ready | CMake, the creator payload and release builds all invoke the same generic source-to-ROM wrapper and record `fallback=none`. |
| General project/content coverage | Ready, bounded and fail-closed | The exporter derives package, campaign, saves, terrain atlas, close plate and character bindings from the selected project. It refuses ambiguous routes, unknown terrains, more than nine terrain families or more than 16 drawings instead of substituting demo content. |

The old Nintendo 64 renderer has been removed and Pyrite/Tiny3D is the only
runtime path. The broader 0.5 engine transition remains active until this
player reaches functional parity with Godot and passes the release campaign on
hardware. The export switch and deletion of the retired source, art,
generators and tests remain separate commits in history; the demo and Steel
and Cedar already pass the same exact-4-MB artifact gate.
