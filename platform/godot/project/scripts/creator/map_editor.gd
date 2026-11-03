# SPDX-License-Identifier: MIT
extends Control

const ArtLibrary = preload("res://scripts/art_library.gd")
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const TILE_SOURCE := 32
const BLOB_COLUMNS := 8
const HISTORY_LIMIT := 64
const SURROUNDINGS_RECOMMENDED_DEPTH := 3
const TERRAIN_PALETTE := [
	"grass", "forest", "mountain", "water", "road", "bridge", "sand",
	"ruins", "farmland", "snow", "swamp", "hills", "bamboo", "paved",
]
const OBJECTIVE_KINDS := [
	{"id": "defeatAllOpponents", "label": "DEFEAT ALL OPPONENTS"},
	{"id": "defeatTarget", "label": "DEFEAT A TARGET"},
	{"id": "protectTarget", "label": "PROTECT A TARGET"},
	{"id": "surviveRounds", "label": "SURVIVE ROUNDS"},
]

var library
var compiler
var art = ArtLibrary.new()
var project_id := ""
var source := ""
var project: Dictionary = {}
var maps: Array = []
var map_index := 0
var encounters: Array[Dictionary] = []
var encounter_index := 0
var selected_terrain := "grass"
var active_tool := "brush"
var rectangle_anchor := -1
var selected_placement_id := ""
var pending_placement: Dictionary = {}
var placement_side := "second"
var placement_candidates: Array[Dictionary] = []
var placement_candidate_index := 0
var deployment_editing := false
var deployment_id := ""
var deployment_draft: Array[int] = []
var deployment_original: Array[int] = []
var tool_before_deployment := "brush"
var objective_mode := "assigned"
var objective_candidates: Array[Dictionary] = []
var objective_candidate_index := 0
var objective_kind_index := 0
var objective_side := "first"
var objective_targets: Array[Dictionary] = []
var objective_target_index := 0
var objective_rounds := 3
var dirty := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []
var discard_armed_until := 0
var resize_width := 1
var resize_height := 1
var resize_preview: Dictionary = {}
var surroundings_mode := "solid"
var surroundings_depth := SURROUNDINGS_RECOMMENDED_DEPTH
var surroundings_colour := Color("07131d")

var title_label: Label
var map_select: OptionButton
var encounter_select: OptionButton
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
var add_unit_button: Button
var placement_layer: Control
var placement_side_button: Button
var placement_candidate_label: Label
var placement_apply_button: Button
var placement_first_button: Button
var deployment_button: Button
var deployment_actions: HBoxContainer
var deployment_apply_button: Button
var objective_button: Button
var objective_layer: Control
var objective_mode_button: Button
var objective_candidate_label: Label
var objective_previous_button: Button
var objective_next_button: Button
var objective_options: VBoxContainer
var objective_kind_button: Button
var objective_side_button: Button
var objective_target_button: Button
var objective_rounds_row: HBoxContainer
var objective_rounds_label: Label
var objective_primary_button: Button
var objective_first_button: Button
var resize_layer: Control
var resize_width_label: Label
var resize_height_label: Label
var resize_fill_label: Label
var resize_preview_label: Label
var resize_apply_button: Button
var resize_first_button: Button
var surroundings_button: Button
var surroundings_layer: Control
var surroundings_mode_button: Button
var surroundings_depth_row: HBoxContainer
var surroundings_depth_label: Label
var surroundings_colour_controls: VBoxContainer
var surroundings_colour_preview: ColorRect
var surroundings_colour_label: Label
var surroundings_channel_labels: Array[Label] = []
var surroundings_channel_sliders: Array[HSlider] = []
var surroundings_guidance_label: Label
var surroundings_apply_button: Button
var surroundings_first_button: Button
var board_panel: PanelContainer
var setup_button: Button
var setup_layer: Control
var setup_first_button: Button
var more_button: Button
var more_layer: Control
var more_first_button: Button
var chapter_layer: Control
var chapter_title: LineEdit
var chapter_create_button: Button
var look_layer: Control
var look_button: Button
var look_theme: OptionButton
var look_style: OptionButton
var look_character_preview: TextureRect
var look_character_label: Label
var look_terrain_previews: Array[TextureRect] = []
var look_refreshing := false


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if look_layer != null and look_layer.visible:
			_close_look()
		elif surroundings_layer != null and surroundings_layer.visible:
			_close_surroundings()
		elif objective_layer != null and objective_layer.visible:
			_close_objectives()
		elif placement_layer != null and placement_layer.visible:
			_close_add_placement()
		elif resize_layer != null and resize_layer.visible:
			_close_resize()
		elif setup_layer != null and setup_layer.visible:
			_close_setup()
		elif chapter_layer != null and chapter_layer.visible:
			_close_new_chapter()
		elif more_layer != null and more_layer.visible:
			_close_more()
		elif deployment_editing:
			_cancel_deployment_edit()
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
	var requested_action: String = CreatorContext.take_map_action()
	if requested_action == "add_chapter":
		call_deferred("_open_new_chapter")
	else:
		map_select.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status("PROJECT SOURCE IS NOT READABLE", true)
		return false
	project = parsed.data
	var map_value: Variant = project.get("maps", [])
	maps = map_value if map_value is Array else []
	if not CreatorContext.map_id.is_empty():
		for index in maps.size():
			var candidate: Variant = maps[index]
			if candidate is Dictionary and str(candidate.get("id", "")) == CreatorContext.map_id:
				map_index = index
				break
	return true


func _refresh_maps() -> void:
	map_select.clear()
	for value: Variant in maps:
		if value is Dictionary:
			map_select.add_item(str(value.get("name", value.get("id", "Untitled map"))))
	map_index = clampi(map_index, 0, maxi(0, maps.size() - 1))
	if not maps.is_empty():
		map_select.select(map_index)
	_refresh_encounters()
	_refresh_palette()
	_refresh_grid()
	_update_actions()


func _select_map(index: int) -> void:
	if index < 0 or index >= maps.size():
		return
	map_index = index
	if active_tool == "surroundings":
		active_tool = "brush"
	_reset_deployment_edit()
	encounter_index = 0
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	_refresh_encounters()
	_refresh_palette()
	_refresh_grid(0)
	_update_tool_selection()
	_set_status("MAP  /  %s" % _current_map_name().to_upper(), false)


func _refresh_encounters() -> void:
	encounters.clear()
	encounter_select.clear()
	var requested_encounter_index := -1
	var map_id := str(_current_map().get("id", ""))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var flow_value: Variant = campaign.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("kind", "")) != "encounter" or str(node.get("mapId", "")) != map_id:
				continue
			encounters.append({
				"campaign_id": str(campaign.get("id", "")),
				"campaign_name": str(campaign.get("name", campaign.get("id", "Campaign"))),
				"campaign": campaign,
				"node": node,
			})
			if str(node.get("id", "")) == CreatorContext.encounter_id:
				requested_encounter_index = encounters.size() - 1
			encounter_select.add_item(
				"%s / %s" % [
					str(campaign.get("name", "Campaign")),
					str(node.get("name", node.get("id", "Encounter"))),
				]
			)
	encounter_select.disabled = encounters.is_empty()
	if encounters.is_empty():
		encounter_select.add_item("NO ENCOUNTER ON THIS MAP")
		encounter_index = 0
	else:
		encounter_index = (
			requested_encounter_index
			if requested_encounter_index >= 0
			else clampi(encounter_index, 0, encounters.size() - 1)
		)
		encounter_select.select(encounter_index)


func _select_encounter(index: int) -> void:
	if index < 0 or index >= encounters.size():
		return
	encounter_index = index
	_reset_deployment_edit()
	selected_placement_id = ""
	pending_placement = {}
	_refresh_grid(0)
	var node: Dictionary = encounters[index].node
	_set_status("ENCOUNTER  /  %s" % str(node.get("name", node.get("id", ""))).to_upper(), false)


func _current_encounter() -> Dictionary:
	if encounter_index < 0 or encounter_index >= encounters.size():
		return {}
	return encounters[encounter_index]


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
	if deployment_editing and name != "deployment":
		_reset_deployment_edit()
	active_tool = name
	rectangle_anchor = -1
	selected_placement_id = ""
	if name != "add_unit":
		pending_placement = {}
	_update_tool_selection()
	_set_status("TOOL  /  %s" % name.to_upper(), false)
	_focus_cell(0)


func _update_tool_selection() -> void:
	for name: String in tool_buttons:
		var button: Button = tool_buttons[name]
		var display_names := {
			"add_unit": "ADD CHARACTER", "unit": "MOVE CHARACTER",
			"remove_unit": "REMOVE CHARACTER", "deployment": "DEPLOYMENT AREA",
			"ownership": "PLAYER OWNERSHIP", "scaling": "TWO-PLAYER SCALING",
			"surroundings": "MAP SURROUNDINGS",
		}
		var display := str(display_names.get(name, name.to_upper()))
		button.text = ("● " if name == active_tool else "  ") + display
	if setup_button != null:
		var setup_names := {
			"add_unit": "ADD CHARACTER", "unit": "MOVE CHARACTER",
			"remove_unit": "REMOVE CHARACTER", "deployment": "DEPLOYMENT",
			"ownership": "PLAYER OWNERSHIP", "scaling": "TWO-PLAYER SCALING",
			"surroundings": "MAP SURROUNDINGS",
		}
		setup_button.text = (
			"● %s" % str(setup_names[active_tool])
			if setup_names.has(active_tool)
			else "BATTLE SETUP"
		)


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
	var outside: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	var outside_mode := str(outside.get("mode", "none"))
	var outside_depth := (
		clampi(int(outside.get("depth", 0)), 0, 8)
		if outside_mode in ["automatic", "manual"] else 0
	)
	var outer_width := width + outside_depth * 2
	var outer_height := height + outside_depth * 2
	if board_panel != null:
		var panel_colour := Color("091721")
		if outside_mode == "solid":
			var encoded := str(outside.get("color", "#091721"))
			if Color.html_is_valid(encoded):
				panel_colour = Color(encoded)
		board_panel.add_theme_stylebox_override(
			"panel", _panel(panel_colour, 18, Color(0.20, 0.62, 0.78, 0.25))
		)
	var placements_by_cell: Dictionary = {}
	var deployment_cells: Array[int] = []
	var objective_targets_by_cell: Dictionary = {}
	var encounter := _current_encounter()
	if not encounter.is_empty():
		var node: Dictionary = encounter.node
		deployment_cells = (
			deployment_draft.duplicate()
			if deployment_editing
			else _deployment_indices(node, width)
		)
		objective_targets_by_cell = _objective_target_cells(node, width)
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			var placement_x := int(placement.get("x", -1))
			var placement_y := int(placement.get("y", -1))
			if placement_x < 0 or placement_y < 0 or placement_x >= width or placement_y >= height:
				continue
			var placement_cell := placement_y * width + placement_x
			if not placements_by_cell.has(placement_cell):
				placements_by_cell[placement_cell] = []
			placements_by_cell[placement_cell].append(placement)
	grid.columns = outer_width
	var tile_size := clampi(
		floori(minf(830.0 / outer_width, 620.0 / outer_height)), 34, 96
	)
	var ring_index := 0
	var manual_terrain: Array = outside.get("terrain", [])
	for outer_y in range(-outside_depth, height + outside_depth):
		for outer_x in range(-outside_depth, width + outside_depth):
			var playable := outer_x >= 0 and outer_y >= 0 and outer_x < width and outer_y < height
			if not playable:
				var outside_name := _automatic_surrounding_name(map, outer_x, outer_y)
				if outside_mode == "manual" and ring_index < manual_terrain.size():
					outside_name = str(manual_terrain[ring_index])
				var outside_cell := _button("", tile_size, tile_size)
				outside_cell.icon = _terrain_icon(outside_name, 255)
				outside_cell.expand_icon = true
				outside_cell.tooltip_text = (
					"NON-PLAYABLE %d, %d  /  %s\n"
					+ "Presentation only: units can never enter this cell."
				) % [outer_x, outer_y, outside_name]
				outside_cell.pressed.connect(_paint_surrounding_cell.bind(ring_index))
				grid.add_child(outside_cell)
				ring_index += 1
				continue
			var index := outer_y * width + outer_x
			var name := str(terrain[index]) if index < terrain.size() else "grass"
			var cell := _button("", tile_size, tile_size)
			cell.icon = _terrain_icon(name, _terrain_mask(index, width, height, terrain))
			cell.expand_icon = true
			cell.tooltip_text = "%d, %d  /  %s" % [outer_x, outer_y, name]
			if placements_by_cell.has(index):
				_add_placement_badge(cell, placements_by_cell[index])
			if deployment_cells.has(index):
				_add_deployment_badge(cell, tile_size, deployment_editing)
			if objective_targets_by_cell.has(index):
				_add_objective_marker(cell, objective_targets_by_cell[index])
			cell.pressed.connect(_paint_cell.bind(index))
			grid.add_child(cell)
	if focus_index >= 0:
		call_deferred("_focus_cell", focus_index)


func _automatic_surrounding_name(map: Dictionary, x: int, y: int) -> String:
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	var terrain: Array = map.get("terrain", [])
	var nearest_x := clampi(x, 0, width - 1)
	var nearest_y := clampi(y, 0, height - 1)
	var index := nearest_y * width + nearest_x
	var edge_name := str(terrain[index]) if index < terrain.size() else "grass"
	return edge_name


func _add_placement_badge(cell: Button, placements: Array) -> void:
	var first: Dictionary = placements[0]
	var first_side := str(first.get("side", "second")) == "first"
	var local_player := clampi(int(first.get("player", 1)), 1, 2)
	var colour := (
		(Color("69d9ff") if local_player == 1 else Color("ffca63"))
		if first_side else Color("ff7a83")
	)
	var selected := false
	for placement_value: Variant in placements:
		if placement_value is Dictionary and str(placement_value.get("id", "")) == selected_placement_id:
			selected = true
			break
	if selected:
		colour = Color("ffca63")
	var label := Label.new()
	var two_player_only := int(first.get("minimumPlayers", 1)) >= 2
	label.text = "MOVE" if selected else (
		"%d×" % placements.size() if placements.size() > 1 else (
			"P%d" % local_player if first_side else ("2P" if two_player_only else "ENEMY")
		)
	)
	label.position = Vector2(5, 5)
	label.custom_minimum_size = Vector2(48, 20)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_size_override("font_size", 11)
	label.add_theme_color_override("font_color", Color("07111b"))
	var badge := StyleBoxFlat.new()
	badge.bg_color = colour
	badge.set_corner_radius_all(5)
	badge.content_margin_left = 5
	badge.content_margin_right = 5
	label.add_theme_stylebox_override("normal", badge)
	cell.add_child(label)
	var details := PackedStringArray()
	for placement_value: Variant in placements:
		var placement: Dictionary = placement_value
		details.append(
			"%s%s%s: %s" % [
				str(placement.get("side", "unit")).to_upper(),
				(
					" / PLAYER %d" % clampi(int(placement.get("player", 1)), 1, 2)
					if str(placement.get("side", "second")) == "first" else ""
				),
				" / 2 PLAYERS" if int(placement.get("minimumPlayers", 1)) >= 2 else "",
				str(placement.get("name", placement.get("id", "unit"))),
			]
		)
	cell.tooltip_text += "\n" + "\n".join(details)


func _add_deployment_badge(cell: Button, tile_size: int, pending: bool) -> void:
	var label := Label.new()
	label.text = "DRAFT" if pending else "ZONE"
	label.position = Vector2(5, tile_size - 23)
	label.size = Vector2(maxi(34, tile_size - 10), 18)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_size_override("font_size", 9)
	label.add_theme_color_override("font_color", Color("07111b"))
	var badge := StyleBoxFlat.new()
	badge.bg_color = Color("ffca63") if pending else Color("b58cff")
	badge.set_corner_radius_all(4)
	label.add_theme_stylebox_override("normal", badge)
	cell.add_child(label)
	cell.tooltip_text += "\n" + (
		"PENDING DEPLOYMENT TILE" if pending else "DEPLOYMENT ZONE"
	)


func _add_objective_marker(cell: Button, names: Array) -> void:
	var outline := Panel.new()
	outline.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outline.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0)
	style.border_color = Color("ffca63")
	style.set_border_width_all(3)
	style.set_corner_radius_all(8)
	outline.add_theme_stylebox_override("panel", style)
	cell.add_child(outline)
	var details := PackedStringArray()
	for value: Variant in names:
		details.append(str(value))
	cell.tooltip_text += "\nOBJECTIVE TARGET  /  " + ", ".join(details)


func _objective_definitions_by_id() -> Dictionary:
	var result := {}
	for value: Variant in project.get("objectives", []):
		if value is Dictionary:
			var objective_id := str(value.get("id", ""))
			if not objective_id.is_empty():
				result[objective_id] = value
	return result


func _objective_target_cells(node: Dictionary, width: int) -> Dictionary:
	var result := {}
	var definitions := _objective_definitions_by_id()
	var targets := {}
	for id_value: Variant in node.get("objectiveIds", []):
		var objective_id := str(id_value)
		if not definitions.has(objective_id):
			continue
		var objective: Dictionary = definitions[objective_id]
		var kind := str(objective.get("kind", "defeatAllOpponents"))
		if kind != "defeatTarget" and kind != "protectTarget":
			continue
		var target := str(objective.get("targetPlacementId", ""))
		if not target.is_empty():
			if not targets.has(target):
				targets[target] = []
			targets[target].append(str(objective.get("name", objective_id)))
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var source_key := str(placement_value.get("memberId", placement_value.get("id", "")))
		if not targets.has(source_key):
			continue
		var x := int(placement_value.get("x", -1))
		var y := int(placement_value.get("y", -1))
		if x >= 0 and y >= 0:
			result[y * width + x] = targets[source_key]
	return result


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
	icon.atlas = art.terrain_texture(kind, _project_theme_index())
	icon.region = Rect2(
		float(variant % BLOB_COLUMNS) * TILE_SOURCE,
		float(variant / BLOB_COLUMNS) * TILE_SOURCE,
		TILE_SOURCE,
		TILE_SOURCE
	)
	return icon


func _project_theme_index() -> int:
	return maxi(0, art.THEME_NAMES.find(str(project.get("themeId", "temperate"))))


func _paint_cell(index: int) -> void:
	var map := _current_map()
	if map.is_empty() or compiler == null:
		return
	var width := int(map.get("width", 0))
	var terrain: Array = map.get("terrain", [])
	if width <= 0 or index < 0 or index >= terrain.size():
		_set_status("THAT CELL IS NOT EDITABLE", true)
		return
	if active_tool == "deployment":
		_toggle_deployment_cell(index)
	elif active_tool == "add_unit":
		_add_unit_at(index, width)
	elif active_tool == "remove_unit":
		_remove_unit_at(index, width)
	elif active_tool == "ownership":
		_toggle_placement_player_at(index, width)
	elif active_tool == "scaling":
		_toggle_placement_scaling_at(index, width)
	elif active_tool == "unit":
		_move_unit_at(index, width)
	elif active_tool == "fill":
		_fill_from(index, width, terrain)
	elif active_tool == "rectangle":
		_rectangle_at(index, width, terrain)
	else:
		var changed := terrain.duplicate()
		changed[index] = selected_terrain
		_apply_terrain(changed, index, "PAINTED %s" % selected_terrain.to_upper())


func _placement_at(index: int, width: int) -> Dictionary:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return {}
	var x := index % width
	var y := index / width
	var node: Dictionary = encounter.node
	for placement_value: Variant in node.get("placements", []):
		if placement_value is Dictionary and int(placement_value.get("x", -1)) == x and int(placement_value.get("y", -1)) == y:
			return placement_value
	return {}


func _add_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	if encounter.is_empty() or pending_placement.is_empty():
		_set_status("CHOOSE ADD UNIT AND A CHARACTER FIRST", true)
		return
	var node: Dictionary = encounter.node
	var placement_id := _unique_placement_id(
		str(pending_placement.get("member_id", pending_placement.get("unit_type_id", "unit")))
	)
	var added: Dictionary = compiler.add_placement(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		placement_id,
		str(pending_placement.get("unit_type_id", "")),
		str(pending_placement.get("side", "second")),
		str(pending_placement.get("member_id", "")),
		index % width,
		index / width
	)
	if not added.ok:
		_set_status("ADD REFUSED  /  %s" % _diagnostic_code(added), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = added.source
	redo_stack.clear()
	dirty = true
	pending_placement = {}
	active_tool = "unit"
	_read_source()
	_refresh_maps_after_edit(index)
	_update_tool_selection()
	_set_status("UNSAVED  /  ADDED %s" % placement_id.to_upper(), false)


func _remove_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status("CHOOSE A CELL THAT CONTAINS A UNIT", true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var removed: Dictionary = compiler.remove_placement(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		placement_id
	)
	if not removed.ok:
		_set_status("REMOVE REFUSED  /  %s" % _diagnostic_code(removed), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = removed.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status("UNSAVED  /  REMOVED %s  /  UNDO TO RESTORE" % placement_id.to_upper(), false)


func _toggle_placement_scaling_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status("CHOOSE AN ENEMY PLACEMENT TO SCALE", true)
		_focus_cell(index)
		return
	if str(placement.get("side", "second")) != "second":
		_set_status("ALLIED ROSTER MEMBERS ARE PRESENT AT EVERY PLAYER COUNT", true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var was_scaled := int(placement.get("minimumPlayers", 1)) >= 2
	var minimum_players := 1 if was_scaled else 2
	var updated: Dictionary = compiler.set_placement_minimum_players(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		placement_id,
		minimum_players
	)
	if not updated.ok:
		_set_status("SCALING REFUSED  /  %s" % _diagnostic_detail(updated), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(
		"UNSAVED  /  %s %s" % [
			placement_id.to_upper(),
			"ALWAYS PRESENT" if was_scaled else "ADDED FOR 2 PLAYERS",
		],
		false
	)


func _toggle_placement_player_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status("CHOOSE AN ALLIED PLACEMENT", true)
		_focus_cell(index)
		return
	if str(placement.get("side", "second")) != "first":
		_set_status("ONLY ALLIED UNITS BELONG TO PLAYER 1 OR PLAYER 2", true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var current_player := clampi(int(placement.get("player", 1)), 1, 2)
	var next_player := 2 if current_player == 1 else 1
	var updated: Dictionary = compiler.set_placement_player(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		placement_id,
		next_player
	)
	if not updated.ok:
		_set_status("OWNERSHIP REFUSED  /  %s" % _diagnostic_detail(updated), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(
		"UNSAVED  /  %s BELONGS TO PLAYER %d" % [placement_id.to_upper(), next_player],
		false
	)


func _unique_placement_id(base: String) -> String:
	var safe_base := base if not base.is_empty() else "unit"
	var existing: Array[String] = []
	var encounter := _current_encounter()
	if not encounter.is_empty():
		var node: Dictionary = encounter.node
		for placement_value: Variant in node.get("placements", []):
			if placement_value is Dictionary:
				existing.append(str(placement_value.get("id", "")))
	if not existing.has(safe_base):
		return safe_base
	var sequence := 2
	while existing.has("%s_%d" % [safe_base, sequence]):
		sequence += 1
	return "%s_%d" % [safe_base, sequence]


func _deployment_indices(node: Dictionary, width: int) -> Array[int]:
	var result: Array[int] = []
	var deployment_value: Variant = node.get("deployment", {})
	if not deployment_value is Dictionary:
		return result
	var deployment: Dictionary = deployment_value
	for tile_value: Variant in deployment.get("tiles", []):
		if not tile_value is Dictionary:
			continue
		var x := int(tile_value.get("x", -1))
		var y := int(tile_value.get("y", -1))
		if x >= 0 and y >= 0:
			result.append(y * width + x)
	result.sort()
	return result


func _unique_deployment_id() -> String:
	var existing: Array[String] = []
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var flow_value: Variant = campaign.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var deployment_value: Variant = node_value.get("deployment", {})
			if deployment_value is Dictionary:
				var found := str(deployment_value.get("id", ""))
				if not found.is_empty():
					existing.append(found)
	var candidate := "deployment"
	var sequence := 2
	while existing.has(candidate):
		candidate = "deployment_%d" % sequence
		sequence += 1
	return candidate


func _begin_deployment_edit() -> void:
	var encounter := _current_encounter()
	var map := _current_map()
	if encounter.is_empty() or map.is_empty():
		_set_status("ADD AN ENCOUNTER BEFORE EDITING ITS DEPLOYMENT ZONE", true)
		return
	var node: Dictionary = encounter.node
	var width := int(map.get("width", 0))
	if width <= 0:
		_set_status("THIS MAP HAS NO EDITABLE CELLS", true)
		return
	var deployment_value: Variant = node.get("deployment", {})
	deployment_id = (
		str(deployment_value.get("id", ""))
		if deployment_value is Dictionary
		else ""
	)
	if deployment_id.is_empty():
		deployment_id = _unique_deployment_id()
	deployment_original = _deployment_indices(node, width)
	deployment_draft.assign(deployment_original)
	tool_before_deployment = active_tool if active_tool != "deployment" else "brush"
	deployment_editing = true
	active_tool = "deployment"
	selected_placement_id = ""
	pending_placement = {}
	deployment_actions.visible = true
	_update_deployment_actions()
	_update_tool_selection()
	_refresh_grid(0)
	_set_status(
		"DEPLOYMENT DRAFT  /  TOGGLE CELLS, THEN APPLY  /  INCLUDE AN ALLIED START",
		false
	)


func _toggle_deployment_cell(index: int) -> void:
	if not deployment_editing:
		_set_status("CHOOSE ZONE BEFORE EDITING DEPLOYMENT", true)
		return
	var at := deployment_draft.find(index)
	if at >= 0:
		deployment_draft.remove_at(at)
	elif deployment_draft.size() >= 256:
		_set_status("DEPLOYMENT REGIONS ARE LIMITED TO 256 CELLS", true)
		_focus_cell(index)
		return
	else:
		deployment_draft.append(index)
		deployment_draft.sort()
	_update_deployment_actions()
	_refresh_grid(index)
	_set_status(
		"DEPLOYMENT DRAFT  /  %d CELL%s  /  APPLY WHEN READY" % [
			deployment_draft.size(), "" if deployment_draft.size() == 1 else "S",
		],
		false
	)


func _update_deployment_actions() -> void:
	deployment_apply_button.disabled = deployment_draft == deployment_original


func _apply_deployment_edit() -> void:
	if not deployment_editing or deployment_apply_button.disabled:
		return
	var encounter := _current_encounter()
	var map := _current_map()
	if encounter.is_empty() or map.is_empty():
		_set_status("DEPLOYMENT ENCOUNTER IS NO LONGER AVAILABLE", true)
		return
	var width := int(map.get("width", 0))
	var coordinates := PackedInt32Array()
	for index: int in deployment_draft:
		coordinates.append(index % width)
		coordinates.append(index / width)
	var node: Dictionary = encounter.node
	var updated: Dictionary = compiler.set_deployment_tiles(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		deployment_id,
		coordinates
	)
	if not updated.ok:
		_set_status("DEPLOYMENT REFUSED  /  %s" % _diagnostic_detail(updated), true)
		return
	var count := deployment_draft.size()
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_reset_deployment_edit()
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status(
		"UNSAVED  /  %s" % (
			"CLEARED DEPLOYMENT ZONE"
			if count == 0
			else "DEPLOYMENT ZONE  /  %d CELL%s" % [count, "" if count == 1 else "S"]
		),
		false
	)


func _cancel_deployment_edit() -> void:
	_reset_deployment_edit()
	_refresh_grid(0)
	_set_status("DEPLOYMENT DRAFT DISCARDED", false)
	setup_button.call_deferred("grab_focus")


func _reset_deployment_edit() -> void:
	if active_tool == "deployment":
		active_tool = tool_before_deployment if not tool_before_deployment.is_empty() else "brush"
	deployment_editing = false
	deployment_id = ""
	deployment_draft.clear()
	deployment_original.clear()
	if deployment_actions != null:
		deployment_actions.visible = false
	if not tool_buttons.is_empty():
		_update_tool_selection()


func _move_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	if encounter.is_empty():
		_set_status("THIS MAP HAS NO ENCOUNTER TO EDIT", true)
		return
	var node: Dictionary = encounter.node
	var x := index % width
	var y := index / width
	var placements: Array = node.get("placements", [])
	if selected_placement_id.is_empty():
		for placement_value: Variant in placements:
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			if int(placement.get("x", -1)) == x and int(placement.get("y", -1)) == y:
				selected_placement_id = str(placement.get("id", ""))
				if selected_placement_id.is_empty():
					break
				_refresh_grid(index)
				_set_status(
					"MOVE %s  /  CHOOSE A DESTINATION" % selected_placement_id.to_upper(),
					false
				)
				return
		_set_status("CHOOSE A CELL THAT CONTAINS A UNIT", true)
		_focus_cell(index)
		return

	for placement_value: Variant in placements:
		if placement_value is Dictionary and str(placement_value.get("id", "")) == selected_placement_id:
			if int(placement_value.get("x", -1)) == x and int(placement_value.get("y", -1)) == y:
				selected_placement_id = ""
				_refresh_grid(index)
				_set_status("UNIT MOVE CANCELLED", false)
				return
			break
	var moved: Dictionary = compiler.move_placement(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		selected_placement_id,
		x,
		y
	)
	if not moved.ok:
		_set_status("MOVE REFUSED  /  %s" % _diagnostic_code(moved), true)
		_focus_cell(index)
		return
	var moved_id := selected_placement_id
	_push_undo(source)
	source = moved.source
	redo_stack.clear()
	dirty = true
	selected_placement_id = ""
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status("UNSAVED  /  MOVED %s" % moved_id.to_upper(), false)


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
	_reset_deployment_edit()
	source = undo_stack.pop_back()
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status("UNDO  /  UNSAVED", false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	_push_undo(source)
	_reset_deployment_edit()
	source = redo_stack.pop_back()
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status("REDO  /  UNSAVED", false)


func _open_add_placement() -> void:
	if _current_encounter().is_empty():
		_set_status("ADD AN ENCOUNTER BEFORE PLACING UNITS", true)
		return
	pending_placement = {}
	placement_side = "second"
	placement_candidate_index = 0
	placement_layer.visible = true
	_refresh_placement_candidates()
	placement_first_button.call_deferred("grab_focus")


func _close_add_placement() -> void:
	placement_layer.visible = false
	placement_candidates.clear()
	pending_placement = {}
	setup_button.call_deferred("grab_focus")


func _toggle_placement_side() -> void:
	placement_side = "first" if placement_side == "second" else "second"
	placement_candidate_index = 0
	_refresh_placement_candidates()


func _cycle_placement_candidate(delta: int) -> void:
	if placement_candidates.is_empty():
		return
	placement_candidate_index = wrapi(
		placement_candidate_index + delta, 0, placement_candidates.size()
	)
	_update_placement_candidate()


func _refresh_placement_candidates() -> void:
	placement_candidates.clear()
	var encounter := _current_encounter()
	if encounter.is_empty():
		_update_placement_candidate()
		return
	if placement_side == "first":
		var used_members: Array[String] = []
		var node: Dictionary = encounter.node
		for placement_value: Variant in node.get("placements", []):
			if placement_value is Dictionary:
				var used := str(placement_value.get("memberId", ""))
				if not used.is_empty():
					used_members.append(used)
		var campaign: Dictionary = encounter.campaign
		for member_value: Variant in campaign.get("roster", []):
			if not member_value is Dictionary:
				continue
			var member_id := str(member_value.get("id", ""))
			if member_id.is_empty() or used_members.has(member_id):
				continue
			placement_candidates.append({
				"name": str(member_value.get("name", member_id)),
				"unit_type_id": str(member_value.get("unitTypeId", "")),
				"member_id": member_id,
				"side": "first",
			})
	else:
		for unit_value: Variant in project.get("unitTypes", []):
			if not unit_value is Dictionary:
				continue
			var unit_type_id := str(unit_value.get("id", ""))
			if unit_type_id.is_empty():
				continue
			placement_candidates.append({
				"name": str(unit_value.get("name", unit_type_id)),
				"unit_type_id": unit_type_id,
				"member_id": "",
				"side": "second",
			})
	placement_candidate_index = clampi(
		placement_candidate_index, 0,
		maxi(0, placement_candidates.size() - 1)
	)
	_update_placement_candidate()


func _update_placement_candidate() -> void:
	placement_side_button.text = (
		"SIDE  /  ALLY" if placement_side == "first" else "SIDE  /  ENEMY"
	)
	placement_side_button.add_theme_color_override(
		"font_color", Color("69d9ff") if placement_side == "first" else Color("ff7a83")
	)
	placement_apply_button.disabled = placement_candidates.is_empty()
	if placement_candidates.is_empty():
		placement_candidate_label.text = (
			"No unused campaign members are available."
			if placement_side == "first"
			else "This project has no unit types yet."
		)
		return
	var candidate: Dictionary = placement_candidates[placement_candidate_index]
	placement_candidate_label.text = "%s\n%s  /  %d OF %d" % [
		str(candidate.get("name", "Unit")),
		str(candidate.get("unit_type_id", "")).to_upper(),
		placement_candidate_index + 1,
		placement_candidates.size(),
	]


func _begin_add_placement() -> void:
	if placement_candidates.is_empty():
		return
	pending_placement = placement_candidates[placement_candidate_index].duplicate(true)
	active_tool = "add_unit"
	selected_placement_id = ""
	placement_layer.visible = false
	_update_tool_selection()
	_set_status(
		"ADD %s  /  CHOOSE AN EMPTY DESTINATION" % str(pending_placement.get("name", "UNIT")).to_upper(),
		false
	)
	call_deferred("_focus_cell", 0)


func _open_objectives() -> void:
	if deployment_editing:
		_set_status("APPLY OR CANCEL THE DEPLOYMENT DRAFT FIRST", true)
		return
	if _current_encounter().is_empty():
		_set_status("ADD AN ENCOUNTER BEFORE EDITING OBJECTIVES", true)
		return
	objective_mode = "assigned"
	objective_candidate_index = 0
	objective_kind_index = 0
	objective_side = "first"
	objective_target_index = 0
	objective_rounds = 3
	objective_layer.visible = true
	_refresh_objective_targets()
	_refresh_objective_candidates()
	objective_first_button.call_deferred("grab_focus")


func _close_objectives() -> void:
	objective_layer.visible = false
	objective_candidates.clear()
	objective_targets.clear()
	setup_button.call_deferred("grab_focus")


func _cycle_objective_mode() -> void:
	var modes := ["assigned", "available", "new"]
	objective_mode = modes[(modes.find(objective_mode) + 1) % modes.size()]
	objective_candidate_index = 0
	_refresh_objective_candidates()


func _cycle_objective_candidate(delta: int) -> void:
	if objective_candidates.is_empty():
		return
	objective_candidate_index = wrapi(
		objective_candidate_index + delta, 0, objective_candidates.size()
	)
	_update_objective_view()


func _cycle_objective_kind(delta: int = 1) -> void:
	objective_kind_index = wrapi(
		objective_kind_index + delta, 0, OBJECTIVE_KINDS.size()
	)
	objective_target_index = 0
	_update_objective_view()


func _toggle_objective_side() -> void:
	objective_side = "second" if objective_side == "first" else "first"
	_update_objective_view()


func _cycle_objective_target() -> void:
	if objective_targets.is_empty():
		return
	objective_target_index = wrapi(
		objective_target_index + 1, 0, objective_targets.size()
	)
	_update_objective_view()


func _change_objective_rounds(delta: int) -> void:
	objective_rounds = clampi(objective_rounds + delta, 1, 65535)
	_update_objective_view()


func _refresh_objective_targets() -> void:
	objective_targets.clear()
	var encounter := _current_encounter()
	if encounter.is_empty():
		return
	var member_names := {}
	var campaign: Dictionary = encounter.campaign
	for member_value: Variant in campaign.get("roster", []):
		if member_value is Dictionary:
			member_names[str(member_value.get("id", ""))] = str(
				member_value.get("name", member_value.get("id", "Member"))
			)
	var node: Dictionary = encounter.node
	var seen: Array[String] = []
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		if int(placement.get("minimumPlayers", 1)) >= 2:
			continue
		var member_id := str(placement.get("memberId", ""))
		var source_key := member_id if not member_id.is_empty() else str(placement.get("id", ""))
		if source_key.is_empty() or seen.has(source_key):
			continue
		seen.append(source_key)
		objective_targets.append({
			"id": source_key,
			"name": str(member_names.get(
				source_key,
				placement.get("name", placement.get("id", "Unit"))
			)),
			"side": str(placement.get("side", "second")),
		})
	objective_target_index = clampi(
		objective_target_index, 0, maxi(0, objective_targets.size() - 1)
	)


func _current_objective_ids() -> Array[String]:
	var result: Array[String] = []
	var encounter := _current_encounter()
	if encounter.is_empty():
		return result
	var node: Dictionary = encounter.node
	for value: Variant in node.get("objectiveIds", []):
		result.append(str(value))
	return result


func _objective_is_compatible(objective: Dictionary) -> bool:
	var kind := str(objective.get("kind", "defeatAllOpponents"))
	if kind != "defeatTarget" and kind != "protectTarget":
		return true
	var target_id := str(objective.get("targetPlacementId", ""))
	for target: Dictionary in objective_targets:
		if str(target.get("id", "")) == target_id:
			return true
	return false


func _refresh_objective_candidates() -> void:
	objective_candidates.clear()
	var assigned := _current_objective_ids()
	var definitions := _objective_definitions_by_id()
	if objective_mode == "assigned":
		for objective_id: String in assigned:
			if definitions.has(objective_id):
				objective_candidates.append(definitions[objective_id])
	elif objective_mode == "available":
		for value: Variant in project.get("objectives", []):
			if not value is Dictionary:
				continue
			var objective_id := str(value.get("id", ""))
			if not assigned.has(objective_id) and _objective_is_compatible(value):
				objective_candidates.append(value)
	objective_candidate_index = clampi(
		objective_candidate_index, 0,
		maxi(0, objective_candidates.size() - 1)
	)
	_update_objective_view()


func _objective_kind_label(kind: String) -> String:
	for value: Dictionary in OBJECTIVE_KINDS:
		if str(value.id) == kind:
			return str(value.label)
	return kind.to_upper()


func _objective_summary(objective: Dictionary) -> String:
	var kind := str(objective.get("kind", "defeatAllOpponents"))
	var lines := PackedStringArray([
		str(objective.get("name", objective.get("id", "Objective"))),
		"%s  /  SIDE %s" % [
			_objective_kind_label(kind),
			str(objective.get("side", "first")).to_upper(),
		],
		str(objective.get("id", "")).to_upper(),
	])
	if kind == "defeatTarget" or kind == "protectTarget":
		lines.append("TARGET  /  %s" % str(objective.get("targetPlacementId", "")).to_upper())
	elif kind == "surviveRounds":
		lines.append("ROUNDS  /  %d" % int(objective.get("rounds", 1)))
	return "\n".join(lines)


func _new_objective_name() -> String:
	var kind := str(OBJECTIVE_KINDS[objective_kind_index].id)
	if kind == "defeatAllOpponents":
		return "Defeat all opponents"
	if kind == "surviveRounds":
		return "Survive %d round%s" % [
			objective_rounds, "" if objective_rounds == 1 else "s",
		]
	var target_name := "target"
	if not objective_targets.is_empty():
		target_name = str(objective_targets[objective_target_index].get("name", "target"))
	return ("Defeat " if kind == "defeatTarget" else "Protect ") + target_name


func _new_objective_summary() -> String:
	var kind := str(OBJECTIVE_KINDS[objective_kind_index].id)
	var lines := PackedStringArray([
		_new_objective_name(),
		"%s  /  SIDE %s" % [
			str(OBJECTIVE_KINDS[objective_kind_index].label), objective_side.to_upper(),
		],
	])
	if kind == "defeatTarget" or kind == "protectTarget":
		lines.append(
			"TARGET  /  %s" % (
				str(objective_targets[objective_target_index].get("id", "")).to_upper()
				if not objective_targets.is_empty()
				else "NO PLACEMENT AVAILABLE"
			)
		)
	elif kind == "surviveRounds":
		lines.append("ROUNDS  /  %d" % objective_rounds)
	return "\n".join(lines)


func _update_objective_view() -> void:
	var mode_labels := {
		"assigned": "VIEW  /  ASSIGNED",
		"available": "VIEW  /  AVAILABLE",
		"new": "VIEW  /  CREATE NEW",
	}
	objective_mode_button.text = str(mode_labels.get(objective_mode, "OBJECTIVES"))
	var browsing := objective_mode != "new"
	objective_previous_button.visible = browsing
	objective_next_button.visible = browsing
	objective_options.visible = not browsing
	if browsing:
		objective_previous_button.disabled = objective_candidates.size() < 2
		objective_next_button.disabled = objective_candidates.size() < 2
		if objective_candidates.is_empty():
			objective_candidate_label.text = (
				"No objectives are assigned to this encounter."
				if objective_mode == "assigned"
				else "No compatible unassigned objectives are available."
			)
		else:
			objective_candidate_label.text = _objective_summary(
				objective_candidates[objective_candidate_index]
			)
		objective_primary_button.text = (
			"REMOVE FROM ENCOUNTER" if objective_mode == "assigned" else "ATTACH TO ENCOUNTER"
		)
		objective_primary_button.disabled = objective_candidates.is_empty() or (
			objective_mode == "assigned" and _current_objective_ids().size() <= 1
		)
		return

	var kind := str(OBJECTIVE_KINDS[objective_kind_index].id)
	objective_candidate_label.text = _new_objective_summary()
	objective_kind_button.text = "KIND  /  %s" % str(OBJECTIVE_KINDS[objective_kind_index].label)
	objective_side_button.text = "SIDE  /  %s" % objective_side.to_upper()
	var targeted := kind == "defeatTarget" or kind == "protectTarget"
	objective_target_button.visible = targeted
	objective_rounds_row.visible = kind == "surviveRounds"
	if targeted:
		objective_target_button.text = (
			"TARGET  /  %s" % str(objective_targets[objective_target_index].get("name", "Unit"))
			if not objective_targets.is_empty()
			else "TARGET  /  NONE AVAILABLE"
		)
	objective_rounds_label.text = str(objective_rounds)
	objective_primary_button.text = "CREATE AND ATTACH"
	objective_primary_button.disabled = targeted and objective_targets.is_empty()


func _unique_objective_id(base: String) -> String:
	var existing: Array[String] = []
	for value: Variant in project.get("objectives", []):
		if value is Dictionary:
			existing.append(str(value.get("id", "")))
	if not existing.has(base):
		return base
	var sequence := 2
	while existing.has("%s_%d" % [base, sequence]):
		sequence += 1
	return "%s_%d" % [base, sequence]


func _apply_objective_action() -> void:
	if objective_primary_button.disabled:
		return
	var encounter := _current_encounter()
	if encounter.is_empty():
		return
	var node: Dictionary = encounter.node
	if objective_mode == "new":
		var kind := str(OBJECTIVE_KINDS[objective_kind_index].id)
		var base_ids := {
			"defeatAllOpponents": "defeat_all_opponents",
			"defeatTarget": "defeat_target",
			"protectTarget": "protect_target",
			"surviveRounds": "survive_rounds",
		}
		var objective_id := _unique_objective_id(str(base_ids[kind]))
		var target_id := ""
		if kind == "defeatTarget" or kind == "protectTarget":
			target_id = str(objective_targets[objective_target_index].get("id", ""))
		var added: Dictionary = compiler.add_encounter_objective(
			source,
			str(encounter.get("campaign_id", "")),
			str(node.get("id", "")),
			objective_id,
			_new_objective_name(),
			kind,
			objective_side,
			target_id,
			objective_rounds if kind == "surviveRounds" else 0
		)
		_accept_objective_edit(added, "CREATED %s" % objective_id.to_upper(), objective_id)
		return

	var objective: Dictionary = objective_candidates[objective_candidate_index]
	var objective_id := str(objective.get("id", ""))
	var ids := _current_objective_ids()
	var message := ""
	if objective_mode == "assigned":
		ids.erase(objective_id)
		message = "REMOVED %s FROM ENCOUNTER" % objective_id.to_upper()
	else:
		ids.append(objective_id)
		message = "ATTACHED %s" % objective_id.to_upper()
	var packed := PackedStringArray(ids)
	var updated: Dictionary = compiler.set_encounter_objectives(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		packed
	)
	_accept_objective_edit(updated, message, objective_id)


func _accept_objective_edit(updated: Dictionary, message: String, selected_id: String) -> void:
	if not updated.ok:
		_set_status("OBJECTIVE REFUSED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	objective_mode = "assigned"
	_refresh_objective_targets()
	_refresh_objective_candidates()
	for index in objective_candidates.size():
		if str(objective_candidates[index].get("id", "")) == selected_id:
			objective_candidate_index = index
			break
	_update_objective_view()
	_set_status("UNSAVED  /  %s" % message, false)
	objective_mode_button.call_deferred("grab_focus")


func _open_surroundings() -> void:
	if deployment_editing:
		_set_status("APPLY OR CANCEL THE DEPLOYMENT DRAFT FIRST", true)
		return
	var map := _current_map()
	if map.is_empty():
		_set_status("ADD A MAP BEFORE EDITING ITS SURROUNDINGS", true)
		return
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	surroundings_mode = str(value.get("mode", "solid"))
	if surroundings_mode not in ["solid", "automatic", "manual"]:
		surroundings_mode = "solid"
	surroundings_depth = clampi(
		int(value.get("depth", SURROUNDINGS_RECOMMENDED_DEPTH)), 1, 8
	)
	var encoded := str(value.get("color", "#07131d"))
	surroundings_colour = Color(encoded) if Color.html_is_valid(encoded) else Color("07131d")
	for channel in surroundings_channel_sliders.size():
		surroundings_channel_sliders[channel].set_value_no_signal(
			roundi(surroundings_colour[channel] * 255.0)
		)
	surroundings_layer.visible = true
	_update_surroundings_view()
	surroundings_first_button.call_deferred("grab_focus")


func _close_surroundings() -> void:
	surroundings_layer.visible = false
	setup_button.call_deferred("grab_focus")


func _cycle_surroundings_mode() -> void:
	var modes := ["solid", "automatic", "manual"]
	surroundings_mode = modes[(modes.find(surroundings_mode) + 1) % modes.size()]
	_update_surroundings_view()


func _change_surroundings_depth(delta: int) -> void:
	surroundings_depth = clampi(surroundings_depth + delta, 1, 8)
	_update_surroundings_view()


func _set_surroundings_channel(value: float, channel: int) -> void:
	var component := clampf(value / 255.0, 0.0, 1.0)
	if channel == 0:
		surroundings_colour.r = component
	elif channel == 1:
		surroundings_colour.g = component
	else:
		surroundings_colour.b = component
	_update_surroundings_view()


func _surrounding_perimeter_count(width: int, height: int, depth: int) -> int:
	return (width + depth * 2) * (height + depth * 2) - width * height


func _update_surroundings_view() -> void:
	var mode_labels := {
		"solid": "MODE  /  SOLID COLOUR",
		"automatic": "MODE  /  AUTOMATIC TERRAIN",
		"manual": "MODE  /  MANUAL TERRAIN",
	}
	surroundings_mode_button.text = str(mode_labels.get(surroundings_mode, "MODE"))
	surroundings_colour_controls.visible = surroundings_mode == "solid"
	surroundings_depth_row.visible = surroundings_mode != "solid"
	surroundings_depth_label.text = "DEPTH  /  %d CELLS" % surroundings_depth
	surroundings_colour_preview.color = surroundings_colour
	surroundings_colour_label.text = "#" + surroundings_colour.to_html(false).to_upper()
	for channel in surroundings_channel_labels.size():
		surroundings_channel_labels[channel].text = str(
			roundi(surroundings_colour[channel] * 255.0)
		)
	var map := _current_map()
	var width := int(map.get("width", 1))
	var height := int(map.get("height", 1))
	if surroundings_mode == "solid":
		surroundings_guidance_label.text = (
			"The chosen colour fills every exposed area beyond the playable map.\n"
			+ "No terrain cells need to be authored."
		)
		surroundings_guidance_label.add_theme_color_override("font_color", Color("72e08f"))
		return
	var required := _surrounding_perimeter_count(width, height, surroundings_depth)
	var recommended := _surrounding_perimeter_count(
		width, height, SURROUNDINGS_RECOMMENDED_DEPTH
	)
	var coverage := (
		"Depth %d guarantees current PC camera coverage at every scroll position."
		% SURROUNDINGS_RECOMMENDED_DEPTH
		if surroundings_depth >= SURROUNDINGS_RECOMMENDED_DEPTH
		else (
			"COVERAGE WARNING  /  add %d more perimeter cells by using depth %d."
			% [recommended - required, SURROUNDINGS_RECOMMENDED_DEPTH]
		)
	)
	if surroundings_mode == "automatic":
		surroundings_guidance_label.text = (
			"%d presentation-only cells will be generated from the nearest map edge.\n"
			+ "Each edge continues its nearest terrain without becoming visitable.\n%s"
		) % [required, coverage]
	else:
		var value: Dictionary = (
			map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
		)
		var authored := 0
		if str(value.get("mode", "")) == "manual" and int(value.get("depth", 0)) == surroundings_depth:
			authored = value.get("terrain", []).size()
		surroundings_guidance_label.text = (
			"Populate exactly %d presentation-only cells; %d currently match this depth.\n"
			+ "Apply starts missing cells from the automatic edge result, then paint the outside ring.\n%s"
		) % [required, authored, coverage]
	surroundings_guidance_label.add_theme_color_override(
		"font_color",
		Color("ffca63") if surroundings_depth < SURROUNDINGS_RECOMMENDED_DEPTH else Color("72e08f")
	)


func _automatic_surrounding_terrain(map: Dictionary, depth: int) -> Array[String]:
	var result: Array[String] = []
	var width := int(map.get("width", 1))
	var height := int(map.get("height", 1))
	for y in range(-depth, height + depth):
		for x in range(-depth, width + depth):
			if x >= 0 and y >= 0 and x < width and y < height:
				continue
			result.append(_automatic_surrounding_name(map, x, y))
	return result


func _apply_surroundings() -> void:
	var map := _current_map()
	if map.is_empty():
		return
	var terrain_ids := PackedStringArray()
	if surroundings_mode == "manual":
		var required := _surrounding_perimeter_count(
			int(map.get("width", 1)), int(map.get("height", 1)), surroundings_depth
		)
		var current: Dictionary = (
			map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
		)
		var values: Array = current.get("terrain", [])
		if str(current.get("mode", "")) != "manual" or int(current.get("depth", 0)) != surroundings_depth or values.size() != required:
			values = _automatic_surrounding_terrain(map, surroundings_depth)
		for value: Variant in values:
			terrain_ids.append(str(value))
	var updated: Dictionary = compiler.set_map_surroundings(
		source,
		str(map.get("id", "")),
		surroundings_mode,
		"#" + surroundings_colour.to_html(false) if surroundings_mode == "solid" else "",
		0 if surroundings_mode == "solid" else surroundings_depth,
		terrain_ids
	)
	if not updated.ok:
		_set_status("SURROUNDINGS REFUSED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	active_tool = "surroundings" if surroundings_mode == "manual" else "brush"
	_read_source()
	surroundings_layer.visible = false
	_refresh_maps_after_edit(0)
	_update_tool_selection()
	var message := (
		"MANUAL OUTSIDE RING READY  /  CHOOSE TERRAIN AND PAINT"
		if surroundings_mode == "manual"
		else "OUTSIDE AREA  /  %s" % surroundings_mode.to_upper()
	)
	_set_status("UNSAVED  /  " + message, false)


func _paint_surrounding_cell(ring_index: int) -> void:
	var map := _current_map()
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	if str(value.get("mode", "")) != "manual":
		_set_status("CHOOSE OUTSIDE  /  MANUAL TERRAIN TO PAINT THIS RING", true)
		return
	var changed: Array = value.get("terrain", []).duplicate()
	if ring_index < 0 or ring_index >= changed.size():
		_set_status("OUTSIDE CELL IS NOT EDITABLE", true)
		return
	if str(changed[ring_index]) == selected_terrain:
		return
	changed[ring_index] = selected_terrain
	var terrain_ids := PackedStringArray()
	for terrain_id: Variant in changed:
		terrain_ids.append(str(terrain_id))
	var updated: Dictionary = compiler.set_map_surroundings(
		source, str(map.get("id", "")), "manual", "",
		int(value.get("depth", SURROUNDINGS_RECOMMENDED_DEPTH)), terrain_ids
	)
	if not updated.ok:
		_set_status("OUTSIDE EDIT REFUSED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	active_tool = "surroundings"
	_read_source()
	_refresh_maps_after_edit(-1)
	call_deferred("_focus_grid_child", _ring_grid_child(ring_index))
	_set_status(
		"UNSAVED  /  OUTSIDE CELL %d OF %d  /  %s" % [
			ring_index + 1, changed.size(), selected_terrain.to_upper()
		], false
	)


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
	var outside_value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	var outside_depth := int(outside_value.get("depth", 0))
	var manual_outside := str(outside_value.get("mode", "")) == "manual"
	var outside_before := (
		_surrounding_perimeter_count(old_width, old_height, outside_depth)
		if manual_outside else 0
	)
	var outside_after := (
		_surrounding_perimeter_count(resize_width, resize_height, outside_depth)
		if manual_outside else 0
	)
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
		"outside_before": outside_before,
		"outside_after": outside_after,
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
	if manual_outside:
		lines.append(
			"Manual outside ring: %d → %d cells; matching coordinates stay painted and new cells use %s."
			% [outside_before, outside_after, selected_terrain.to_upper()]
		)
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


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status("SAVE FAILED  /  %s" % saved.message, true)
		return false
	dirty = false
	_set_status("SAVED  /  PORTABLE PROJECT SOURCE", false)
	_update_actions()
	return true


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
	if dirty and not _save():
		return
	LaunchContext.stage_package(compiled.package, campaign, true)
	get_tree().change_scene_to_file("res://main.tscn")


func _open_story() -> void:
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _open_army() -> void:
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://army_editor.tscn")


func _request_projects() -> void:
	if dirty and Time.get_ticks_msec() > discard_armed_until:
		discard_armed_until = Time.get_ticks_msec() + 3500
		_set_status("UNSAVED CHANGES  /  SAVE, OR PRESS CANCEL AGAIN TO DISCARD", true)
		return
	get_tree().change_scene_to_file("res://campaign_editor.tscn")


func _diagnostic_code(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	return str(diagnostics[0].get("code", "PROJECT INCOMPLETE")).replace("_", " ").to_upper()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT INCOMPLETE"
	var detail := str(diagnostics[0].get("detail", ""))
	return detail.to_upper() if not detail.is_empty() else _diagnostic_code(result)


func _focus_cell(index: int) -> void:
	if grid.get_child_count() == 0:
		return
	var safe := clampi(_map_grid_child(index), 0, grid.get_child_count() - 1)
	_focus_grid_child(safe)


func _focus_grid_child(index: int) -> void:
	if grid.get_child_count() == 0:
		return
	var safe := clampi(index, 0, grid.get_child_count() - 1)
	var child := grid.get_child(safe)
	if child is Control and child.is_inside_tree():
		child.grab_focus()


func _map_grid_child(index: int) -> int:
	var map := _current_map()
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	var safe := clampi(index, 0, width * height - 1)
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	var mode := str(value.get("mode", "none"))
	var depth := clampi(int(value.get("depth", 0)), 0, 8) if mode in ["automatic", "manual"] else 0
	var outer_width := width + depth * 2
	return (floori(float(safe) / float(width)) + depth) * outer_width + safe % width + depth


func _ring_grid_child(ring_index: int) -> int:
	var map := _current_map()
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	var depth := clampi(int(value.get("depth", 0)), 0, 8)
	var seen := 0
	var child := 0
	for y in range(-depth, height + depth):
		for x in range(-depth, width + depth):
			if x < 0 or y < 0 or x >= width or y >= height:
				if seen == ring_index:
					return child
				seen += 1
			child += 1
	return 0


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
	var subtitle := _label("1  SHAPE THE MAP   →   2  WRITE THE STORY   →   3  TEST", 30, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button("SAVE", 150, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button("UNDO", 130, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	play_button = _button("TEST CAMPAIGN", 240, 58)
	play_button.tooltip_text = "Saves valid changes, then starts this campaign."
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var story := _button("NEXT: STORY", 220, 58)
	story.tooltip_text = "Saves valid changes, then opens the story."
	story.pressed.connect(_open_story)
	toolbar.add_child(story)
	more_button = _button("MORE", 160, 58)
	more_button.pressed.connect(_open_more)
	toolbar.add_child(more_button)
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
	tools_column.add_child(_label("ENCOUNTER PREVIEW", 17, Color("69d9ff")))
	encounter_select = OptionButton.new()
	encounter_select.custom_minimum_size = Vector2(330, 52)
	encounter_select.add_theme_font_size_override("font_size", 15)
	encounter_select.item_selected.connect(_select_encounter)
	tools_column.add_child(encounter_select)
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
	setup_button = _button("BATTLE SETUP", 330, 52)
	setup_button.add_theme_font_size_override("font_size", 15)
	setup_button.tooltip_text = "Characters, goals, deployment, co-op, and map surroundings"
	setup_button.pressed.connect(_open_setup)
	tools_column.add_child(setup_button)
	deployment_actions = HBoxContainer.new()
	deployment_actions.add_theme_constant_override("separation", 6)
	deployment_actions.visible = false
	tools_column.add_child(deployment_actions)
	deployment_apply_button = _button("APPLY ZONE", 162, 46)
	deployment_apply_button.add_theme_font_size_override("font_size", 13)
	deployment_apply_button.pressed.connect(_apply_deployment_edit)
	deployment_actions.add_child(deployment_apply_button)
	var deployment_cancel := _button("CANCEL", 162, 46)
	deployment_cancel.add_theme_font_size_override("font_size", 13)
	deployment_cancel.pressed.connect(_cancel_deployment_edit)
	deployment_actions.add_child(deployment_cancel)
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

	board_panel = PanelContainer.new()
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
	_build_placement_overlay()
	_build_objective_overlay()
	_build_resize_overlay()
	_build_surroundings_overlay()
	_build_setup_overlay()
	_build_more_overlay()


func _open_setup() -> void:
	if deployment_editing:
		_set_status("APPLY OR CANCEL THE DEPLOYMENT DRAFT FIRST", true)
		return
	setup_layer.visible = true
	setup_first_button.call_deferred("grab_focus")


func _close_setup() -> void:
	setup_layer.visible = false
	setup_button.call_deferred("grab_focus")


func _choose_setup_tool(name: String) -> void:
	setup_layer.visible = false
	_select_tool(name)


func _setup_add_character() -> void:
	setup_layer.visible = false
	_open_add_placement()


func _setup_deployment() -> void:
	setup_layer.visible = false
	_begin_deployment_edit()


func _setup_objectives() -> void:
	setup_layer.visible = false
	_open_objectives()


func _setup_surroundings() -> void:
	setup_layer.visible = false
	_open_surroundings()


func _build_setup_overlay() -> void:
	setup_layer = Control.new()
	setup_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	setup_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	setup_layer.visible = false
	setup_layer.z_index = 30
	add_child(setup_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.88)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	setup_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(390, 150)
	panel.size = Vector2(1140, 780)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	setup_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label("BATTLE SETUP", 24, Color("69d9ff")))
	var explanation := _label(
		"The map is ready to paint without these. Open setup only when you want "
		+ "to change characters, victory rules, co-op, or the area beyond the board.",
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 68
	column.add_child(explanation)
	var choices := GridContainer.new()
	choices.columns = 2
	choices.add_theme_constant_override("h_separation", 14)
	choices.add_theme_constant_override("v_separation", 12)
	column.add_child(choices)
	add_unit_button = _button("ADD CHARACTER", 500, 66)
	add_unit_button.pressed.connect(_setup_add_character)
	choices.add_child(add_unit_button)
	setup_first_button = add_unit_button
	tool_buttons["add_unit"] = add_unit_button
	var move_unit := _button("MOVE CHARACTER", 500, 66)
	move_unit.pressed.connect(_choose_setup_tool.bind("unit"))
	choices.add_child(move_unit)
	tool_buttons["unit"] = move_unit
	var remove_unit := _button("REMOVE CHARACTER", 500, 66)
	remove_unit.pressed.connect(_choose_setup_tool.bind("remove_unit"))
	choices.add_child(remove_unit)
	tool_buttons["remove_unit"] = remove_unit
	deployment_button = _button("DEPLOYMENT AREA", 500, 66)
	deployment_button.pressed.connect(_setup_deployment)
	choices.add_child(deployment_button)
	tool_buttons["deployment"] = deployment_button
	objective_button = _button("VICTORY GOALS", 500, 66)
	objective_button.pressed.connect(_setup_objectives)
	choices.add_child(objective_button)
	var ownership_button := _button("WHO CONTROLS EACH ALLY", 500, 66)
	ownership_button.pressed.connect(_choose_setup_tool.bind("ownership"))
	choices.add_child(ownership_button)
	tool_buttons["ownership"] = ownership_button
	var scaling_button := _button("TWO-PLAYER ENEMY SCALING", 500, 66)
	scaling_button.pressed.connect(_choose_setup_tool.bind("scaling"))
	choices.add_child(scaling_button)
	tool_buttons["scaling"] = scaling_button
	surroundings_button = _button("MAP SURROUNDINGS", 500, 66)
	surroundings_button.pressed.connect(_setup_surroundings)
	choices.add_child(surroundings_button)
	tool_buttons["surroundings"] = surroundings_button
	var back := _button("BACK TO MAP", 500, 62)
	back.pressed.connect(_close_setup)
	column.add_child(back)
	_update_tool_selection()


func _open_more() -> void:
	more_layer.visible = true
	more_first_button.call_deferred("grab_focus")


func _close_more() -> void:
	more_layer.visible = false
	more_button.call_deferred("grab_focus")


func _more_redo() -> void:
	more_layer.visible = false
	_redo()


func _more_new_map() -> void:
	_open_new_chapter()


func _more_resize() -> void:
	more_layer.visible = false
	_open_resize()


func _more_look() -> void:
	more_layer.visible = false
	_open_look()


func _more_army() -> void:
	more_layer.visible = false
	_open_army()


func _more_projects() -> void:
	more_layer.visible = false
	_request_projects()


func _open_look() -> void:
	look_refreshing = true
	look_theme.select(maxi(0, art.THEME_NAMES.find(str(
		project.get("themeId", "temperate")
	))))
	look_style.select(maxi(0, art.STYLE_NAMES.find(str(
		project.get("characterStyleId", "medieval")
	))))
	look_refreshing = false
	_refresh_look_preview()
	look_layer.visible = true
	look_theme.call_deferred("grab_focus")


func _close_look() -> void:
	look_layer.visible = false
	more_layer.visible = true
	look_button.call_deferred("grab_focus")


func _select_look(index: int, field: String) -> void:
	if look_refreshing:
		return
	var options: Array = art.THEME_NAMES if field == "themeId" else art.STYLE_NAMES
	if index < 0 or index >= options.size():
		return
	var updated: Dictionary = compiler.set_project_presentation(
		source, field, str(options[index])
	)
	if not updated.ok:
		_set_status("LOOK NOT CHANGED  /  %s" % _diagnostic_detail(updated), true)
		return
	_push_undo(source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = true
	_read_source()
	_refresh_palette()
	_refresh_grid()
	_refresh_look_preview()
	_set_status(
		"UNSAVED  /  %s  /  SHARED BY PC AND CONSOLES" % str(options[index]).to_upper(),
		false
	)
	_update_actions()


func _refresh_look_preview() -> void:
	var terrain_names := ["grass", "forest", "road", "water"]
	for index in look_terrain_previews.size():
		look_terrain_previews[index].texture = _terrain_icon(terrain_names[index], 0)
	look_character_preview.texture = null
	look_character_label.text = "CHARACTER PREVIEW"
	var unit_types: Array = project.get("unitTypes", [])
	if unit_types.is_empty() or not unit_types[0] is Dictionary:
		return
	var unit: Dictionary = unit_types[0]
	var preview: Dictionary = compiler.source_character_preview(
		source, str(unit.get("id", ""))
	)
	if not preview.ok:
		return
	var frame: Dictionary = art.character_frame(preview, "idle")
	var image := AtlasTexture.new()
	image.atlas = art.texture(str(frame.path))
	image.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
	look_character_preview.texture = image
	look_character_label.text = str(unit.get("name", "CHARACTER")).to_upper()


func _next_chapter_sequence() -> int:
	var ids: Array[String] = []
	for value: Variant in maps:
		if value is Dictionary:
			ids.append(str(value.get("id", "")))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var flow_value: Variant = campaign_value.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if node_value is Dictionary:
				ids.append(str(node_value.get("id", "")))
	var sequence := 1
	while (
		ids.has("chapter_%d_map" % sequence)
		or ids.has("chapter_%d_story" % sequence)
		or ids.has("chapter_%d_battle" % sequence)
	):
		sequence += 1
	return sequence


func _open_new_chapter() -> void:
	var encounter := _current_encounter()
	if encounter.is_empty():
		more_layer.visible = false
		_set_status("CHOOSE A PLAYABLE BATTLE BEFORE ADDING ITS NEXT CHAPTER", true)
		return
	var sequence := _next_chapter_sequence()
	chapter_title.text = "Chapter %d" % sequence
	more_layer.visible = false
	chapter_layer.visible = true
	chapter_title.call_deferred("grab_focus")
	chapter_title.call_deferred("select_all")


func _close_new_chapter() -> void:
	chapter_layer.visible = false
	more_button.grab_focus()


func _create_chapter_from_text(_value: String) -> void:
	_create_chapter()


func _create_chapter() -> void:
	var title := chapter_title.text.strip_edges()
	if title.is_empty():
		_set_status("GIVE THE CHAPTER A NAME", true)
		chapter_title.grab_focus()
		return
	var encounter := _current_encounter()
	if encounter.is_empty():
		_set_status("THE PRECEDING BATTLE IS NO LONGER AVAILABLE", true)
		_close_new_chapter()
		return
	var sequence := _next_chapter_sequence()
	var base := "chapter_%d" % sequence
	var added: Dictionary = compiler.add_campaign_chapter(
		source,
		str(encounter.get("campaign_id", "")),
		str(encounter.node.get("id", "")),
		base + "_map",
		title,
		base + "_story",
		"Before %s" % title,
		base + "_battle",
		"%s Battle" % title,
		selected_terrain
	)
	if not added.ok:
		_set_status("CHAPTER NOT CREATED  /  %s" % _diagnostic_detail(added), true)
		return
	_push_undo(source)
	source = str(added.source)
	redo_stack.clear()
	dirty = true
	_read_source()
	for index in maps.size():
		if str(maps[index].get("id", "")) == base + "_map":
			map_index = index
			break
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	chapter_layer.visible = false
	_refresh_maps_after_edit(0)
	_set_status(
		"UNSAVED  /  %s IS READY TO SHAPE, WRITE, AND TEST" % title.to_upper(),
		false
	)


func _build_more_overlay() -> void:
	more_layer = Control.new()
	more_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	more_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	more_layer.visible = false
	more_layer.z_index = 30
	add_child(more_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.88)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	more_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(560, 150)
	panel.size = Vector2(800, 780)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	more_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("MORE MAP OPTIONS", 24, Color("69d9ff")))
	var explanation := _label(
		"These actions are useful, but not part of the basic make → story → test flow.",
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 54
	column.add_child(explanation)
	redo_button = _button("REDO LAST EDIT", 680, 70)
	redo_button.pressed.connect(_more_redo)
	column.add_child(redo_button)
	var new_map_button := _button("ADD NEXT CHAPTER", 680, 70)
	new_map_button.pressed.connect(_more_new_map)
	column.add_child(new_map_button)
	more_first_button = new_map_button
	var army_button := _button("GROW THE ARMY", 680, 70)
	army_button.pressed.connect(_more_army)
	column.add_child(army_button)
	look_button = _button("LOOK & FEEL", 680, 70)
	look_button.pressed.connect(_more_look)
	column.add_child(look_button)
	var resize_button := _button("RESIZE THIS MAP", 680, 70)
	resize_button.pressed.connect(_more_resize)
	column.add_child(resize_button)
	var projects := _button("RETURN TO PROJECTS", 680, 70)
	projects.pressed.connect(_more_projects)
	column.add_child(projects)
	var back := _button("BACK TO MAP", 680, 70)
	back.pressed.connect(_close_more)
	column.add_child(back)
	_build_chapter_overlay()
	_build_look_overlay()


func _build_chapter_overlay() -> void:
	chapter_layer = Control.new()
	chapter_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	chapter_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	chapter_layer.visible = false
	chapter_layer.z_index = 40
	add_child(chapter_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.9)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	chapter_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 270)
	panel.size = Vector2(1060, 520)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	chapter_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("ADD NEXT CHAPTER", 20, Color("69d9ff")))
	column.add_child(_label("Keep the adventure moving.", 34, Color("f2f7f9")))
	var explanation := _label(
		"This adds a story moment, a playable battle, and a new map after the "
		+ "current battle. Characters, enemies, co-op ownership, and goals start "
		+ "from the working setup so you can reshape them instead of rebuilding them.",
		19,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 130
	column.add_child(explanation)
	column.add_child(_label("CHAPTER NAME", 16, Color("69d9ff")))
	chapter_title = LineEdit.new()
	chapter_title.custom_minimum_size = Vector2(940, 68)
	chapter_title.max_length = 140
	chapter_title.add_theme_font_size_override("font_size", 24)
	chapter_title.text_submitted.connect(_create_chapter_from_text)
	column.add_child(chapter_title)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	chapter_create_button = _button("CREATE & SHAPE MAP", 420, 72)
	chapter_create_button.pressed.connect(_create_chapter)
	actions.add_child(chapter_create_button)
	var back := _button("BACK", 260, 72)
	back.pressed.connect(_close_new_chapter)
	actions.add_child(back)


func _build_look_overlay() -> void:
	look_layer = Control.new()
	look_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	look_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	look_layer.visible = false
	look_layer.z_index = 40
	add_child(look_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	look_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 120)
	panel.size = Vector2(1200, 840)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	look_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("LOOK & FEEL", 20, Color("69d9ff")))
	column.add_child(_label("Choose the shared art direction.", 34, Color("f2f7f9")))
	var explanation := _label(
		"These are real assets used by the game and both console ports. They change presentation only; maps, characters, and rules stay portable.",
		17, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 54
	column.add_child(explanation)
	var choices := HBoxContainer.new()
	choices.add_theme_constant_override("separation", 18)
	column.add_child(choices)
	var theme_column := VBoxContainer.new()
	theme_column.custom_minimum_size.x = 540
	theme_column.add_theme_constant_override("separation", 7)
	choices.add_child(theme_column)
	theme_column.add_child(_label("WORLD THEME", 15, Color("69d9ff")))
	look_theme = OptionButton.new()
	look_theme.custom_minimum_size = Vector2(540, 60)
	look_theme.add_theme_font_size_override("font_size", 20)
	for name: String in art.THEME_NAMES:
		look_theme.add_item(name.to_upper())
	look_theme.item_selected.connect(_select_look.bind("themeId"))
	theme_column.add_child(look_theme)
	var style_column := VBoxContainer.new()
	style_column.custom_minimum_size.x = 540
	style_column.add_theme_constant_override("separation", 7)
	choices.add_child(style_column)
	style_column.add_child(_label("CHARACTER STYLE", 15, Color("69d9ff")))
	look_style = OptionButton.new()
	look_style.custom_minimum_size = Vector2(540, 60)
	look_style.add_theme_font_size_override("font_size", 20)
	for name: String in art.STYLE_NAMES:
		look_style.add_item(name.to_upper())
	look_style.item_selected.connect(_select_look.bind("characterStyleId"))
	style_column.add_child(look_style)
	column.add_child(_label("ACTUAL GAME ASSETS", 15, Color("69d9ff")))
	var previews := HBoxContainer.new()
	previews.custom_minimum_size.y = 250
	previews.add_theme_constant_override("separation", 12)
	column.add_child(previews)
	look_terrain_previews.clear()
	for _index in 4:
		var terrain := TextureRect.new()
		terrain.custom_minimum_size = Vector2(180, 220)
		terrain.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		terrain.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		terrain.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
		previews.add_child(terrain)
		look_terrain_previews.append(terrain)
	var character_column := VBoxContainer.new()
	character_column.custom_minimum_size.x = 300
	previews.add_child(character_column)
	look_character_preview = TextureRect.new()
	look_character_preview.custom_minimum_size = Vector2(300, 195)
	look_character_preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	look_character_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	look_character_preview.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	character_column.add_child(look_character_preview)
	look_character_label = _label("CHARACTER PREVIEW", 15, Color("f2f7f9"))
	look_character_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	character_column.add_child(look_character_label)
	var done := _button("DONE", 320, 64)
	done.pressed.connect(_close_look)
	column.add_child(done)


func _build_placement_overlay() -> void:
	placement_layer = Control.new()
	placement_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	placement_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	placement_layer.visible = false
	add_child(placement_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.86)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	placement_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(520, 220)
	panel.size = Vector2(880, 620)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	placement_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label("ADD ENCOUNTER UNIT", 24, Color("69d9ff")))
	var explanation := _label(
		"Choose an unused campaign member for an ally, or any unit type for an "
		+ "enemy. Then choose its starting cell on the board.",
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 72
	column.add_child(explanation)
	placement_first_button = _button("TOGGLE SIDE", 360, 64)
	placement_first_button.pressed.connect(_toggle_placement_side)
	placement_side_button = placement_first_button
	column.add_child(placement_side_button)
	placement_candidate_label = _label("", 25, Color("f2f7f9"))
	placement_candidate_label.custom_minimum_size.y = 110
	placement_candidate_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	column.add_child(placement_candidate_label)
	var candidate_row := HBoxContainer.new()
	candidate_row.add_theme_constant_override("separation", 12)
	column.add_child(candidate_row)
	var previous := _button("PREVIOUS", 240, 60)
	previous.pressed.connect(_cycle_placement_candidate.bind(-1))
	candidate_row.add_child(previous)
	var next := _button("NEXT", 240, 60)
	next.pressed.connect(_cycle_placement_candidate.bind(1))
	candidate_row.add_child(next)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	placement_apply_button = _button("PLACE ON BOARD", 300, 64)
	placement_apply_button.pressed.connect(_begin_add_placement)
	actions.add_child(placement_apply_button)
	var cancel := _button("CANCEL", 220, 64)
	cancel.pressed.connect(_close_add_placement)
	actions.add_child(cancel)


func _build_objective_overlay() -> void:
	objective_layer = Control.new()
	objective_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	objective_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	objective_layer.visible = false
	add_child(objective_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.86)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	objective_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 130)
	panel.size = Vector2(1060, 820)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("ffca63"))
	)
	objective_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("ENCOUNTER OBJECTIVES", 24, Color("ffca63")))
	var explanation := _label(
		"Attach reusable project objectives or create a complete win condition. "
		+ "Targeted objectives use portable member or placement identities.",
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 66
	column.add_child(explanation)
	objective_first_button = _button("VIEW", 420, 60)
	objective_first_button.pressed.connect(_cycle_objective_mode)
	objective_mode_button = objective_first_button
	column.add_child(objective_mode_button)
	objective_candidate_label = _label("", 22, Color("f2f7f9"))
	objective_candidate_label.custom_minimum_size.y = 150
	objective_candidate_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	column.add_child(objective_candidate_label)
	var navigation := HBoxContainer.new()
	navigation.add_theme_constant_override("separation", 12)
	column.add_child(navigation)
	objective_previous_button = _button("PREVIOUS", 240, 56)
	objective_previous_button.pressed.connect(_cycle_objective_candidate.bind(-1))
	navigation.add_child(objective_previous_button)
	objective_next_button = _button("NEXT", 240, 56)
	objective_next_button.pressed.connect(_cycle_objective_candidate.bind(1))
	navigation.add_child(objective_next_button)

	objective_options = VBoxContainer.new()
	objective_options.add_theme_constant_override("separation", 10)
	column.add_child(objective_options)
	var kind_side := HBoxContainer.new()
	kind_side.add_theme_constant_override("separation", 12)
	objective_options.add_child(kind_side)
	objective_kind_button = _button("KIND", 610, 56)
	objective_kind_button.add_theme_font_size_override("font_size", 15)
	objective_kind_button.pressed.connect(_cycle_objective_kind.bind(1))
	kind_side.add_child(objective_kind_button)
	objective_side_button = _button("SIDE", 300, 56)
	objective_side_button.add_theme_font_size_override("font_size", 15)
	objective_side_button.pressed.connect(_toggle_objective_side)
	kind_side.add_child(objective_side_button)
	objective_target_button = _button("TARGET", 922, 56)
	objective_target_button.add_theme_font_size_override("font_size", 15)
	objective_target_button.pressed.connect(_cycle_objective_target)
	objective_options.add_child(objective_target_button)
	objective_rounds_row = HBoxContainer.new()
	objective_rounds_row.add_theme_constant_override("separation", 10)
	objective_options.add_child(objective_rounds_row)
	var rounds_minus := _button("−", 120, 56)
	rounds_minus.pressed.connect(_change_objective_rounds.bind(-1))
	objective_rounds_row.add_child(rounds_minus)
	objective_rounds_label = _label("3", 25, Color("f2f7f9"))
	objective_rounds_label.custom_minimum_size = Vector2(160, 56)
	objective_rounds_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	objective_rounds_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	objective_rounds_row.add_child(objective_rounds_label)
	var rounds_plus := _button("+", 120, 56)
	rounds_plus.pressed.connect(_change_objective_rounds.bind(1))
	objective_rounds_row.add_child(rounds_plus)
	objective_rounds_row.add_child(_label("ROUNDS TO SURVIVE", 17, Color("718896")))

	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	objective_primary_button = _button("APPLY", 420, 62)
	objective_primary_button.add_theme_font_size_override("font_size", 15)
	objective_primary_button.pressed.connect(_apply_objective_action)
	actions.add_child(objective_primary_button)
	var close := _button("CLOSE", 220, 62)
	close.pressed.connect(_close_objectives)
	actions.add_child(close)


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


func _build_surroundings_overlay() -> void:
	surroundings_layer = Control.new()
	surroundings_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	surroundings_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	surroundings_layer.visible = false
	add_child(surroundings_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.86)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	surroundings_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 130)
	panel.size = Vector2(1060, 820)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("72e08f"))
	)
	surroundings_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label("NON-PLAYABLE MAP SURROUNDINGS", 24, Color("72e08f")))
	var explanation := _label(
		"These visuals sit outside tactical map coordinates, so units can never "
		+ "enter them. Choose a colour, generated edge terrain, or a ring you paint yourself.",
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 72
	column.add_child(explanation)
	surroundings_first_button = _button("MODE", 520, 60)
	surroundings_first_button.pressed.connect(_cycle_surroundings_mode)
	surroundings_mode_button = surroundings_first_button
	column.add_child(surroundings_mode_button)

	surroundings_depth_row = HBoxContainer.new()
	surroundings_depth_row.add_theme_constant_override("separation", 12)
	column.add_child(surroundings_depth_row)
	var depth_minus := _button("−", 110, 58)
	depth_minus.pressed.connect(_change_surroundings_depth.bind(-1))
	surroundings_depth_row.add_child(depth_minus)
	surroundings_depth_label = _label("DEPTH", 22, Color("f2f7f9"))
	surroundings_depth_label.custom_minimum_size = Vector2(360, 58)
	surroundings_depth_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	surroundings_depth_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	surroundings_depth_row.add_child(surroundings_depth_label)
	var depth_plus := _button("+", 110, 58)
	depth_plus.pressed.connect(_change_surroundings_depth.bind(1))
	surroundings_depth_row.add_child(depth_plus)

	surroundings_colour_controls = VBoxContainer.new()
	surroundings_colour_controls.add_theme_constant_override("separation", 8)
	column.add_child(surroundings_colour_controls)
	var colour_header := HBoxContainer.new()
	colour_header.add_theme_constant_override("separation", 16)
	surroundings_colour_controls.add_child(colour_header)
	colour_header.add_child(_label("OUTSIDE COLOUR", 16, Color("718896")))
	surroundings_colour_preview = ColorRect.new()
	surroundings_colour_preview.custom_minimum_size = Vector2(120, 38)
	surroundings_colour_preview.mouse_filter = Control.MOUSE_FILTER_IGNORE
	colour_header.add_child(surroundings_colour_preview)
	surroundings_colour_label = _label("#07131D", 20, Color("f2f7f9"))
	surroundings_colour_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	colour_header.add_child(surroundings_colour_label)
	for channel in 3:
		var channel_row := HBoxContainer.new()
		channel_row.add_theme_constant_override("separation", 12)
		surroundings_colour_controls.add_child(channel_row)
		var channel_name: String = ["RED", "GREEN", "BLUE"][channel]
		var channel_title := _label(channel_name, 16, Color("69d9ff"))
		channel_title.custom_minimum_size.x = 110
		channel_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		channel_row.add_child(channel_title)
		var slider := HSlider.new()
		slider.min_value = 0
		slider.max_value = 255
		slider.step = 1
		slider.custom_minimum_size = Vector2(590, 42)
		slider.value_changed.connect(_set_surroundings_channel.bind(channel))
		channel_row.add_child(slider)
		surroundings_channel_sliders.append(slider)
		var channel_value := _label("0", 19, Color("f2f7f9"))
		channel_value.custom_minimum_size.x = 70
		channel_value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		channel_value.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		channel_row.add_child(channel_value)
		surroundings_channel_labels.append(channel_value)

	surroundings_guidance_label = _label("", 19, Color("72e08f"))
	surroundings_guidance_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	surroundings_guidance_label.custom_minimum_size.y = 150
	column.add_child(surroundings_guidance_label)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	surroundings_apply_button = _button("APPLY OUTSIDE AREA", 360, 62)
	surroundings_apply_button.pressed.connect(_apply_surroundings)
	actions.add_child(surroundings_apply_button)
	var cancel := _button("CANCEL", 220, 62)
	cancel.pressed.connect(_close_surroundings)
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
