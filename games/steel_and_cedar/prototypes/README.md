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

This prototype does not replace Steel and Cedar. Console export needs further
work: the N64 currently reserves space for 280 cells, and both console castle
paths assume the existing six-by-six keep. The prototype has 768 cells and
170 covered floor cells. Keep those limits visible rather than silently
exporting a smaller map or different renderer. The N64 exporter now reports
its current cell limit before generating cartridge assets, so an unsupported
layout does not produce a ROM that fails after booting.
