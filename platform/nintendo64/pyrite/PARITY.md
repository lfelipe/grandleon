# Pyrite cutover audit

This is the release-facing checklist for replacing the current Nintendo 64
presenter. A row marked **ready** has both a real runtime path and a focused
test or exact-4-MB ares checkpoint. A row marked **blocked** prevents cutover;
the existing renderer must remain the release target until it is cleared.

| Release contract | Status | Evidence |
| --- | --- | --- |
| 1P and 2P launch | Ready | The packaged `choice` policy drives the launch screen. Each mode has its own SRAM slot, and the two controllers keep independent cursors and selection ownership. |
| Manual board actions | Ready | Move, Attack, every equipped Ability, Wait and Cancel enter the authoritative `Encounter` command boundary. Physical takeover leaves the review driver and opposing turns continue through the packaged tactics policy. |
| Story and battle moments | Ready | Campaign story nodes and packaged stage-open/fall/talk dialogue are loaded from the GPK and shown as advanceable framed dialogue. |
| Deployment | Ready | The screen appears only when the packaged encounter authors deployment; fixed formations skip it. |
| Maps, objectives and aftermath | Ready within the bounded export contract | The demo's one battle and Steel and Cedar's five battles load from their distinct packages, commit reports and reach their authored terminal nodes. |
| Save and resume | Ready | Project-derived 1P/2P slot identities keep games separate. A third settings slot preserves presentation choices. |
| Help and settings | Ready | Title Help plus in-battle Help cover the controls. Close view, reduced motion, audio and FPS display are persistent settings; FPS remains off by default. |
| Optional close presentation | Ready | Full, reduced, 3x and skip paths keep physical sides stable and restore the exact tactical camera/hash. |
| Audio | Ready baseline | Original deterministic ambience, UI, impact and victory cues are generated at build time and converted by the pinned libdragon toolchain. |
| Exact hardware-style budget | Ready for two projects | Distinct demo and Steel ROMs pass in real-RSP ares with Expansion Pak disabled and renderer identity `pyrite-tiny3d`. |
| Normal creator/export path | **Blocked** | The generic source-to-ROM wrapper is ready, but the creator's normal Nintendo 64 export button and release artifact still invoke the current renderer. |
| General project/content coverage | Ready, bounded and fail-closed | The exporter derives package, campaign, saves, terrain atlas, close plate and character bindings from the selected project. It refuses ambiguous routes, unknown terrains, more than nine terrain families or more than 16 drawings instead of substituting demo content. |

Cutover is therefore not authorized yet. Both source projects have passed the
same artifact and exact-4-MB gate; the remaining blocker is connecting this
generic wrapper to the normal creator/export contract. That switch and removal
of the old renderer must remain separate, reviewable commits.
