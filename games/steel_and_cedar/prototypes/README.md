# Larger castle study

**The Three Courts** is a playable 32×24 map for reviewing larger interiors.
It has a western guardhouse, an open connecting court, a northern council
chamber and a larger great hall. Rowan starts near the guardhouse in solo play;
co-op adds Akari's southern approach and extra opposition.

The rooms start covered and uncover as friendly units enter and explore.
The courtyard stays visible. The objective is to defeat Masatora without
losing Rowan. This is a layout study; its difficulty has not been tuned.

Generate the editable source from the repository root:

```sh
python3 games/steel_and_cedar/prototypes/castle_expansion.py
```

It writes `out/castle-expansion06/project.json`. Import the generated editable
archive into **Create / Edit Games**, or open its playable campaign bundle in
the Godot player. The review test below produces both archives and screenshots:

```sh
platform/godot/scripts/check-castle-expansion.sh
```

In **Map & Battle → Interior Visibility → Cover a rectangle**, select two
opposite corners to mark a room in one edit. Repeat a fully covered rectangle
to uncover it, or use individual cells to refine an edge. Each room can be undone
with one press.

This prototype does not replace Steel and Cedar. It now runs on all three
players, with normal 1P/2P console menus, scrolling and local room reveals.
The console pieces use the same wall, doorway and stone roof builders as Godot.
The N64 exporter supports up to 768 cells; console asset and memory limits
still apply. Unsupported exports report the limit rather than shrinking a map.

The normal N64 ROM is checked in base 4 MiB RAM, and the normal PS1 disc retains
its audio, stack and gameplay-heap reserves in 2 MiB. These are emulator checks;
PS1 frame pacing, physical-console review and encounter balancing remain open.

To build your own rooms, the Map Editor palette includes **Castle Wall**,
**Castle Floor**, **Castle Doorway** and **Castle Dais**. Paint two doorway
cells next to each other vertically to form an arch. Use **Interior Visibility**
and its rectangle tool to cover each room; play the draft to check the reveal.
