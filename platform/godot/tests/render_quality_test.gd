# SPDX-License-Identifier: MIT
extends SceneTree

var failed := false

func _init() -> void: call_deferred("run")

func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)

func run() -> void:
	var script = load("res://scripts/render_quality.gd")
	check(script.recommend(true, "Adreno (TM) 610", 4 * 1024 * 1024 * 1024) == "low", "entry-level phone defaults Low")
	check(script.recommend(true, "Mali-G615 MC2", 6 * 1024 * 1024 * 1024) == "medium", "tablet defaults Medium")
	check(script.recommend(false, "unknown", 0) == "high", "desktop keeps High")
	check(script.recommend(true, "unknown", 12 * 1024 * 1024 * 1024) == "medium", "RAM alone never certifies High")
	var quality: Node = root.get_node("RenderQuality")
	quality.settings_path = "user://quality-test-%d.cfg" % OS.get_process_id()
	var board = load("res://scripts/board_view_3d.gd").new()
	root.add_child(board)
	await process_frame
	var world = board._world
	var camera = board._camera
	var ui_size: Vector2 = board.size
	for value: String in ["low", "medium", "high"]:
		var selector: OptionButton = quality.make_selector(44)
		root.add_child(selector)
		selector.item_selected.emit(quality.CHOICES.find(value))
		await process_frame
		check(quality.choice == value, "selector applies " + value)
		check(is_equal_approx(board._viewport.scaling_3d_scale, float(script.profile(value).scale)), "3D sampling changes live")
		check(board._key_light.shadow_enabled == bool(script.profile(value).shadows), "shadows match preset")
		check(board._world == world and board._camera == camera and board.size == ui_size, "quality preserves world, camera and UI coordinates")
		var reloaded: Node = script.new()
		reloaded.settings_path = quality.settings_path
		root.add_child(reloaded)
		check(reloaded.choice == value, "manual choice survives restart")
		reloaded.queue_free()
		selector.queue_free()
	check(not quality.select("invalid") and quality.choice == "high", "invalid choice keeps current settings")
	check(quality.select("auto") and quality.effective() == quality.recommended, "automatic selection can be restored")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(quality.settings_path))
	board.queue_free()
	await process_frame
	print("Render quality selection, persistence and live rendering checks passed" if not failed else "Render quality checks failed")
	quit(1 if failed else 0)
