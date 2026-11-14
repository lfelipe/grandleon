# SPDX-License-Identifier: MIT
extends "steel_cedar_board_capture_test.gd"

func _run() -> void:
	var source := _argument("--source=")
	var reference := _argument("--reference-source=")
	if source.is_empty() or reference.is_empty():
		_fail("settlement checks require --source and --reference-source")
		quit(1)
		return
	for spec: Dictionary in MAPS:
		var before := await _load_real_battle(reference, spec)
		var after := await _load_real_battle(source, spec)
		if before.is_empty() or after.is_empty(): continue
		if String(spec.node) != "gate_village":
			_expect("%s preserves native terrain kinds and movement rules" % spec.node, after.terrain, before.terrain)
			_expect("%s preserves starting armies" % spec.node, after.snapshot.units, before.snapshot.units)
	var scenery = load("res://scripts/settlement_scenery.gd").new()
	var world := Node3D.new()
	root.add_child(world)
	for kind: String in ["abandoned_cart", "village_well", "watchtower", "walled_garden", "village_house"]:
		var prop: Node3D = scenery.build(world, kind, 0)
		_verify_footprint(prop, prop)
		prop.queue_free()
	world.queue_free()
	await process_frame
	if failures == 0: print("Settlement models fit their footprints; other encounters retain their rules and armies")
	quit(1 if failures else 0)

func _verify_footprint(node: Node, prop: Node3D) -> void:
	if node is MeshInstance3D:
		var bounds: AABB = prop.global_transform.affine_inverse() * node.global_transform * node.get_aabb()
		_expect_true("%s keeps geometry inside its blocked footprint" % prop.name,
			bounds.position.x >= -1.00001 and bounds.end.x <= 1.00001
			and bounds.position.z >= -1.00001 and bounds.end.z <= 1.00001)
	for child in node.get_children(): _verify_footprint(child, prop)
