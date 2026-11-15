# README gameplay captures

The current comparison records the same real **Steel and Cedar** opening on
Godot, Nintendo 64 and PlayStation. [bell-orchard.json](bell-orchard.json)
identifies the one-player map, units, move and expected combat result.

Each player shows the complete 8×6 Bell Orchard board and its five units.
Rowan walks from (1, 2) to (4, 1), attacks the swordsman at (5, 1), deals six
damage, receives one damage from the counterattack and returns to the board.
Record ordinary production controls and preserve each player's actual timing.
The clips show the same exchange; they are not synchronized or sped up.

The README uses GIFs generated from those recordings. Keep build hashes and
probe logs with local release evidence so a clip can be traced to its build.

`scene.json` and `compose-renderer-comparison.sh` are an older isolated
cinematic fixture with fixed timing. They do not represent the campaign or
supply the current README captures.

[android-performance-06.json](android-performance-06.json) records 30-second
phone/tablet LAN samples at each graphics setting. These use the two-player
opening with eight units; they are separate from the one-player README clips.

[mipsync-followups-06.json](mipsync-followups-06.json) records the texture-safety
and projection follow-ups, matched PS1 frame comparisons and unchanged campaign
memory limits. Deepwood meets the strict frame-pacing gate; the heavier castle
scroll still has missed frames.

[ps1-compiler-06.json](ps1-compiler-06.json) compares renderer compiler settings
on the richer castle. `-O2` and `-Os` preserve the captures but run slower;
the production renderer keeps `-O3`.

For a PS1 movement review with measured display timing, run
`PSN00BSDK_ROOT=/path/to/sdk python3 tools/renderer_comparison/playstation_native_motion.py record BUILD OUTPUT`
against the two-player Bell Orchard probe build. It preserves the original
combat/audio checks, observes native display changes and records their timing.
The output includes a silent MP4, a 15 FPS GIF preview and capture provenance.
Add `--last-update 1000` to include the later ability and return; the default
window ends at update 480.
Original frame-pacing failures remain recorded; a valid movie is not a passed
performance gate. `encode CAPTURE OUTPUT` validates an existing trace before
encoding it. Current PS1 cadence and remaining misses are recorded in
[ps1-display-deadlines-06.json](ps1-display-deadlines-06.json). The N64 decoder
and its original movement capture are documented in
[n64-native-movement-06.json](n64-native-movement-06.json).
