# SPDX-License-Identifier: MIT
extends SceneTree

const BackdropPackResolver = preload("res://scripts/conversation_backdrop_resolver.gd")
const ConversationBackdrop = preload("res://scripts/conversation_backdrop.gd")
const CAPTURE_SIZE := Vector2i(1280, 720)


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var output_dir := _argument("--output-dir=")
	if output_dir.is_empty():
		push_error("Steel and Cedar backdrop capture requires --output-dir=<path>")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	get_root().size = CAPTURE_SIZE
	var snapshot := {
		"width": 6,
		"height": 4,
		"units": [{
			"id": 10, "unit_type_id": 101, "x": 2, "y": 2,
			"health": 12, "on_board": true,
		}],
	}
	var resolver = BackdropPackResolver.new()
	var captures := [
		{
			"name": "bell-orchard-production-backdrop.png",
			"scene": "orchard_orders",
			"pack": "medieval_bell_orchard",
			"terrain": 10,
			"feature": "orchard_tree",
			"route": {
				"stage_id": "935b03cf7490233c",
				"map_id": "935b03cf7490233c",
				"encounter_id": "4c67d9e3fd41410e",
			},
		},
		{
			"name": "cedar-watch-production-backdrop.png",
			"scene": "watch_orders",
			"pack": "medieval_cedar_watch",
			"terrain": 12,
			"feature": "watch_ruin",
			"route": {
				"stage_id": "0079abb4797ff5b4",
				"map_id": "0079abb4797ff5b4",
				"encounter_id": "a91cac5b07854756",
			},
		},
	]
	for capture: Dictionary in captures:
		var terrain: Array = []
		terrain.resize(24)
		terrain.fill(int(capture.terrain))
		var features: Array = []
		features.resize(24)
		features.fill("")
		features[14] = String(capture.feature)
		var selection: Dictionary = resolver.resolve_for_conversation(
			capture.scene, 0, true, snapshot, terrain, [101], 0,
			features, capture.route
		)
		if String(selection.get("pack_id", "")) != String(capture.pack):
			push_error("production resolver did not select %s" % capture.pack)
			quit(1)
			return
		if not await _capture(selection, output_dir.path_join(String(capture.name))):
			quit(1)
			return
	print("captured Steel and Cedar production-resolver backdrops: " + output_dir)
	quit()


func _capture(selection: Dictionary, output: String) -> bool:
	var stage := Control.new()
	stage.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	get_root().add_child(stage)
	var backdrop = ConversationBackdrop.new()
	stage.add_child(backdrop)
	if not backdrop.present(selection):
		push_error("resolved backdrop could not be presented: " + output)
		stage.queue_free()
		return false
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var error := get_root().get_viewport().get_texture().get_image().save_png(output)
	stage.queue_free()
	await process_frame
	if error != OK:
		push_error("could not save production backdrop capture: " + output)
		return false
	return true


func _argument(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""
