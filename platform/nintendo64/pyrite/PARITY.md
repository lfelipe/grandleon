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
| Maps, objectives and aftermath | Ready for Steel and Cedar | All five packaged maps, objective snapshots, battle reports, aftermath and terminal progression complete through `CampaignSession`. |
| Save and resume | Ready for Steel and Cedar | Separate `steel_cedar_1p` and `steel_cedar_2p` slots preserve campaign progress. A third settings slot preserves presentation choices. |
| Help and settings | Ready | Title Help plus in-battle Help cover the controls. Close view, reduced motion, audio and FPS display are persistent settings; FPS remains off by default. |
| Optional close presentation | Ready | Full, reduced, 3x and skip paths keep physical sides stable and restore the exact tactical camera/hash. |
| Audio | Ready baseline | Original deterministic ambience, UI, impact and victory cues are generated at build time and converted by the pinned libdragon toolchain. |
| Exact hardware-style budget | Ready for this campaign | Fresh and resume runs pass in real-RSP ares with Expansion Pak disabled and the renderer identity set to `pyrite-tiny3d`. |
| Normal creator/export path | **Blocked** | `stage-inputs.sh` still names Steel and Cedar, its campaign ID, five map IDs and a small curated art set. A general exported project cannot yet build this renderer through the ordinary editor/export flow. |
| General project/content coverage | **Blocked** | Runtime presentation bindings and the close terrain plate do not yet cover arbitrary exported campaigns, styles, terrains, landmarks and unit identities. |

Cutover is therefore not authorized yet. The next architecture slice is a
generated project manifest that replaces the Steel-and-Cedar constants and
stages the exported package plus its complete presentation bindings. Only
after a second creator-authored project passes the same artifact and 4 MB gate
should the release/export target point at Pyrite and the old renderer be
removed.
