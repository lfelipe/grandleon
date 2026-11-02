# SPDX-License-Identifier: MIT
extends Control

const ArtLibrary = preload("res://scripts/art_library.gd")
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const TILE_SOURCE := 32
const BLOB_COLUMNS := 8
const HISTORY_LIMIT := 64
const TERRAIN_PALETTE := [
	"grass", "forest", "mountain", "water", "road", "bridge", "sand",
	"ruins", "farmland", "snow", "swamp", "hills", "bamboo", "paved",
]

var library
var compiler
var art = ArtLibrary.new()
var project_id := ""
var source := ""
var project: Dictionary = {}
var maps: Array = []
var map_index := 0
var selected_terrain := "grass"
var active_tool := "brush"
var rectangle_anchor := -1
var dirty := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0
var resize_width := 1
var resize_height := 1
var resize_preview: Dictionary = {}

var title_label: Label
var map_select: OptionButton
var tool_buttons: Dictionary = {}
var palette_list: VBoxContainer
var palette_buttons: Dictionary = {}
var grid: GridContainer
var empty_label: Label
var status_label: Label
var save_button: Button
var undo_button: Button
var redo_button: Button
var play_button: Button
var resize_layer: Control
var resize_width_label: Label
var resize_height_label: Label
var resize_fill_label: Label
var resize_preview_label: Label
var resize_apply_button: Button
var resize_first_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if resize_layer != null and resize_layer.visible:
			_close_resize()
		else:
			_request_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status("NO PROJECT SELECTED", true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status("OPEN FAILED  /  %s" % loaded.message, true)
		return
	source = loaded.source
	if not _read_source():
		return
	title_label.text = "%s  /  MAP CREATOR" % str(project.get("title", "UNTITLED GAME")).to_upper()
	_refresh_maps()
	_set_status("READY  /  CHOOSE TERRAIN, THEN PAINT THE BOARD", false)
	map_select.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var map_value: Variant = project.get("maps", [])
	maps = map_value if map_value is Array else []
	return true


func _refresh_maps() -> void:
	map_select.clear()
	for value: Variant in maps:
		if value is Dictionary:
			map_select.add_item(str(value.get("name", value.get("id", "Untitled map"))))
	map_index = clampi(map_index, 0, maxi(0, maps.size() - 1))
	if not maps.is_empty():
		map_select.select(map_index)
	_refresh_palette()
	_refresh_grid()
	_update_actions()


func _select_map(index: int) -> void:
	if index < 0 or index >= maps.size():
		return
	map_index = index
	rectangle_anchor = -1
	_refresh_palette()
	_refresh_grid(0)
	_set_status("MAP  /  %s" % _current_map_name().to_upper(), false)


func _current_map() -> Dictionary:
	if map_index < 0 or map_index >= maps.size() or not maps[map_index] is Dictionary:
		return {}
	return maps[map_index]


func _current_map_name() -> String:
	var map := _current_map()
	return str(map.get("name", map.get("id", "Untitled map")))


func _refresh_palette() -> void:
	for child in palette_list.get_children():
		palette_list.remove_child(child)
		child.queue_free()
	palette_buttons.clear()
	var names: Array[String] = []
	for value: String in TERRAIN_PALETTE:
		names.append(value)
	var map := _current_map()
	for value: Variant in map.get("terrain", []):
		var name := str(value)
		if not name.is_empty() and not names.has(name):
			names.append(name)
	if not names.has(selected_terrain) and not names.is_empty():
		selected_terrain = names[0]
	for name: String in names:
		var button := _button(name.to_upper(), 330, 54)
		button.icon = _terrain_icon(name, 0)
		button.tooltip_text = "Paint %s" % name
		button.pressed.connect(_select_terrain.bind(name))
		palette_list.add_child(button)
		palette_buttons[name] = button
	_update_palette_selection()


func _select_terrain(name: String) -> void:
	selected_terrain = name
	_update_palette_selection()
	_set_status("TERRAIN  /  %s" % name.to_upper(), false)
	_focus_cell(0)


func _select_tool(name: String) -> void:
	active_tool = name
	rectangle_anchor = -1
	_update_tool_selection()
	_set_status("TOOL  /  %s" % name.to_upper(), false)
	_focus_cell(0)


func _update_tool_selection() -> void:
	for name: String in tool_buttons:
		var button: Button = tool_buttons[name]
		button.text = ("● " if name == active_tool else "  ") + name.to_upper()


func _update_palette_selection() -> void:
	for name: String in palette_buttons:
		var button: Button = palette_buttons[name]
		button.text = ("● " if name == selected_terrain else "  ") + name.to_upper()


func _refresh_grid(focus_index := -1) -> void:
	for child in grid.get_children():
		grid.remove_child(child)
		child.queue_free()
	var map := _current_map()
	empty_label.visible = map.is_empty()
	grid.visible = not map.is_empty()
	if map.is_empty():
		empty_label.text = "This project has no maps yet.\nChoose NEW MAP to begin creating it."
		return
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	var terrain: Array = map.get("terrain", [])
	grid.columns = width
	var tile_size := clampi(floori(minf(830.0 / width, 620.0 / height)), 46, 96)
	for index in width * height:
		var name := str(terrain[index]) if index < terrain.size() else "grass"
		var cell := _button("", tile_size, tile_size)
		cell.icon = _terrain_icon(name, _terrain_mask(index, width, height, terrain))
		cell.expand_icon = true
		cell.tooltip_text = "%d, %d  /  %s" % [index % width, index / width, name]
		cell.pressed.connect(_paint_cell.bind(index))
		grid.add_child(cell)
	if focus_index >= 0:
		call_deferred("_focus_cell", focus_index)


func _terrain_mask(index: int, width: int, height: int, terrain: Array) -> int:
	if index < 0 or index >= terrain.size():
		return 0
	var kind := art.terrain_kind_for_source(str(terrain[index]))
	var x := index % width
	var y := index / width
	var samples := [
		Vector3i(0, -1, 1), Vector3i(1, -1, 2), Vector3i(1, 0, 4),
		Vector3i(1, 1, 8), Vector3i(0, 1, 16), Vector3i(-1, 1, 32),
		Vector3i(-1, 0, 64), Vector3i(-1, -1, 128),
	]
	var mask := 0
	for sample: Vector3i in samples:
		var nx := x + sample.x
		var ny := y + sample.y
		if nx < 0 or ny < 0 or nx >= width or ny >= height:
			continue
		var neighbour := ny * width + nx
		if neighbour < terrain.size() and art.terrain_kind_for_source(str(terrain[neighbour])) == kind:
			mask |= sample.z
	return mask


func _terrain_icon(name: String, mask: int) -> AtlasTexture:
	var kind := art.terrain_kind_for_source(name)
	var variant := art.terrain_variant(mask)
	var icon := AtlasTexture.new()
	icon.atlas = art.terrain_texture(kind, 0)
	icon.region = Rect2(
		float(variant % BLOB_COLUMNS) * TILE_SOURCE,
		float(variant / BLOB_COLUMNS) * TILE_SOURCE,
		TILE_SOURCE,
		TILE_SOURCE
	)
	return icon


func _paint_cell(index: int) -> void:
	var map := _current_map()
	if map.is_empty() or compiler == null:
		return
	var width := int(map.get("width", 0))
	var terrain: Array = map.get("terrain", [])
	if width <= 0 or index < 0 or index >= terrain.size():
		_set_status("THAT CELL IS NOT EDITABLE", true)
		return
	if active_tool == "fill":
		_fill_from(index, width, terrain)
	elif active_tool == "rectangle":
		_rectangle_at(index, width, terrain)
	else:
		var changed := terrain.duplicate()
		changed[index] = selected_terrain
		_apply_terrain(changed, index, "PAINTED %s" % selected_terrain.to_upper())


func _fill_from(index: int, width: int, terrain: Array) -> void:
	var target := str(terrain[index])
	if target == selected_terrain:
		_focus_cell(index)
		return
	var changed := terrain.duplicate()
	var pending: Array[int] = [index]
	while not pending.is_empty():
		var at: int = pending.pop_back()
		if at < 0 or at >= changed.size() or str(changed[at]) != target:
			continue
		changed[at] = selected_terrain
		var x: int = at % width
		if x > 0:
			pending.append(at - 1)
		if x + 1 < width:
			pending.append(at + 1)
		if at >= width:
			pending.append(at - width)
		if at + width < changed.size():
			pending.append(at + width)
	_apply_terrain(changed, index, "FILLED %s" % selected_terrain.to_upper())


func _rectangle_at(index: int, width: int, terrain: Array) -> void:
	if rectangle_anchor < 0:
		rectangle_anchor = index
		_set_status("RECTANGLE  /  CHOOSE THE OPPOSITE CORNER", false)
		_focus_cell(index)
		return
	var from_x: int = mini(rectangle_anchor % width, index % width)
	var to_x: int = maxi(rectangle_anchor % width, index % width)
	var from_y: int = mini(rectangle_anchor / width, index / width)
	var to_y: int = maxi(rectangle_anchor / width, index / width)
	var changed := terrain.duplicate()
	for y in range(from_y, to_y + 1):
		for x in range(from_x, to_x + 1):
			changed[y * width + x] = selected_terrain
	rectangle_anchor = -1
	_apply_terrain(
		changed, index,
		"PAINTED RECTANGLE  /  %s" % selected_terrain.to_upper()
	)


func _apply_terrain(changed: Array, focus_index: int, message: String) -> void:
	var map := _current_map()
	var current: Array = map.get("terrain", [])
	if changed == current:
		_focus_cell(focus_index)
		return
	var terrain_ids := PackedStringArray()
	for value: Variant in changed:
		terrain_ids.append(str(value))
	var updated: Dictionary = compiler.update_map_terrain(
		source, str(map.get("id", "")), terrain_ids
	)
	if not updated.ok:
		_set_status("EDIT REFUSED  /  %s" % _diagnostic_code(updated), true)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(focus_index)
	_set_status("UNSAVED  /  %s" % message, false)


func _refresh_maps_after_edit(focus_index: int) -> void:
	map_index = clampi(map_index, 0, maxi(0, maps.size() - 1))
	_refresh_maps()
	if focus_index >= 0:
		call_deferred("_focus_cell", focus_index)


func _create_map() -> void:
	if compiler == null:
		_set_status("MAP CREATION IS NOT AVAILABLE", true)
		return
	var sequence := 1
	var existing: Array[String] = []
	for value: Variant in maps:
		if value is Dictionary:
			existing.append(str(value.get("id", "")))
	while existing.has("map_%d" % sequence):
		sequence += 1
	var map_id := "map_%d" % sequence
	var map_name := "New Map %d" % sequence
	var added: Dictionary = compiler.add_map(
		source, map_id, map_name, 8, 6, selected_terrain
	)
	if not added.ok:
		_set_status("CREATE REFUSED  /  %s" % _diagnostic_code(added), true)
		return
	_push_undo(source)
	source = added.source
	redo_stack.clear()
	dirty = true
	_read_source()
	map_index = maps.size() - 1
	rectangle_anchor = -1
	_refresh_maps_after_edit(0)
	_set_status("UNSAVED  /  CREATED %s" % map_name.to_upper(), false)


func _push_undo(value: String) -> void:
	undo_stack.append(value)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()


func _undo() -> void:
	if undo_stack.is_empty():
		return
	redo_stack.append(source)
	source = undo_stack.pop_back()
	rectangle_anchor = -1
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status("UNDO  /  UNSAVED", false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	_push_undo(source)
	source = redo_stack.pop_back()
	rectangle_anchor = -1
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status("REDO  /  UNSAVED", false)


func _open_resize() -> void:
	var map := _current_map()
	if map.is_empty():
		_set_status("ADD A MAP BEFORE RESIZING", true)
		return
	resize_width = int(map.get("width", 1))
	resize_height = int(map.get("height", 1))
	resize_layer.visible = true
	_update_resize_preview()
	resize_first_button.call_deferred("grab_focus")


func _close_resize() -> void:
	resize_layer.visible = false
	map_select.call_deferred("grab_focus")


func _change_resize_width(delta: int) -> void:
	_set_resize_dimensions(resize_width + delta, resize_height)


func _change_resize_height(delta: int) -> void:
	_set_resize_dimensions(resize_width, resize_height + delta)


func _set_resize_dimensions(width: int, height: int) -> void:
	resize_width = clampi(width, 1, 256)
	resize_height = clampi(height, 1, 256)
	_update_resize_preview()


func _update_resize_preview() -> void:
	var map := _current_map()
	if map.is_empty():
		resize_preview = {}
		resize_apply_button.disabled = true
		return
	var old_width := int(map.get("width", 1))
	var old_height := int(map.get("height", 1))
	var overlap_width := mini(old_width, resize_width)
	var overlap_height := mini(old_height, resize_height)
	var retained := overlap_width * overlap_height
	var terrain_clipped := old_width * old_height - retained
	var terrain_added := resize_width * resize_height - retained
	var placement_clipped := 0
	var deployment_clipped := 0
	var encounters := 0
	var map_id := str(map.get("id", ""))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var flow_value: Variant = campaign.get("flow", {})
		if not flow_value is Dictionary:
			continue
		var flow: Dictionary = flow_value
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("kind", "")) != "encounter" or str(node.get("mapId", "")) != map_id:
				continue
			encounters += 1
			for placement_value: Variant in node.get("placements", []):
				if placement_value is Dictionary and _coordinate_is_clipped(
					placement_value, resize_width, resize_height
				):
					placement_clipped += 1
			var deployment_value: Variant = node.get("deployment", {})
			if deployment_value is Dictionary:
				var deployment: Dictionary = deployment_value
				for tile_value: Variant in deployment.get("tiles", []):
					if tile_value is Dictionary and _coordinate_is_clipped(
						tile_value, resize_width, resize_height
					):
						deployment_clipped += 1
	var blocked := placement_clipped > 0 or deployment_clipped > 0
	resize_preview = {
		"old_width": old_width,
		"old_height": old_height,
		"width": resize_width,
		"height": resize_height,
		"retained": retained,
		"terrain_clipped": terrain_clipped,
		"terrain_added": terrain_added,
		"encounters": encounters,
		"placement_clipped": placement_clipped,
		"deployment_clipped": deployment_clipped,
		"blocked": blocked,
	}
	resize_width_label.text = str(resize_width)
	resize_height_label.text = str(resize_height)
	resize_fill_label.text = "NEW CELLS  /  %s" % selected_terrain.to_upper()
	var lines := PackedStringArray([
		"%d × %d  →  %d × %d" % [old_width, old_height, resize_width, resize_height],
		"%d terrain cells retained  ·  %d clipped  ·  %d added" % [retained, terrain_clipped, terrain_added],
		"%d encounter%s reference this map" % [encounters, "" if encounters == 1 else "s"],
	])
	if blocked:
		lines.append(
			"BLOCKED  /  %d placement%s and %d deployment tile%s would be outside" % [
				placement_clipped, "" if placement_clipped == 1 else "s",
				deployment_clipped, "" if deployment_clipped == 1 else "s",
			]
		)
	else:
		lines.append("SAFE TO APPLY  /  NO ENCOUNTER DATA IS CLIPPED")
	resize_preview_label.text = "\n".join(lines)
	resize_preview_label.add_theme_color_override(
		"font_color", Color("ff7a83") if blocked else Color("72e08f")
	)
	resize_apply_button.disabled = blocked or (
		old_width == resize_width and old_height == resize_height
	)


func _coordinate_is_clipped(value: Dictionary, width: int, height: int) -> bool:
	var x := int(value.get("x", -1))
	var y := int(value.get("y", -1))
	return x < 0 or y < 0 or x >= width or y >= height


func _apply_resize() -> void:
	if resize_apply_button.disabled:
		return
	var map := _current_map()
	var resized: Dictionary = compiler.resize_map(
		source, str(map.get("id", "")), resize_width, resize_height,
		selected_terrain
	)
	if not resized.ok:
		_set_status("RESIZE REFUSED  /  %s" % _diagnostic_code(resized), true)
		return
	_push_undo(source)
	source = resized.source
	redo_stack.clear()
	dirty = true
	rectangle_anchor = -1
	_read_source()
	resize_layer.visible = false
	_refresh_maps_after_edit(0)
	_set_status(
		"UNSAVED  /  RESIZED TO %d × %d" % [resize_width, resize_height],
		false
	)


func _save() -> void:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return
	dirty = false
	_set_status("SAVED  /  PORTABLE PROJECT SOURCE", false)
	_update_actions()


func _play_draft() -> void:
	if compiler == null:
		_set_status("NATIVE PROJECT COMPILER IS UNAVAILABLE", true)
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status("NOT PLAYABLE  /  %s" % _diagnostic_code(compiled), true)
		return
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status("NOT PLAYABLE  /  ADD A CAMPAIGN", true)
		return
	var campaign := str(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status("NOT PLAYABLE  /  CAMPAIGN NEEDS AN ID", true)
		return
	LaunchContext.stage_package(compiled.package, campaign)
	get_tree().change_scene_to_file("res://main.tscn")


func _request_projects() -> void:
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS CANCEL AGAIN TO DISCARD", true)
		return
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _diagnostic_code(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	return str(diagnostics[0].get("code", "PROJECT INCOMPLETE")).replace("_", " ").to_upper()


func _focus_cell(index: int) -> void:
	if grid.get_child_count() == 0:
		return
	var safe := clampi(index, 0, grid.get_child_count() - 1)
	var child := grid.get_child(safe)
	if child is Control and child.is_inside_tree():
		child.grab_focus()


func _update_actions() -> void:
	save_button.disabled = not dirty
	undo_button.disabled = undo_stack.is_empty()
	redo_button.disabled = redo_stack.is_empty()
	play_button.disabled = maps.is_empty()


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label("GRANDLEON  /  MAP CREATOR", 24, Color("69d9ff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label("Paint maps and play the draft immediately.", 34, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE", 190, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 170, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	redo_button = _button("REDO", 170, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	var new_map_button := _button("NEW MAP", 190, 58)
	new_map_button.pressed.connect(_create_map)
	toolbar.add_child(new_map_button)
	var resize_button := _button("RESIZE", 190, 58)
	resize_button.pressed.connect(_open_resize)
	toolbar.add_child(resize_button)
	play_button = _button("PLAY DRAFT", 240, 58)
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var projects := _button("PROJECTS", 210, 58)
	projects.pressed.connect(_request_projects)
	toolbar.add_child(projects)
	var toolbar_hint := _label("Confirm applies tool  •  Cancel returns", 17, Color("718896"))
	toolbar_hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	toolbar_hint.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	toolbar.add_child(toolbar_hint)

	var tools_panel := PanelContainer.new()
	tools_panel.position = Vector2(80, 232)
	tools_panel.size = Vector2(410, 760)
	tools_panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 18, Color(0.20, 0.62, 0.78, 0.35)))
	add_child(tools_panel)
	var tools_column := VBoxContainer.new()
	tools_column.add_theme_constant_override("separation", 12)
	tools_panel.add_child(tools_column)
	tools_column.add_child(_label("MAP", 17, Color("69d9ff")))
	map_select = OptionButton.new()
	map_select.custom_minimum_size = Vector2(330, 58)
	map_select.add_theme_font_size_override("font_size", 19)
	map_select.item_selected.connect(_select_map)
	tools_column.add_child(map_select)
	tools_column.add_child(_label("TOOL", 17, Color("69d9ff")))
	var tool_row := HBoxContainer.new()
	tool_row.add_theme_constant_override("separation", 6)
	tools_column.add_child(tool_row)
	for tool_name: String in ["brush", "fill", "rectangle"]:
		var tool_button := _button(tool_name.to_upper(), 102, 48)
		tool_button.add_theme_font_size_override("font_size", 14)
		tool_button.pressed.connect(_select_tool.bind(tool_name))
		tool_row.add_child(tool_button)
		tool_buttons[tool_name] = tool_button
	_update_tool_selection()
	tools_column.add_child(_label("TERRAIN", 17, Color("69d9ff")))
	var palette_scroll := ScrollContainer.new()
	palette_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	palette_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	tools_column.add_child(palette_scroll)
	palette_list = VBoxContainer.new()
	palette_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	palette_list.add_theme_constant_override("separation", 7)
	palette_scroll.add_child(palette_list)

	var board_panel := PanelContainer.new()
	board_panel.position = Vector2(520, 232)
	board_panel.size = Vector2(1320, 760)
	board_panel.add_theme_stylebox_override("panel", _panel(Color("091721"), 18, Color(0.20, 0.62, 0.78, 0.25)))
	add_child(board_panel)
	var board_column := VBoxContainer.new()
	board_column.add_theme_constant_override("separation", 10)
	board_panel.add_child(board_column)
	var board_title := _label("BOARD", 17, Color("69d9ff"))
	board_column.add_child(board_title)
	var board_scroll := ScrollContainer.new()
	board_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	board_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	board_column.add_child(board_scroll)
	var centre := CenterContainer.new()
	centre.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	centre.size_flags_vertical = Control.SIZE_EXPAND_FILL
	board_scroll.add_child(centre)
	grid = GridContainer.new()
	grid.add_theme_constant_override("h_separation", 4)
	grid.add_theme_constant_override("v_separation", 4)
	centre.add_child(grid)
	empty_label = _label("", 24, Color("718896"))
	empty_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	empty_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	empty_label.custom_minimum_size = Vector2(900, 500)
	centre.add_child(empty_label)

	status_label = _label("OPENING PROJECT", 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 1010)
	status_label.size = Vector2(1750, 44)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)
	_build_resize_overlay()


func _build_resize_overlay() -> void:
	resize_layer = Control.new()
	resize_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	resize_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	resize_layer.visible = false
	add_child(resize_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.86)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	resize_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(500, 190)
	panel.size = Vector2(920, 700)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	resize_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("RESIZE MAP", 24, Color("69d9ff")))
	var explanation := _label(
		"The top-left origin stays fixed. New cells use the selected terrain. "
		+ "A shrink cannot apply while encounter data would be clipped.",
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 70
	column.add_child(explanation)

	var dimensions := HBoxContainer.new()
	dimensions.add_theme_constant_override("separation", 22)
	column.add_child(dimensions)
	var width_column := VBoxContainer.new()
	width_column.add_theme_constant_override("separation", 8)
	dimensions.add_child(width_column)
	width_column.add_child(_label("WIDTH", 16, Color("718896")))
	var width_row := HBoxContainer.new()
	width_row.add_theme_constant_override("separation", 8)
	width_column.add_child(width_row)
	resize_first_button = _button("−", 92, 64)
	resize_first_button.pressed.connect(_change_resize_width.bind(-1))
	width_row.add_child(resize_first_button)
	resize_width_label = _label("1", 28, Color("f2f7f9"))
	resize_width_label.custom_minimum_size = Vector2(112, 64)
	resize_width_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	resize_width_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	width_row.add_child(resize_width_label)
	var width_plus := _button("+", 92, 64)
	width_plus.pressed.connect(_change_resize_width.bind(1))
	width_row.add_child(width_plus)

	var height_column := VBoxContainer.new()
	height_column.add_theme_constant_override("separation", 8)
	dimensions.add_child(height_column)
	height_column.add_child(_label("HEIGHT", 16, Color("718896")))
	var height_row := HBoxContainer.new()
	height_row.add_theme_constant_override("separation", 8)
	height_column.add_child(height_row)
	var height_minus := _button("−", 92, 64)
	height_minus.pressed.connect(_change_resize_height.bind(-1))
	height_row.add_child(height_minus)
	resize_height_label = _label("1", 28, Color("f2f7f9"))
	resize_height_label.custom_minimum_size = Vector2(112, 64)
	resize_height_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	resize_height_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	height_row.add_child(resize_height_label)
	var height_plus := _button("+", 92, 64)
	height_plus.pressed.connect(_change_resize_height.bind(1))
	height_row.add_child(height_plus)

	resize_fill_label = _label("NEW CELLS", 17, Color("69d9ff"))
	column.add_child(resize_fill_label)
	resize_preview_label = _label("", 19, Color("72e08f"))
	resize_preview_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	resize_preview_label.custom_minimum_size.y = 180
	column.add_child(resize_preview_label)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	resize_apply_button = _button("APPLY RESIZE", 300, 64)
	resize_apply_button.pressed.connect(_apply_resize)
	actions.add_child(resize_apply_button)
	var cancel := _button("CANCEL", 220, 64)
	cancel.pressed.connect(_close_resize)
	actions.add_child(cancel)


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override("font_color", Color("ff7a83") if failure else Color("72e08f"))


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int, height: int) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.add_theme_font_size_override("font_size", 18)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override("normal", _panel(Color("102633"), 10, Color(0.25, 0.62, 0.74, 0.30)))
	var active := _panel(Color("69d9ff"), 10, Color("b9f2ff"))
	button.add_theme_stylebox_override("hover", active)
	button.add_theme_stylebox_override("pressed", active)
	button.add_theme_stylebox_override("focus", active)
	return button


func _panel(fill: Color, radius: int, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(2)
	style.set_corner_radius_all(radius)
	style.content_margin_left = 14
	style.content_margin_right = 14
	style.content_margin_top = 10
	style.content_margin_bottom = 10
	return style
