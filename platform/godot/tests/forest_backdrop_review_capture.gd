# SPDX-License-Identifier: MIT
extends SceneTree

const ArtLibrary = preload("res://scripts/art_library.gd")
const BackdropPackResolver = preload("res://scripts/conversation_backdrop_resolver.gd")
const CombatCinematic = preload("res://scripts/combat_cinematic.gd")
const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")
const ConversationBackdrop = preload("res://scripts/conversation_backdrop.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const CAPTURE_SIZE := Vector2i(1280, 720)


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var output_dir := _argument("--output-dir=")
	if output_dir.is_empty():
		push_error("forest review capture requires --output-dir=<path>")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	get_root().size = CAPTURE_SIZE
	var selection := _forest_selection()
	if String(selection.get("pack_id", "")) != "medieval_forest_gate":
		push_error("forest review capture did not select the production pack")
		quit(1)
		return
	if not await _capture_conversation(
		selection, output_dir.path_join("cedar-deepwood-conversation.png")
	):
		quit(1)
		return
	if not await _capture_combat(
		output_dir.path_join("cedar-deepwood-combat.png")
	):
		quit(1)
		return
	print("captured reviewed Cedar Deepwood conversation and combat: " + output_dir)
	quit()


func _forest_selection() -> Dictionary:
	var snapshot := {
		"width": 6,
		"height": 4,
		"units": [{
			"id": 10, "unit_type_id": 101, "x": 2, "y": 2,
			"health": 12, "on_board": true,
		}],
	}
	var terrain: Array = [
		2, 2, 9, 12, 12, 2,
		2, 9, 9, 12, 12, 2,
		2, 9, 9, 12, 9, 2,
		2, 2, 9, 12, 2, 2,
	]
	return BackdropPackResolver.new().resolve_for_conversation(
		"cedar_deepwood_opening", 0, true, snapshot, terrain, [101]
	)


func _capture_conversation(selection: Dictionary, output: String) -> bool:
	var stage := Control.new()
	stage.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	get_root().add_child(stage)
	var backdrop = ConversationBackdrop.new()
	stage.add_child(backdrop)
	if not backdrop.present(selection):
		push_error("review conversation could not present the forest pack")
		stage.queue_free()
		return false
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.004, 0.008, 0.014, 0.16)
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	stage.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(112, 453)
	panel.size = Vector2(1054, 200)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color(0.018, 0.032, 0.052, 0.98), Color(0.30, 0.76, 0.92, 0.55))
	)
	stage.add_child(panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 19)
	panel.add_child(row)
	var portrait := TextureRect.new()
	portrait.custom_minimum_size = Vector2(147, 147)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	portrait.texture = _rowan_portrait()
	row.add_child(portrait)
	var words := VBoxContainer.new()
	words.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(words)
	var speaker := Label.new()
	speaker.text = "Rowan"
	speaker.add_theme_font_size_override("font_size", 18)
	speaker.add_theme_color_override("font_color", Color("69d9ff"))
	words.add_child(speaker)
	var line := RichTextLabel.new()
	line.bbcode_enabled = true
	line.size_flags_vertical = Control.SIZE_EXPAND_FILL
	line.add_theme_font_size_override("normal_font_size", 17)
	line.add_theme_color_override("default_color", Color("e9f0f4"))
	line.text = "We clear the near ambush, use the rock line as cover, and reach the eastern courier."
	words.add_child(line)
	var hint := Label.new()
	hint.text = "A / ENTER  CONTINUE"
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	hint.add_theme_font_size_override("font_size", 10)
	hint.add_theme_color_override("font_color", Color("89a8b8"))
	words.add_child(hint)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var saved := get_root().get_viewport().get_texture().get_image().save_png(output)
	stage.queue_free()
	await process_frame
	if saved != OK:
		push_error("could not save conversation capture: " + output)
		return false
	return true


func _capture_combat(output: String) -> bool:
	var before := _combat_snapshot(12)
	var after := _combat_snapshot(7)
	var events := [{
		"kind": "unit_damaged", "unit_id": 20,
		"related_unit_id": 10, "amount": 5,
	}]
	var plan := CombatPresentationPlan.build(before, after, events, "attack")
	var terrain := [2, 2, 12, 9, 2, 2, 12, 9, 2, 2, 9, 9]
	var cinematic = CombatCinematic.new()
	get_root().add_child(cinematic)
	cinematic.configure(
		GraphicsProfiles.new().profile("voxel-sprites"), 1.0, "reduced", false
	)
	if not cinematic.enqueue(
		plan, terrain, 0, [Vector2(350, 450), Vector2(930, 450)]
	):
		push_error("review combat could not enqueue its production exchange")
		cinematic.queue_free()
		return false
	cinematic._process(0.35)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var saved := get_root().get_viewport().get_texture().get_image().save_png(output)
	cinematic.queue_free()
	await process_frame
	if saved != OK:
		push_error("could not save combat capture: " + output)
		return false
	return true


func _rowan_portrait() -> Texture2D:
	var art = ArtLibrary.new()
	art.configure_profile(GraphicsProfiles.new().profile("voxel-sprites"))
	var frame: Dictionary = art.character_frame({
		"archetype": 5, "colour": 0, "style": 0, "figure": 0,
	}, "idle")
	var atlas := AtlasTexture.new()
	atlas.atlas = art.character_frame_texture(frame)
	atlas.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
	return atlas


func _combat_snapshot(target_health: int) -> Dictionary:
	return {
		"width": 4,
		"height": 3,
		"units": [
			{
				"id": 10, "name": "Commander Rowan Vale", "side": "first",
				"player": 1, "x": 0, "y": 1, "health": 14,
				"maximum_health": 14, "on_board": true,
				"archetype": 5, "colour": 0, "style": 0, "figure": 0,
			},
			{
				"id": 20, "name": "March Sword", "side": "second",
				"player": 0, "x": 1, "y": 1, "health": target_health,
				"maximum_health": 12, "on_board": target_health > 0,
				"archetype": 0, "colour": 1, "style": 0, "figure": 1,
			},
		],
	}


func _panel(fill: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = border
	box.set_border_width_all(1)
	box.set_corner_radius_all(14)
	box.content_margin_left = 18
	box.content_margin_right = 18
	box.content_margin_top = 14
	box.content_margin_bottom = 14
	return box


func _argument(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""
