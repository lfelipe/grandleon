# SPDX-License-Identifier: MIT
extends Control

const ArtLibrary = preload("res://scripts/art_library.gd")
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)
const TILE_SOURCE := 32
const BLOB_COLUMNS := 8
const HISTORY_LIMIT := 64
const SURROUNDINGS_RECOMMENDED_DEPTH := 3
const TERRAIN_PALETTE := [
	"grass", "forest", "mountain", "water", "road", "bridge", "sand",
	"ruins", "farmland", "snow", "swamp", "hills", "bamboo", "paved",
]
const LANDMARKS := {
	"landmark_house": {
		"kind": "village_house",
		"source_id": "mountain_village_house",
		"label_key": "map.landmark.village_house",
		"clear_terrain": "paved",
	},
	"landmark_grove": {
		"kind": "ancient_grove",
		"source_id": "mountain_ancient_grove",
		"label_key": "map.landmark.ancient_grove",
		"clear_terrain": "forest",
	},
}
const OBJECTIVE_KINDS := [
	{"id": "defeatAllOpponents", "label_key": "map.objective.kind.defeat_all"},
	{"id": "defeatTarget", "label_key": "map.objective.kind.defeat_target"},
	{"id": "protectTarget", "label_key": "map.objective.kind.protect_target"},
	{"id": "surviveRounds", "label_key": "map.objective.kind.survive"},
]
const FORCE_MODES := ["opening", "once", "repeating"]
const FORCE_BEHAVIORS := ["hold", "patrol", "pursue"]

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
var deployment_capacity_draft := 0
var deployment_capacity_original := 0
var tool_before_deployment := "brush"
var patrol_editing := false
var patrol_placement_id := ""
var patrol_draft: Array[int] = []
var patrol_original: Array[int] = []
var tool_before_patrol := "brush"
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
var budget_report: Dictionary = {}
var discard_armed_until := 0
var resize_width := 1
var resize_height := 1
var resize_preview: Dictionary = {}
var surroundings_mode := "continue"
var surroundings_depth := SURROUNDINGS_RECOMMENDED_DEPTH
var surroundings_colour := Color("07131d")
var surroundings_fill_terrain := "grass"
var preview_players := 1
var force_candidates: Array[Dictionary] = []
var force_candidate_index := 0
var force_mode := "opening"
var force_round := 2
var force_every := 2
var force_times := 2
var force_behavior := "hold"
var moment_candidates: Array[Dictionary] = []
var moment_candidate_index := 0
var moment_target_candidates: Array[Dictionary] = []
var moment_target_index := 0
var moment_two_player_only := false

var title_label: Label
var map_select: OptionButton
var encounter_select: OptionButton
var experience_select: OptionButton
var tool_buttons: Dictionary = {}
var palette_list: VBoxContainer
var palette_buttons: Dictionary = {}
var grid: GridContainer
var empty_label: Label
var status_label: Label
var target_budget_label: Label
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
var deployment_capacity_actions: HBoxContainer
var deployment_capacity_label: Label
var deployment_capacity_minus: Button
var deployment_capacity_plus: Button
var patrol_actions: HBoxContainer
var patrol_apply_button: Button
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
var forces_layer: Control
var forces_first_button: Button
var forces_candidate_label: Label
var forces_mode_button: Button
var forces_round_label: Label
var forces_repeat_row: HBoxContainer
var forces_every_label: Label
var forces_times_label: Label
var forces_behavior_button: Button
var forces_patrol_button: Button
var forces_scaling_button: Button
var forces_apply_button: Button
var moments_layer: Control
var moments_first_button: Button
var moments_summary_label: Label
var moments_target_button: Button
var moments_players_button: Button
var moments_edit_button: Button
var moments_remove_button: Button
var moments_earlier_button: Button
var moments_later_button: Button
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
var surroundings_terrain_row: HBoxContainer
var surroundings_terrain_button: Button
var surroundings_colour_controls: VBoxContainer
var surroundings_colour_preview: ColorRect
var surroundings_colour_label: Label
var surroundings_channel_labels: Array[Label] = []
var surroundings_channel_sliders: Array[HSlider] = []
var surroundings_guidance_label: Label
var surroundings_apply_button: Button
var surroundings_first_button: Button
var interior_visibility_layer: Control
var visibility_first_button: Button
var visibility_help_label: Label
var visibility_radius_label: Label
var visibility_summary_label: Label
var visibility_radius := 2
var board_panel: PanelContainer
var setup_button: Button
var setup_layer: Control
var setup_first_button: Button
var copy_setup_button: Button
var copy_setup_layer: Control
var copy_setup_select: OptionButton
var copy_setup_summary: Label
var copy_setup_candidates: Array[Dictionary] = []
var talk_layer: Control
var talk_title: Label
var talk_guidance: Label
var talk_write_button: Button
var talk_remove_button: Button
var talk_campaign_id := ""
var talk_node_id := ""
var talk_placement_id := ""
var talk_display_name := ""
var talk_cell_index := 0
var more_button: Button
var more_layer: Control
var more_first_button: Button
var rename_layer: Control
var rename_button: Button
var rename_map_input: LineEdit
var rename_battle_input: LineEdit
var rename_apply_button: Button
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
var delete_map_button: Button
var dependency_layer: Control
var dependency_heading: Label
var dependency_summary: Label
var dependency_uses: RichTextLabel
var dependency_confirm_button: Button
var dependency_close_button: Button
var dependency_category := "map"
var dependency_id := ""


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		if dependency_layer != null and dependency_layer.visible:
			_close_dependency_review()
		elif talk_layer != null and talk_layer.visible:
			_close_talk_choice()
		elif look_layer != null and look_layer.visible:
			_close_look()
		elif surroundings_layer != null and surroundings_layer.visible:
			_close_surroundings()
		elif interior_visibility_layer != null and interior_visibility_layer.visible:
			_close_visibility()
		elif objective_layer != null and objective_layer.visible:
			_close_objectives()
		elif forces_layer != null and forces_layer.visible:
			_close_forces()
		elif moments_layer != null and moments_layer.visible:
			_close_moments()
		elif placement_layer != null and placement_layer.visible:
			_close_add_placement()
		elif resize_layer != null and resize_layer.visible:
			_close_resize()
		elif copy_setup_layer != null and copy_setup_layer.visible:
			_close_copy_setup()
		elif setup_layer != null and setup_layer.visible:
			_close_setup()
		elif chapter_layer != null and chapter_layer.visible:
			_close_new_chapter()
		elif rename_layer != null and rename_layer.visible:
			_close_rename()
		elif more_layer != null and more_layer.visible:
			_close_more()
		elif deployment_editing:
			_cancel_deployment_edit()
		elif patrol_editing:
			_cancel_patrol_edit()
		else:
			_request_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status(_t("map.error.no_project"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status(_t("map.error.open", {"message": loaded.message}), true)
		return
	source = loaded.source
	if not _read_source():
		return
	title_label.text = _t("map.project_title", {
		"project": str(project.get("title", _t("map.fallback.untitled_game"))).to_upper(),
	})
	_refresh_maps()
	_set_status(_t("map.status.ready"), false)
	var diagnostic_focus: Dictionary = CreatorContext.take_diagnostic_focus()
	if not diagnostic_focus.is_empty():
		call_deferred("_apply_diagnostic_focus", diagnostic_focus)
		return
	var requested_action: String = CreatorContext.take_map_action()
	if requested_action == "add_chapter":
		call_deferred("_open_new_chapter")
	else:
		map_select.call_deferred("grab_focus")


func _apply_diagnostic_focus(diagnostic: Dictionary) -> void:
	var focus_index := -1
	var x := int(diagnostic.get("x", -1))
	var y := int(diagnostic.get("y", -1))
	var map := _current_map()
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	if x >= 0 and y >= 0 and x < width and y < height:
		focus_index = y * width + x

	var object_id := str(diagnostic.get("object_id", ""))
	var current := _current_encounter()
	if not object_id.is_empty() and not current.is_empty():
		var prefix := "%s/%s/" % [
			current.get("campaign_id", ""),
			current.node.get("id", ""),
		]
		for placement_value: Variant in current.node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			if str(compiler.stable_source_id(prefix + str(placement.get("id", "")))) != object_id:
				continue
			selected_placement_id = str(placement.get("id", ""))
			active_tool = "move"
			if focus_index < 0:
				var placement_x := int(placement.get("x", -1))
				var placement_y := int(placement.get("y", -1))
				if placement_x >= 0 and placement_y >= 0 and placement_x < width and placement_y < height:
					focus_index = placement_y * width + placement_x
			break

	_refresh_grid(focus_index)
	_update_tool_selection()
	var object_name := str(diagnostic.get("object", _t("map.fallback.this_unit"))).to_upper()
	if not selected_placement_id.is_empty():
		_set_status(
			_t("map.status.fix_unit_cell", {"object": object_name, "x": x, "y": y}),
			true
		)
	elif focus_index >= 0:
		_set_status(
			_t("map.status.fix_cell", {
				"x": x, "y": y,
				"field": _field_label(str(diagnostic.get("field", "map"))),
			}),
			true
		)
	else:
		_set_status(
			_t("map.status.fix_setup", {"field": _field_label(str(
				diagnostic.get("field", diagnostic.get("code", "project_incomplete"))
			))}),
			true
		)
		setup_button.call_deferred("grab_focus")


func _read_source() -> bool:
	var parsed := JSON.new()
	if parsed.parse(source) != OK or not parsed.data is Dictionary:
		_set_status(_t("map.error.source_unreadable"), true)
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
			map_select.add_item(str(value.get("name", value.get("id", _t("map.fallback.untitled_map")))))
	map_index = clampi(map_index, 0, maxi(0, maps.size() - 1))
	if not maps.is_empty():
		map_select.select(map_index)
	_refresh_encounters()
	_refresh_target_budget()
	_refresh_palette()
	_refresh_grid()
	_update_actions()


func _select_map(index: int) -> void:
	if index < 0 or index >= maps.size():
		return
	map_index = index
	if active_tool in ["surroundings", "visibility"]:
		active_tool = "brush"
	_reset_deployment_edit()
	_reset_patrol_edit()
	encounter_index = 0
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	_refresh_encounters()
	_refresh_palette()
	_refresh_grid(0)
	_update_tool_selection()
	_set_status(_t("map.status.map", {"name": _current_map_name().to_upper()}), false)


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
				"campaign_name": str(campaign.get("name", campaign.get("id", _t("map.fallback.campaign")))),
				"campaign": campaign,
				"node": node,
			})
			if (
				str(node.get("id", "")) == CreatorContext.encounter_id
				and (
					CreatorContext.campaign_id.is_empty()
					or str(campaign.get("id", "")) == CreatorContext.campaign_id
				)
			):
				requested_encounter_index = encounters.size() - 1
			encounter_select.add_item(
				"%s / %s" % [
					str(campaign.get("name", _t("map.fallback.campaign"))),
					str(node.get("name", node.get("id", _t("map.fallback.battle")))),
				]
			)
	encounter_select.disabled = encounters.is_empty()
	if encounters.is_empty():
		encounter_select.add_item(_t("map.encounter.none"))
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
	_refresh_target_budget()
	_reset_deployment_edit()
	_reset_patrol_edit()
	selected_placement_id = ""
	pending_placement = {}
	_refresh_grid(0)
	var node: Dictionary = encounters[index].node
	_set_status(_t("map.status.battle", {
		"name": str(node.get("name", node.get("id", ""))).to_upper(),
	}), false)


func _select_experience_preview(index: int) -> void:
	if experience_select == null or index < 0 or index >= experience_select.item_count:
		return
	preview_players = clampi(int(experience_select.get_item_metadata(index)), 1, 2)
	selected_placement_id = ""
	pending_placement = {}
	_refresh_grid(0)
	_set_status(
		_t("map.status.preview", {"mode": _t(
			"map.preview.solo" if preview_players == 1 else "map.preview.coop"
		)}),
		false
	)


func _placement_preview_position(placement: Dictionary) -> Vector2i:
	if preview_players >= 2 and placement.get("twoPlayerPosition", {}) is Dictionary:
		var alternate: Dictionary = placement.get("twoPlayerPosition", {})
		if alternate.has("x") and alternate.has("y"):
			return Vector2i(int(alternate.x), int(alternate.y))
	return Vector2i(int(placement.get("x", -1)), int(placement.get("y", -1)))


func _placement_visible_in_preview(placement: Dictionary) -> bool:
	return int(placement.get("minimumPlayers", 1)) <= preview_players


func _current_encounter() -> Dictionary:
	if encounter_index < 0 or encounter_index >= encounters.size():
		return {}
	return encounters[encounter_index]


func _refresh_target_budget() -> void:
	if compiler == null or target_budget_label == null:
		return
	budget_report = compiler.target_budget_report(source)
	target_budget_label.text = _target_impact_summary()
	var over := _current_stage_is_over_budget()
	target_budget_label.add_theme_color_override(
		"font_color", Color("ffca63") if over else Color("a9bdc9")
	)
	target_budget_label.tooltip_text = (
		_t("map.targets.tooltip")
	)


func _budget_target(target_id: String) -> Dictionary:
	for value: Variant in budget_report.get("targets", []):
		if value is Dictionary and str(value.get("id", "")) == target_id:
			return value
	return {}


func _current_stage_units() -> int:
	var encounter := _current_encounter()
	if encounter.is_empty() or compiler == null:
		return 0
	var owner_id := str(compiler.stable_source_id(
		"%s/%s" % [encounter.get("campaign_id", ""), encounter.node.get("id", "")]
	))
	for value: Variant in budget_report.get("stages", []):
		if value is Dictionary and str(value.get("owner_id", "")) == owner_id:
			return int(value.get("units", 0))
	return 0


func _current_stage_is_over_budget(additional_units: int = 0) -> bool:
	var units := _current_stage_units() + additional_units
	for target_id: String in ["nintendo64", "playstation"]:
		var target := _budget_target(target_id)
		var limit := int(target.get("units_per_encounter", 0))
		if limit > 0 and units > limit:
			return true
	return false


func _target_impact_summary(additional_units: int = 0) -> String:
	if not bool(budget_report.get("ok", false)):
		return _t("map.targets.unavailable")
	if _current_encounter().is_empty():
		return _t("map.targets.choose_battle")
	var units := _current_stage_units() + additional_units
	var n64_limit := int(_budget_target("nintendo64").get("units_per_encounter", 0))
	var ps1_limit := int(_budget_target("playstation").get("units_per_encounter", 0))
	var prefix := _t("map.targets.after_add" if additional_units > 0 else "map.targets.label")
	var summary := _t("map.targets.summary", {
		"prefix": prefix, "units": units, "n64_limit": n64_limit, "ps1_limit": ps1_limit,
	})
	if _current_stage_is_over_budget(additional_units):
		summary += _t("map.targets.over_budget_suffix")
	return summary


func _current_map() -> Dictionary:
	if map_index < 0 or map_index >= maps.size() or not maps[map_index] is Dictionary:
		return {}
	return maps[map_index]


func _current_map_name() -> String:
	var map := _current_map()
	return str(map.get("name", map.get("id", _t("map.fallback.untitled_map"))))


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
		if (
			not name.is_empty()
			and _landmark_definition_for_source(name).is_empty()
			and not names.has(name)
		):
			names.append(name)
	if (
		not _landmark_definition_for_source(selected_terrain).is_empty()
		or (not names.has(selected_terrain) and not names.is_empty())
	):
		selected_terrain = names[0]
	for name: String in names:
		var button := _button(_terrain_label(name), 330, 54)
		button.icon = _terrain_icon(name, 0)
		button.tooltip_text = _t("map.terrain.paint_tooltip", {"terrain": _terrain_label(name)})
		button.pressed.connect(_select_terrain.bind(name))
		palette_list.add_child(button)
		palette_buttons[name] = button
	_update_palette_selection()


func _select_terrain(name: String) -> void:
	var landmark := _landmark_definition_for_source(name)
	if not landmark.is_empty():
		_select_tool(str(landmark.tool))
		return
	selected_terrain = name
	_update_palette_selection()
	_set_status(_t("map.status.terrain", {"terrain": _terrain_label(name)}), false)
	_focus_cell(0)


func _select_tool(name: String) -> void:
	if deployment_editing and name != "deployment":
		_reset_deployment_edit()
	if patrol_editing and name != "patrol":
		_reset_patrol_edit()
	active_tool = name
	rectangle_anchor = -1
	selected_placement_id = ""
	if name != "add_unit":
		pending_placement = {}
	_update_tool_selection()
	_set_status(
		_t("map.status.tool_talk")
		if name == "talk"
		else _t("map.status.tool", {"tool": _tool_label(name)}),
		false
	)
	_focus_cell(0)


func _update_tool_selection() -> void:
	for name: String in tool_buttons:
		var button: Button = tool_buttons[name]
		var display := _tool_label(name)
		button.text = ("● " if name == active_tool else "  ") + display
	if setup_button != null:
		var setup_names := [
			"add_unit", "unit", "remove_unit", "deployment", "ownership",
			"scaling", "talk", "surroundings", "visibility",
		]
		setup_button.text = (
			"● %s" % _tool_label(active_tool, true)
			if setup_names.has(active_tool)
			else _t("map.setup.open")
		)


func _update_palette_selection() -> void:
	for name: String in palette_buttons:
		var button: Button = palette_buttons[name]
		button.text = ("● " if name == selected_terrain else "  ") + _terrain_label(name)


func _refresh_grid(focus_index := -1) -> void:
	for child in grid.get_children():
		grid.remove_child(child)
		child.queue_free()
	var map := _current_map()
	empty_label.visible = map.is_empty()
	grid.visible = not map.is_empty()
	if map.is_empty():
		empty_label.text = _t("map.empty")
		return
	var width := maxi(1, int(map.get("width", 1)))
	var height := maxi(1, int(map.get("height", 1)))
	var terrain: Array = map.get("terrain", [])
	var landmark_cells := _landmark_cells_by_index(width, height, terrain)
	var visibility_cells := _visibility_cells(map)
	var outside: Dictionary = _surroundings_preview_value(map)
	if outside.is_empty():
		outside = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
		)
	var outside_mode := str(outside.get("mode", "none"))
	var outside_depth := (
		clampi(int(outside.get("depth", 0)), 0, 8)
		if outside_mode in ["continue", "automatic", "fill", "blend", "manual"] else 0
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
	var patrol_cells: Array[int] = []
	var objective_targets_by_cell: Dictionary = {}
	var encounter := _current_encounter()
	if not encounter.is_empty():
		var node: Dictionary = encounter.node
		deployment_cells = (
			deployment_draft.duplicate()
			if deployment_editing
			else _deployment_indices(node, width)
		)
		if patrol_editing:
			patrol_cells.assign(patrol_draft)
		objective_targets_by_cell = _objective_target_cells(node, width)
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			if not _placement_visible_in_preview(placement):
				continue
			var position := _placement_preview_position(placement)
			var placement_x := position.x
			var placement_y := position.y
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
				elif outside_mode == "fill":
					outside_name = str(outside.get("terrainId", selected_terrain))
				elif outside_mode == "blend" and _natural_blend_uses_fill(
					outer_x, outer_y, width, height, outside_depth
				):
					outside_name = str(outside.get("terrainId", selected_terrain))
				var outside_cell := _button("", tile_size, tile_size)
				outside_cell.icon = _terrain_icon(outside_name, 255)
				outside_cell.expand_icon = true
				outside_cell.modulate = Color(0.72, 0.78, 0.82, 0.92)
				outside_cell.tooltip_text = _t("map.cell.outside_tooltip", {
					"x": outer_x, "y": outer_y, "terrain": _terrain_label(outside_name),
				})
				outside_cell.pressed.connect(_paint_surrounding_cell.bind(ring_index))
				grid.add_child(outside_cell)
				ring_index += 1
				continue
			var index := outer_y * width + outer_x
			var name := str(terrain[index]) if index < terrain.size() else "grass"
			var cell := _button("", tile_size, tile_size)
			cell.icon = _terrain_icon(name, _terrain_mask(index, width, height, terrain))
			cell.expand_icon = true
			var landmark: Dictionary = landmark_cells.get(index, {})
			cell.tooltip_text = _t("map.cell.tooltip", {
				"x": outer_x, "y": outer_y, "terrain": _terrain_label(name),
				"landmark": (
					_t("map.cell.landmark_suffix", {
						"landmark": _landmark_kind_label(str(landmark.kind)),
					})
					if not landmark.is_empty() else ""
				),
			})
			if visibility_cells.has(index):
				_add_visibility_marker(cell, tile_size)
			_add_playable_boundary(cell, outer_x, outer_y, width, height, tile_size)
			if not landmark.is_empty() and landmark.anchor == Vector2i(outer_x, outer_y):
				_add_landmark_badge(cell, str(landmark.kind), tile_size)
			if placements_by_cell.has(index):
				_add_placement_badge(cell, placements_by_cell[index])
			if deployment_cells.has(index):
				_add_deployment_badge(cell, tile_size, deployment_editing)
			if patrol_cells.has(index):
				_add_patrol_badge(cell, tile_size, patrol_cells.find(index) + 1)
			if objective_targets_by_cell.has(index):
				_add_objective_marker(cell, objective_targets_by_cell[index])
			cell.pressed.connect(_paint_cell.bind(index))
			grid.add_child(cell)
	if focus_index >= 0:
		call_deferred("_focus_cell", focus_index)


func _add_playable_boundary(
	cell: Control, x: int, y: int, width: int, height: int, tile_size: int
) -> void:
	var colour := Color("69d9ff")
	colour.a = 0.92
	var edges := []
	if x == 0:
		edges.append(Rect2(0, 0, 3, tile_size))
	if x == width - 1:
		edges.append(Rect2(tile_size - 3, 0, 3, tile_size))
	if y == 0:
		edges.append(Rect2(0, 0, tile_size, 3))
	if y == height - 1:
		edges.append(Rect2(0, tile_size - 3, tile_size, 3))
	for edge: Rect2 in edges:
		var line := ColorRect.new()
		line.position = edge.position
		line.size = edge.size
		line.color = colour
		line.mouse_filter = Control.MOUSE_FILTER_IGNORE
		cell.add_child(line)


func _cluster_landmark_for_cell(
	x: int, y: int, width: int, height: int, terrain: Array
) -> Dictionary:
	var layout := _landmark_layout(width, height, terrain)
	if not bool(layout.ok):
		return {}
	var index := y * width + x
	for feature_value: Variant in layout.features:
		var feature: Dictionary = feature_value
		if feature.cells.has(index):
			return feature
	return {}


func _landmark_definition_for_source(source_id: String) -> Dictionary:
	for tool: String in LANDMARKS:
		var definition: Dictionary = LANDMARKS[tool]
		if str(definition.source_id) == source_id:
			var result := definition.duplicate()
			result["tool"] = tool
			return result
	return {}


func _landmark_layout(width: int, height: int, terrain: Array) -> Dictionary:
	if width < 1 or height < 1 or terrain.size() != width * height:
		return {"ok": false, "detail": _t("map.landmark.error.incomplete_terrain"), "features": []}
	var visited: Dictionary = {}
	var features: Array[Dictionary] = []
	for y in height:
		for x in width:
			var index := y * width + x
			if visited.has(index):
				continue
			var source_id := str(terrain[index])
			var definition := _landmark_definition_for_source(source_id)
			if definition.is_empty():
				continue
			if x + 1 >= width or y + 1 >= height:
				return {
					"ok": false,
					"detail": _t("map.landmark.error.outside", {
						"landmark": _t(str(definition.label_key)), "x": x, "y": y,
					}),
					"features": features,
				}
			var cells: Array[int] = [
				index, index + 1, index + width, index + width + 1,
			]
			for cell: int in cells:
				if visited.has(cell) or str(terrain[cell]) != source_id:
					return {
						"ok": false,
						"detail": _t("map.landmark.error.footprint", {
							"landmark": _t(str(definition.label_key)), "x": x, "y": y,
						}),
						"features": features,
					}
			for cell: int in cells:
				visited[cell] = true
			features.append({
				"anchor": Vector2i(x, y),
				"kind": str(definition.kind),
				"source_id": source_id,
				"clear_terrain": str(definition.clear_terrain),
				"label": _t(str(definition.label_key)),
				"cells": cells,
			})
	return {"ok": true, "detail": "", "features": features}


func _landmark_cells_by_index(width: int, height: int, terrain: Array) -> Dictionary:
	var by_cell := {}
	var layout := _landmark_layout(width, height, terrain)
	if not bool(layout.ok):
		return by_cell
	for feature_value: Variant in layout.features:
		var feature: Dictionary = feature_value
		for index: int in feature.cells:
			by_cell[index] = feature
	return by_cell


func _add_landmark_badge(cell: Button, kind: String, tile_size: int) -> void:
	var label := Label.new()
	label.text = _t(
		"map.landmark.grove_badge" if kind == "ancient_grove" else "map.landmark.house_badge"
	)
	label.add_theme_font_size_override("font_size", clampi(tile_size / 6, 8, 13))
	label.add_theme_color_override("font_color", Color("fff0b0"))
	label.add_theme_color_override("font_shadow_color", Color("07131d"))
	label.add_theme_constant_override("shadow_offset_x", 2)
	label.add_theme_constant_override("shadow_offset_y", 2)
	label.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE, Control.PRESET_MODE_MINSIZE, 4)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cell.add_child(label)


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
	var arrival: Dictionary = (
		first.get("arrival", {}) if first.get("arrival", {}) is Dictionary else {}
	)
	var arrival_round := int(arrival.get("round", 0))
	label.text = _t("map.badge.move") if selected else (
		"%d×" % placements.size() if placements.size() > 1 else (
			"P%d" % local_player if first_side else (
				"W%d" % arrival_round if arrival_round > 0 else (
					"2P" if two_player_only else (
						_t("map.badge.talk") if first.has("talk") else _t("map.badge.enemy")
					)
				)
			)
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
		var placement_arrival: Dictionary = (
			placement.get("arrival", {})
			if placement.get("arrival", {}) is Dictionary else {}
		)
		var arrival_detail := ""
		var position_detail := ""
		if placement.get("twoPlayerPosition", {}) is Dictionary and not placement.get("twoPlayerPosition", {}).is_empty():
			var solo_position := Vector2i(
				int(placement.get("x", -1)), int(placement.get("y", -1))
			)
			var coop_position: Dictionary = placement.get("twoPlayerPosition", {})
			position_detail = _t("map.placement.position_detail", {
				"solo_x": solo_position.x, "solo_y": solo_position.y,
				"coop_x": int(coop_position.get("x", -1)),
				"coop_y": int(coop_position.get("y", -1)),
			})
		if not placement_arrival.is_empty():
			arrival_detail = _t("map.placement.arrival_round", {
				"round": int(placement_arrival.get("round", 2)),
			})
			if placement_arrival.has("every") and placement_arrival.has("times"):
				arrival_detail += _t("map.placement.arrival_repeat", {
					"every": int(placement_arrival.get("every", 1)),
					"times": int(placement_arrival.get("times", 2)),
				})
		details.append(
			_t("map.placement.tooltip", {
				"side": _side_label(str(placement.get("side", "unit"))),
				"player": (
					_t("map.placement.player_suffix", {
						"player": clampi(int(placement.get("player", 1)), 1, 2),
					})
					if str(placement.get("side", "second")) == "first" else ""
				),
				"players": _t("map.placement.two_players_suffix") if int(placement.get("minimumPlayers", 1)) >= 2 else "",
				"arrival": arrival_detail,
				"position": position_detail,
				"name": _placement_display_name(placement),
				"talk": _t("map.placement.talkable_suffix") if placement.has("talk") else "",
			})
		)
	cell.tooltip_text += "\n" + "\n".join(details)


func _add_deployment_badge(cell: Button, tile_size: int, pending: bool) -> void:
	var label := Label.new()
	label.text = _t("map.badge.draft" if pending else "map.badge.zone")
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
		_t("map.deployment.pending_tile" if pending else "map.deployment.zone")
	)


func _add_patrol_badge(cell: Button, tile_size: int, order: int) -> void:
	var label := Label.new()
	label.text = str(order)
	label.position = Vector2(tile_size - 27, 5)
	label.size = Vector2(22, 22)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_size_override("font_size", 11)
	label.add_theme_color_override("font_color", Color("07111b"))
	var badge := StyleBoxFlat.new()
	badge.bg_color = Color("72e08f")
	badge.set_corner_radius_all(11)
	label.add_theme_stylebox_override("normal", badge)
	cell.add_child(label)
	cell.tooltip_text += "\n" + _t("map.patrol.waypoint", {"order": order})


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
	cell.tooltip_text += "\n" + _t("map.objective.target_marker", {"names": ", ".join(details)})


func _visibility_cells(map: Dictionary) -> Array[int]:
	var result: Array[int] = []
	var value: Variant = map.get("visualVisibility", {})
	if not value is Dictionary:
		return result
	var width := int(map.get("width", 0))
	var height := int(map.get("height", 0))
	for cell_value: Variant in value.get("hiddenCells", []):
		if not cell_value is Dictionary:
			continue
		var x := int(cell_value.get("x", -1))
		var y := int(cell_value.get("y", -1))
		if x >= 0 and y >= 0 and x < width and y < height:
			result.append(y * width + x)
	return result


func _add_visibility_marker(cell: Button, tile_size: int) -> void:
	var shade := ColorRect.new()
	shade.color = Color(0.025, 0.045, 0.065, 0.66)
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cell.add_child(shade)
	var label := _label(_t("map.visibility.cell_badge"), 9, Color("d6c8ff"))
	label.position = Vector2(4, tile_size - 22)
	label.size = Vector2(maxi(28, tile_size - 8), 18)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cell.add_child(label)
	cell.tooltip_text += "\n" + _t("map.visibility.cell_tooltip")


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
		if not _placement_visible_in_preview(placement_value):
			continue
		var source_key := str(placement_value.get("memberId", placement_value.get("id", "")))
		if not targets.has(source_key):
			continue
		var position := _placement_preview_position(placement_value)
		var x := position.x
		var y := position.y
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
		_set_status(_t("map.error.cell_not_editable"), true)
		return
	if active_tool == "deployment":
		_toggle_deployment_cell(index)
	elif active_tool == "patrol":
		_toggle_patrol_cell(index)
	elif active_tool == "add_unit":
		_add_unit_at(index, width)
	elif active_tool == "remove_unit":
		_remove_unit_at(index, width)
	elif active_tool == "ownership":
		_toggle_placement_player_at(index, width)
	elif active_tool == "scaling":
		_toggle_placement_scaling_at(index, width)
	elif active_tool == "talk":
		_toggle_placement_talk_at(index, width)
	elif active_tool == "visibility":
		_toggle_visibility_cell(index)
	elif active_tool == "unit":
		_move_unit_at(index, width)
	elif LANDMARKS.has(active_tool):
		_place_landmark_at(index, width, terrain, active_tool)
	elif active_tool == "landmark_remove":
		_remove_landmark_at(index, width, terrain)
	elif active_tool == "fill":
		_fill_from(index, width, terrain)
	elif active_tool == "rectangle":
		_rectangle_at(index, width, terrain)
	else:
		var changed := terrain.duplicate()
		changed[index] = selected_terrain
		_apply_terrain(changed, index, _t("map.edit.painted", {"terrain": _terrain_label(selected_terrain)}))


func _landmark_footprint_blocker(cells: Array[int], width: int) -> String:
	for encounter_value: Variant in encounters:
		if not encounter_value is Dictionary:
			continue
		var node: Dictionary = encounter_value.get("node", {})
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			var positions: Array[Vector2i] = [Vector2i(
				int(placement.get("x", -1)), int(placement.get("y", -1))
			)]
			if placement.get("twoPlayerPosition", {}) is Dictionary:
				var alternate: Dictionary = placement.get("twoPlayerPosition", {})
				if alternate.has("x") and alternate.has("y"):
					positions.append(Vector2i(int(alternate.x), int(alternate.y)))
			for position: Vector2i in positions:
				if position.x >= 0 and position.y >= 0 and cells.has(
					position.y * width + position.x
				):
					return _t("map.landmark.blocker.characters")
		var deployment: Dictionary = (
			node.get("deployment", {}) if node.get("deployment", {}) is Dictionary else {}
		)
		for tile_value: Variant in deployment.get("tiles", []):
			if not tile_value is Dictionary:
				continue
			var tile: Dictionary = tile_value
			var tile_index := int(tile.get("y", -1)) * width + int(tile.get("x", -1))
			if cells.has(tile_index):
				return _t("map.landmark.blocker.deployment")
	return ""


func _place_landmark_at(index: int, width: int, terrain: Array, tool: String) -> void:
	var definition: Dictionary = LANDMARKS.get(tool, {})
	if definition.is_empty():
		_set_status(_t("map.error.landmark_unavailable"), true)
		return
	var height := terrain.size() / width
	var x := index % width
	var y := index / width
	if x + 1 >= width or y + 1 >= height:
		_set_status(
			_t("map.error.landmark_needs_cells", {
				"landmark": _t(str(definition.label_key)),
			}),
			true
		)
		_focus_cell(index)
		return
	var cells: Array[int] = [
		index, index + 1, index + width, index + width + 1,
	]
	var layout := _landmark_layout(width, height, terrain)
	if not bool(layout.ok):
		_set_status(_t("map.error.landmark_footprint", {"detail": str(layout.detail)}), true)
		_focus_cell(index)
		return
	for feature_value: Variant in layout.features:
		var feature: Dictionary = feature_value
		var overlaps := false
		for cell: int in feature.cells:
			if cells.has(cell):
				overlaps = true
		if overlaps and feature.anchor != Vector2i(x, y):
			_set_status(_t("map.error.landmark_overlap"), true)
			_focus_cell(index)
			return
	var blocker := _landmark_footprint_blocker(cells, width)
	if not blocker.is_empty():
		_set_status(_t("map.error.landmark_not_placed", {"detail": blocker}), true)
		_focus_cell(index)
		return
	var changed := terrain.duplicate()
	for cell: int in cells:
		changed[cell] = str(definition.source_id)
	_apply_terrain(
		changed, index,
		_t("map.edit.landmark_placed", {"landmark": _t(str(definition.label_key))})
	)


func _remove_landmark_at(index: int, width: int, terrain: Array) -> void:
	var height := terrain.size() / width
	var feature := _cluster_landmark_for_cell(
		index % width, index / width, width, height, terrain
	)
	if feature.is_empty():
		_set_status(_t("map.error.choose_complete_landmark"), true)
		_focus_cell(index)
		return
	var changed := terrain.duplicate()
	for cell: int in feature.cells:
		changed[cell] = str(feature.clear_terrain)
	_apply_terrain(
		changed, int(feature.cells[0]),
		_t("map.edit.landmark_removed", {
			"landmark": str(feature.label),
			"terrain": _terrain_label(str(feature.clear_terrain)),
		})
	)


func _placement_at(index: int, width: int) -> Dictionary:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return {}
	var x := index % width
	var y := index / width
	var node: Dictionary = encounter.node
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary or not _placement_visible_in_preview(placement_value):
			continue
		var position := _placement_preview_position(placement_value)
		if position.x == x and position.y == y:
			return placement_value
	return {}


func _unit_type_display_name(unit_type_id: String) -> String:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return str(value.get("name", _t("map.fallback.character")))
	return _t("map.fallback.character")


func _member_display_name(member_id: String) -> String:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return ""
	for value: Variant in encounter.campaign.get("roster", []):
		if value is Dictionary and str(value.get("id", "")) == member_id:
			return str(value.get("name", _t("map.fallback.character")))
	return ""


func _placement_display_name(placement: Dictionary) -> String:
	var authored_name := str(placement.get("name", "")).strip_edges()
	if not authored_name.is_empty():
		return authored_name
	var member_name := _member_display_name(str(placement.get("memberId", "")))
	if not member_name.is_empty():
		return member_name
	return _unit_type_display_name(str(placement.get("unitTypeId", "")))


func _add_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	if encounter.is_empty() or pending_placement.is_empty():
		_set_status(_t("map.error.choose_add_character"), true)
		return
	var node: Dictionary = encounter.node
	var display_name := str(pending_placement.get("name", _t("map.fallback.character")))
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
		_set_status(_t("map.error.add_refused", {"detail": _diagnostic_code(added)}), true)
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
	_set_status(_t("map.status.character_added", {"name": display_name.to_upper()}), false)


func _remove_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status(_t("map.error.choose_unit_cell"), true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var display_name := _placement_display_name(placement)
	var removed: Dictionary = compiler.remove_placement(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		placement_id
	)
	if not removed.ok:
		_set_status(_t("map.error.remove_refused", {"detail": _diagnostic_code(removed)}), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = removed.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(_t("map.status.character_removed", {"name": display_name.to_upper()}), false)


func _toggle_placement_scaling_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status(_t("map.error.choose_enemy_scaling"), true)
		_focus_cell(index)
		return
	if str(placement.get("side", "second")) != "second":
		_set_status(_t("map.error.allies_all_player_counts"), true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var display_name := _placement_display_name(placement)
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
		_set_status(_t("map.error.scaling_refused", {"detail": _diagnostic_detail(updated)}), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(
		_t("map.status.scaling", {
			"name": display_name.to_upper(),
			"state": _t("map.scaling.always" if was_scaled else "map.scaling.coop_only"),
		}),
		false
	)


func _toggle_placement_player_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status(_t("map.error.choose_ally"), true)
		_focus_cell(index)
		return
	if str(placement.get("side", "second")) != "first":
		_set_status(_t("map.error.only_allies_owned"), true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var placement_id := str(placement.get("id", ""))
	var display_name := _placement_display_name(placement)
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
		_set_status(_t("map.error.ownership_refused", {"detail": _diagnostic_detail(updated)}), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(
		_t("map.status.ownership", {"name": display_name.to_upper(), "player": next_player}),
		false
	)


func _toggle_placement_talk_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	var placement := _placement_at(index, width)
	if encounter.is_empty() or placement.is_empty():
		_set_status(_t("map.error.choose_talk_opponent"), true)
		_focus_cell(index)
		return
	if str(placement.get("side", "second")) != "second":
		_set_status(_t("map.error.talk_opponents_only"), true)
		_focus_cell(index)
		return
	var node: Dictionary = encounter.node
	var display_name := _placement_display_name(placement)
	var was_talkable := placement.has("talk")
	if was_talkable:
		_open_talk_choice(
			str(encounter.get("campaign_id", "")),
			str(node.get("id", "")),
			str(placement.get("id", "")),
			display_name,
			index
		)
		return
	var updated: Dictionary = compiler.set_placement_talkable(
		source,
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		str(placement.get("id", "")),
		not was_talkable
	)
	if not updated.ok:
		_set_status(_t("map.error.talk_not_changed", {"detail": _diagnostic_detail(updated)}), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(
		_t("map.status.talk_enabled", {"name": display_name.to_upper()}),
		false
	)
	_open_talk_choice(
		str(encounter.get("campaign_id", "")),
		str(node.get("id", "")),
		str(placement.get("id", "")),
		display_name,
		index
	)


func _talk_node() -> Dictionary:
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != talk_campaign_id:
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			return {}
		for node_value: Variant in flow.get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == talk_node_id:
				return node_value
	return {}


func _talk_dialogue_id() -> String:
	var node := _talk_node()
	for value: Variant in node.get("moments", []):
		if not value is Dictionary:
			continue
		var when: Variant = value.get("when", {})
		if (
			when is Dictionary
			and str(when.get("kind", "")) == "characterTalked"
			and str(when.get("placementId", "")) == talk_placement_id
		):
			return str(value.get("dialogueId", ""))
	return ""


func _open_talk_choice(
	campaign_id: String,
	node_id: String,
	placement_id: String,
	display_name: String,
	cell_index: int
) -> void:
	talk_campaign_id = campaign_id
	talk_node_id = node_id
	talk_placement_id = placement_id
	talk_display_name = display_name
	talk_cell_index = cell_index
	var has_scene := not _talk_dialogue_id().is_empty()
	talk_title.text = _t("map.talk.title", {"name": display_name.to_upper()})
	talk_guidance.text = (
		_t("map.talk.existing_help")
		if has_scene else
		_t("map.talk.new_help")
	)
	talk_write_button.text = _t("map.talk.edit" if has_scene else "map.talk.write")
	talk_remove_button.text = _t("map.talk.remove_with_scene" if has_scene else "map.talk.remove")
	talk_layer.visible = true
	talk_write_button.call_deferred("grab_focus")


func _close_talk_choice() -> void:
	talk_layer.visible = false
	_focus_cell(talk_cell_index)


func _ensure_talk_dialogue() -> String:
	var existing := _talk_dialogue_id()
	if not existing.is_empty():
		return existing
	var added: Dictionary = compiler.add_talk_dialogue(
		source,
		talk_campaign_id,
		talk_node_id,
		talk_placement_id,
		("Conversation with %s" % talk_display_name).left(160),
		"",
		talk_display_name.left(160),
		"..."
	)
	if not added.ok:
		_set_status(_t("map.error.conversation_not_created", {"detail": _diagnostic_detail(added)}), true)
		return ""
	_push_undo(source)
	redo_stack.clear()
	source = str(added.source)
	dirty = true
	_read_source()
	_update_actions()
	return str(added.get("dialogue_id", ""))


func _write_talk_conversation() -> void:
	var dialogue_id := _ensure_talk_dialogue()
	if dialogue_id.is_empty() or (dirty and not _save()):
		return
	talk_layer.visible = false
	CreatorContext.choose_dialogue(dialogue_id)
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _coop_opening_dialogue_id() -> String:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return ""
	var node: Dictionary = encounter.node
	for value: Variant in node.get("moments", []):
		if not value is Dictionary or int(value.get("minimumPlayers", 1)) < 2:
			continue
		var when: Variant = value.get("when", {})
		if when is Dictionary and str(when.get("kind", "")) == "stageOpens":
			return str(value.get("dialogueId", ""))
	return ""


func _coop_opening_character() -> Dictionary:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return {}
	var node: Dictionary = encounter.node
	var placement: Dictionary = {}
	for value: Variant in node.get("placements", []):
		if (
			value is Dictionary
			and str(value.get("side", "first")) == "first"
			and int(value.get("player", 1)) == 2
		):
			placement = value
			break
	if placement.is_empty():
		return {}
	var unit_type_id := str(placement.get("unitTypeId", ""))
	var display_name := str(placement.get("name", ""))
	var member_id := str(placement.get("memberId", ""))
	if not member_id.is_empty():
		var campaign_id := str(encounter.get("campaign_id", ""))
		for campaign_value: Variant in project.get("campaigns", []):
			if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != campaign_id:
				continue
			for member_value: Variant in campaign_value.get("roster", []):
				if member_value is Dictionary and str(member_value.get("id", "")) == member_id:
					unit_type_id = str(member_value.get("unitTypeId", unit_type_id))
					display_name = str(member_value.get("name", display_name))
					break
	if display_name.is_empty():
		for unit_value: Variant in project.get("unitTypes", []):
			if unit_value is Dictionary and str(unit_value.get("id", "")) == unit_type_id:
				display_name = str(unit_value.get("name", unit_type_id))
				break
	if unit_type_id.is_empty():
		return {}
	return {
		"speaker": display_name if not display_name.is_empty() else "Player 2",
		"unit_type_id": unit_type_id,
	}


func _unique_project_id(base: String, keys: Array[String]) -> String:
	var candidate := base
	var sequence := 2
	while keys.has(candidate):
		candidate = "%s_%d" % [base, sequence]
		sequence += 1
	return candidate


func _ensure_coop_opening_dialogue() -> String:
	var existing := _coop_opening_dialogue_id()
	if not existing.is_empty():
		return existing
	var character := _coop_opening_character()
	if character.is_empty():
		_set_status(
			_t("map.error.coop_story_needs_ally"), true
		)
		return ""
	var encounter := _current_encounter()
	if encounter.is_empty():
		return ""
	var portable: Variant = JSON.parse_string(source)
	if not portable is Dictionary:
		_set_status(_t("map.error.coop_story_invalid_source"), true)
		return ""
	var dialogue_ids: Array[String] = []
	for value: Variant in portable.get("dialogues", []):
		if value is Dictionary:
			dialogue_ids.append(str(value.get("id", "")))
	var node: Dictionary = {}
	var campaign_id := str(encounter.get("campaign_id", ""))
	var node_id := str(encounter.node.get("id", ""))
	for campaign_value: Variant in portable.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != campaign_id:
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == node_id:
				node = node_value
				break
	if node.is_empty():
		_set_status(_t("map.error.coop_story_missing_battle"), true)
		return ""
	var dialogue_id := _unique_project_id("%s_coop_opening" % node_id, dialogue_ids)
	var moment_ids: Array[String] = []
	for value: Variant in node.get("moments", []):
		if value is Dictionary:
			moment_ids.append(str(value.get("id", "")))
	var moment_id := _unique_project_id("%s_begins" % dialogue_id, moment_ids)
	var dialogues: Array = portable.get("dialogues", [])
	dialogues.append({
		"id": dialogue_id,
		"name": "%s / Two-player opening" % str(node.get("name", "Battle")),
		"cast": [{
			"speaker": str(character.speaker),
			"unitTypeId": str(character.unit_type_id),
		}],
		"lines": [{
			"speaker": str(character.speaker),
			"text": "...",
		}],
	})
	portable["dialogues"] = dialogues
	var moments: Array = node.get("moments", [])
	moments.append({
		"id": moment_id,
		"when": {"kind": "stageOpens"},
		"dialogueId": dialogue_id,
		"minimumPlayers": 2,
	})
	node["moments"] = moments
	var candidate_source := JSON.stringify(_integer_numbers(portable), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(
			_t("map.error.coop_story_not_created", {"detail": _diagnostic_detail(checked)}), true
		)
		return ""
	_push_undo(source)
	redo_stack.clear()
	source = candidate_source
	dirty = true
	_read_source()
	_update_actions()
	_set_status(_t("map.status.coop_story_created"), false)
	return dialogue_id


func _write_coop_opening_story() -> void:
	var dialogue_id := _ensure_coop_opening_dialogue()
	if dialogue_id.is_empty() or (dirty and not _save()):
		return
	setup_layer.visible = false
	CreatorContext.choose_dialogue(dialogue_id)
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _remove_talk_choice() -> void:
	var updated: Dictionary = compiler.set_placement_talkable(
		source, talk_campaign_id, talk_node_id, talk_placement_id, false
	)
	if not updated.ok:
		_set_status(_t("map.error.talk_not_removed", {"detail": _diagnostic_detail(updated)}), true)
		talk_remove_button.grab_focus()
		return
	_push_undo(source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = true
	_read_source()
	talk_layer.visible = false
	_refresh_maps_after_edit(talk_cell_index)
	_set_status(
		_t("map.status.talk_removed", {"name": talk_display_name.to_upper()}),
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
		_set_status(_t("map.error.deployment_needs_battle"), true)
		return
	var node: Dictionary = encounter.node
	var width := int(map.get("width", 0))
	if width <= 0:
		_set_status(_t("map.error.no_editable_cells"), true)
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
	deployment_capacity_original = (
		int(deployment_value.get("capacity", 0))
		if deployment_value is Dictionary else 0
	)
	deployment_capacity_draft = deployment_capacity_original
	tool_before_deployment = active_tool if active_tool != "deployment" else "brush"
	deployment_editing = true
	active_tool = "deployment"
	selected_placement_id = ""
	pending_placement = {}
	deployment_actions.visible = true
	deployment_capacity_actions.visible = true
	_update_deployment_actions()
	_update_tool_selection()
	_refresh_grid(0)
	_set_status(
		_t("map.deployment.draft_help"),
		false
	)


func _toggle_deployment_cell(index: int) -> void:
	if not deployment_editing:
		_set_status(_t("map.error.choose_deployment_zone"), true)
		return
	var at := deployment_draft.find(index)
	if at >= 0:
		deployment_draft.remove_at(at)
	elif deployment_draft.size() >= 256:
		_set_status(_t("map.error.deployment_limit"), true)
		_focus_cell(index)
		return
	else:
		deployment_draft.append(index)
		deployment_draft.sort()
	_update_deployment_actions()
	_refresh_grid(index)
	_set_status(
		_t(
			"map.deployment.draft_count.one" if deployment_draft.size() == 1
			else "map.deployment.draft_count.many",
			{"count": deployment_draft.size()}
		),
		false
	)


func _update_deployment_actions() -> void:
	var fieldable := _deployment_fieldable_count()
	var maximum := maxi(0, fieldable - 1)
	deployment_capacity_draft = clampi(deployment_capacity_draft, 0, maximum)
	deployment_capacity_label.text = (
		_t("map.deployment.no_limit")
		if deployment_capacity_draft == 0
		else _t("map.deployment.field_up_to_allies", {
			"capacity": deployment_capacity_draft, "allies": fieldable,
		})
	)
	deployment_capacity_minus.disabled = deployment_capacity_draft == 0
	deployment_capacity_plus.disabled = maximum == 0 or deployment_capacity_draft >= maximum
	deployment_apply_button.disabled = (
		deployment_draft == deployment_original
		and deployment_capacity_draft == deployment_capacity_original
	)


func _deployment_fieldable_count() -> int:
	var encounter := _current_encounter()
	if encounter.is_empty():
		return 0
	var count := 0
	for value: Variant in encounter.node.get("placements", []):
		if value is Dictionary and str(value.get("side", "second")) == "first":
			count += 1
	return count


func _change_deployment_capacity(delta: int) -> void:
	var maximum := maxi(0, _deployment_fieldable_count() - 1)
	if maximum == 0:
		deployment_capacity_draft = 0
	elif delta > 0 and deployment_capacity_draft == 0:
		deployment_capacity_draft = 1
	else:
		deployment_capacity_draft = clampi(deployment_capacity_draft + delta, 0, maximum)
	_update_deployment_actions()
	_set_status(
		_t("map.deployment.draft_capacity", {"capacity": deployment_capacity_label.text}),
		false
	)


func _apply_deployment_edit() -> void:
	if not deployment_editing or deployment_apply_button.disabled:
		return
	var encounter := _current_encounter()
	var map := _current_map()
	if encounter.is_empty() or map.is_empty():
		_set_status(_t("map.error.deployment_battle_missing"), true)
		return
	var width := int(map.get("width", 0))
	var tiles: Array[Dictionary] = []
	for index: int in deployment_draft:
		tiles.append({"x": index % width, "y": index / width})
	var node: Dictionary = encounter.node
	var candidate: Dictionary = project.duplicate(true)
	var changed := false
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		var flow: Dictionary = campaign_value.get("flow", {})
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("id", "")) != str(node.get("id", "")):
				continue
			if tiles.is_empty() and deployment_capacity_draft == 0:
				node_value.erase("deployment")
			else:
				var deployment: Dictionary = (
					node_value.get("deployment", {}).duplicate(true)
					if node_value.get("deployment", {}) is Dictionary else {}
				)
				deployment["id"] = deployment_id
				if tiles.is_empty():
					deployment.erase("tiles")
				else:
					deployment["tiles"] = tiles
				if deployment_capacity_draft == 0:
					deployment.erase("capacity")
				else:
					deployment["capacity"] = deployment_capacity_draft
				node_value["deployment"] = deployment
			changed = true
			break
		break
	if not changed:
		_set_status(_t("map.error.deployment_missing_battle"), true)
		return
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(
			_t("map.error.deployment_refused_full", {
				"code": _diagnostic_token(checked), "detail": _diagnostic_detail(checked),
			}),
			true
		)
		return
	var count := deployment_draft.size()
	var capacity := deployment_capacity_draft
	_push_undo(source)
	source = candidate_source
	redo_stack.clear()
	dirty = true
	_reset_deployment_edit()
	_read_source()
	_refresh_maps_after_edit(0)
	var result := ""
	if count == 0:
		result = (
			_t("map.deployment.cleared") if capacity == 0
			else _t("map.deployment.fixed_starts", {"capacity": capacity})
		)
	else:
		result = _t(
			"map.deployment.applied.one" if count == 1 else "map.deployment.applied.many",
			{
				"count": count,
				"capacity": _t("map.deployment.no_limit") if capacity == 0
				else _t("map.deployment.field_up_to", {"capacity": capacity}),
			}
		)
	_set_status(_t("map.status.unsaved_detail", {"detail": result}), false)


func _cancel_deployment_edit() -> void:
	_reset_deployment_edit()
	_refresh_grid(0)
	_set_status(_t("map.status.deployment_discarded"), false)
	setup_button.call_deferred("grab_focus")


func _reset_deployment_edit() -> void:
	if active_tool == "deployment":
		active_tool = tool_before_deployment if not tool_before_deployment.is_empty() else "brush"
	deployment_editing = false
	deployment_id = ""
	deployment_draft.clear()
	deployment_original.clear()
	deployment_capacity_draft = 0
	deployment_capacity_original = 0
	if deployment_actions != null:
		deployment_actions.visible = false
	if deployment_capacity_actions != null:
		deployment_capacity_actions.visible = false
	if not tool_buttons.is_empty():
		_update_tool_selection()


func _begin_patrol_edit() -> void:
	var placement := _selected_force()
	var map := _current_map()
	if placement.is_empty() or map.is_empty():
		_set_status(_t("map.error.patrol_choose_opponent"), true)
		return
	var width := int(map.get("width", 0))
	patrol_placement_id = str(placement.get("id", ""))
	patrol_original.clear()
	for value: Variant in placement.get("patrolPoints", []):
		if value is Dictionary:
			patrol_original.append(int(value.get("y", 0)) * width + int(value.get("x", 0)))
	patrol_draft.assign(patrol_original)
	tool_before_patrol = active_tool if active_tool != "patrol" else "brush"
	patrol_editing = true
	active_tool = "patrol"
	forces_layer.visible = false
	patrol_actions.visible = true
	_update_patrol_actions()
	_update_tool_selection()
	_refresh_grid(0)
	_set_status(_t("map.patrol.choose_waypoints"), false)


func _toggle_patrol_cell(index: int) -> void:
	if not patrol_editing:
		return
	if patrol_draft.has(index):
		_set_status(_t("map.error.patrol_duplicate_cell"), true)
		_focus_cell(index)
		return
	if patrol_draft.size() >= 32:
		_set_status(_t("map.error.patrol_limit"), true)
		return
	patrol_draft.append(index)
	_update_patrol_actions()
	_refresh_grid(index)
	_set_status(_t("map.patrol.added", {"count": patrol_draft.size()}), false)


func _remove_last_patrol_point() -> void:
	if patrol_draft.is_empty():
		return
	var focus: int = patrol_draft.pop_back()
	_update_patrol_actions()
	_refresh_grid(focus)


func _clear_patrol_points() -> void:
	patrol_draft.clear()
	_update_patrol_actions()
	_refresh_grid(0)
	_set_status(_t("map.patrol.cleared_help"), false)


func _update_patrol_actions() -> void:
	patrol_apply_button.disabled = patrol_draft == patrol_original


func _apply_patrol_edit() -> void:
	if not patrol_editing or patrol_apply_button.disabled:
		return
	var encounter := _current_encounter()
	var map := _current_map()
	if encounter.is_empty() or map.is_empty():
		return
	var width := int(map.get("width", 0))
	var points: Array[Dictionary] = []
	for index: int in patrol_draft:
		points.append({"x": index % width, "y": index / width})
	var candidate: Dictionary = project.duplicate(true)
	var changed := false
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		var flow: Dictionary = campaign_value.get("flow", {})
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("id", "")) != str(encounter.node.get("id", "")):
				continue
			for placement_value: Variant in node_value.get("placements", []):
				if not placement_value is Dictionary or str(placement_value.get("id", "")) != patrol_placement_id:
					continue
				if points.is_empty():
					placement_value.erase("patrolPoints")
					placement_value["behavior"] = "hold"
				else:
					placement_value["patrolPoints"] = points
					placement_value["behavior"] = "patrol"
				changed = true
				break
			break
		break
	if not changed:
		_set_status(_t("map.error.patrol_opponent_missing"), true)
		return
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("map.error.patrol_refused", {"detail": _diagnostic_detail(checked)}), true)
		return
	var selected_id := patrol_placement_id
	var count := points.size()
	_push_undo(source)
	source = candidate_source
	redo_stack.clear()
	dirty = true
	_reset_patrol_edit()
	_read_source()
	_refresh_maps_after_edit(0)
	_collect_force_candidates(selected_id)
	_load_force_draft()
	forces_layer.visible = true
	_update_forces_view()
	_set_status(_t("map.status.unsaved_detail", {"detail": (
		_t("map.patrol.cleared") if count == 0 else _t("map.patrol.point_count", {"count": count})
	)}), false)


func _cancel_patrol_edit() -> void:
	_reset_patrol_edit()
	_refresh_grid(0)
	forces_layer.visible = true
	_update_forces_view()
	_set_status(_t("map.status.patrol_discarded"), false)


func _reset_patrol_edit() -> void:
	if active_tool == "patrol":
		active_tool = tool_before_patrol if not tool_before_patrol.is_empty() else "brush"
	patrol_editing = false
	patrol_placement_id = ""
	patrol_draft.clear()
	patrol_original.clear()
	if patrol_actions != null:
		patrol_actions.visible = false
	if not tool_buttons.is_empty():
		_update_tool_selection()


func _move_unit_at(index: int, width: int) -> void:
	var encounter := _current_encounter()
	if encounter.is_empty():
		_set_status(_t("map.error.no_battle_to_edit"), true)
		return
	var node: Dictionary = encounter.node
	var x := index % width
	var y := index / width
	var placements: Array = node.get("placements", [])
	if selected_placement_id.is_empty():
		for placement_value: Variant in placements:
			if not placement_value is Dictionary or not _placement_visible_in_preview(placement_value):
				continue
			var placement: Dictionary = placement_value
			var position := _placement_preview_position(placement)
			if position.x == x and position.y == y:
				selected_placement_id = str(placement.get("id", ""))
				if selected_placement_id.is_empty():
					break
				var display_name := _placement_display_name(placement)
				_refresh_grid(index)
				_set_status(
					_t("map.move.choose_destination", {"name": display_name.to_upper()}),
					false
				)
				return
		_set_status(_t("map.error.choose_unit_cell"), true)
		_focus_cell(index)
		return

	for placement_value: Variant in placements:
		if placement_value is Dictionary and str(placement_value.get("id", "")) == selected_placement_id:
			var position := _placement_preview_position(placement_value)
			if position.x == x and position.y == y:
				selected_placement_id = ""
				_refresh_grid(index)
				_set_status(_t("map.status.move_cancelled"), false)
				return
			break
	var moved_name := _t("map.fallback.character")
	for placement_value: Variant in placements:
		if placement_value is Dictionary and str(placement_value.get("id", "")) == selected_placement_id:
			moved_name = _placement_display_name(placement_value)
			break
	var moved: Dictionary = (
		_move_two_player_position(selected_placement_id, x, y)
		if preview_players >= 2
		else compiler.move_placement(
			source,
			str(encounter.get("campaign_id", "")),
			str(node.get("id", "")),
			selected_placement_id,
			x,
			y
		)
	)
	if not moved.ok:
		_set_status(_t("map.error.move_refused", {"detail": _diagnostic_code(moved)}), true)
		_focus_cell(index)
		return
	_push_undo(source)
	source = moved.source
	redo_stack.clear()
	dirty = true
	selected_placement_id = ""
	_read_source()
	_refresh_maps_after_edit(index)
	_set_status(_t("map.status.moved", {"name": moved_name.to_upper()}), false)


func _move_two_player_position(placement_id: String, x: int, y: int) -> Dictionary:
	var encounter := _current_encounter()
	var candidate: Dictionary = project.duplicate(true)
	var changed := false
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		if str(campaign.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		var flow: Dictionary = campaign.get("flow", {})
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("id", "")) != str(encounter.node.get("id", "")):
				continue
			var node: Dictionary = node_value
			for placement_value: Variant in node.get("placements", []):
				if not placement_value is Dictionary or str(placement_value.get("id", "")) != placement_id:
					continue
				placement_value["twoPlayerPosition"] = {"x": x, "y": y}
				changed = true
				break
			break
		break
	if not changed:
		return {"ok": false, "diagnostics": [{"detail": "placement is no longer available"}]}
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		return checked
	return {"ok": true, "source": candidate_source}


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
	_apply_terrain(changed, index, _t("map.edit.filled", {"terrain": _terrain_label(selected_terrain)}))


func _rectangle_at(index: int, width: int, terrain: Array) -> void:
	if rectangle_anchor < 0:
		rectangle_anchor = index
		_set_status(_t("map.rectangle.choose_corner"), false)
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
		_t("map.edit.rectangle", {"terrain": _terrain_label(selected_terrain)})
	)


func _apply_terrain(changed: Array, focus_index: int, message: String) -> void:
	var map := _current_map()
	var current: Array = map.get("terrain", [])
	if changed == current:
		_focus_cell(focus_index)
		return
	var width := int(map.get("width", 0))
	var height := int(map.get("height", 0))
	var landmark_layout := _landmark_layout(width, height, changed)
	if not bool(landmark_layout.ok):
		_set_status(
			_t("map.error.edit_landmark_refused", {"detail": str(landmark_layout.detail)}),
			true
		)
		_focus_cell(focus_index)
		return
	var terrain_ids := PackedStringArray()
	for value: Variant in changed:
		terrain_ids.append(str(value))
	var updated: Dictionary = compiler.update_map_terrain(
		source, str(map.get("id", "")), terrain_ids
	)
	if not updated.ok:
		_set_status(_t("map.error.edit_refused", {"detail": _diagnostic_code(updated)}), true)
		return
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(focus_index)
	_set_status(_t("map.status.unsaved_detail", {"detail": message}), false)


func _refresh_maps_after_edit(focus_index: int) -> void:
	map_index = clampi(map_index, 0, maxi(0, maps.size() - 1))
	_refresh_maps()
	if focus_index >= 0:
		call_deferred("_focus_cell", focus_index)


func _create_map() -> void:
	if compiler == null:
		_set_status(_t("map.error.creation_unavailable"), true)
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
		_set_status(_t("map.error.create_refused", {"detail": _diagnostic_code(added)}), true)
		return
	_push_undo(source)
	source = added.source
	redo_stack.clear()
	dirty = true
	_read_source()
	map_index = maps.size() - 1
	rectangle_anchor = -1
	_refresh_maps_after_edit(0)
	_set_status(_t("map.status.created", {"name": map_name.to_upper()}), false)


func _push_undo(value: String) -> void:
	undo_stack.append(value)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()


func _undo() -> void:
	if undo_stack.is_empty():
		return
	redo_stack.append(source)
	_reset_deployment_edit()
	_reset_patrol_edit()
	source = undo_stack.pop_back()
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status(_t("map.status.undo"), false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	_push_undo(source)
	_reset_deployment_edit()
	_reset_patrol_edit()
	source = redo_stack.pop_back()
	rectangle_anchor = -1
	selected_placement_id = ""
	pending_placement = {}
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status(_t("map.status.redo"), false)


func _open_add_placement() -> void:
	if _current_encounter().is_empty():
		_set_status(_t("map.error.placement_needs_battle"), true)
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
		_t("map.placement.side_ally" if placement_side == "first" else "map.placement.side_enemy")
	)
	placement_side_button.add_theme_color_override(
		"font_color", Color("69d9ff") if placement_side == "first" else Color("ff7a83")
	)
	placement_apply_button.disabled = placement_candidates.is_empty()
	if placement_candidates.is_empty():
		placement_candidate_label.text = (
			_t("map.placement.no_allies")
			if placement_side == "first"
			else _t("map.placement.no_enemies")
		)
		return
	var candidate: Dictionary = placement_candidates[placement_candidate_index]
	placement_candidate_label.text = _t("map.placement.choice", {
		"name": str(candidate.get("name", _t("map.fallback.unit"))),
		"side": _t("map.side.ally" if placement_side == "first" else "map.side.enemy"),
		"current": placement_candidate_index + 1,
		"total": placement_candidates.size(),
	})
	placement_candidate_label.text += "\n%s" % _target_impact_summary(1)


func _begin_add_placement() -> void:
	if placement_candidates.is_empty():
		return
	pending_placement = placement_candidates[placement_candidate_index].duplicate(true)
	active_tool = "add_unit"
	selected_placement_id = ""
	placement_layer.visible = false
	_update_tool_selection()
	_set_status(
		_t("map.placement.choose_destination", {
			"name": str(pending_placement.get("name", _t("map.fallback.unit"))).to_upper(),
		}),
		false
	)
	call_deferred("_focus_cell", 0)


func _open_objectives() -> void:
	if deployment_editing:
		_set_status(_t("map.error.finish_deployment_first"), true)
		return
	if _current_encounter().is_empty():
		_set_status(_t("map.error.objectives_need_battle"), true)
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
				_placement_display_name(placement)
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
			return _t(str(value.label_key))
	return kind.to_upper()


func _objective_summary(objective: Dictionary) -> String:
	var kind := str(objective.get("kind", "defeatAllOpponents"))
	var lines := PackedStringArray([
		str(objective.get("name", objective.get("id", "Objective"))),
		_t("map.objective.summary_kind", {
			"kind": _objective_kind_label(kind),
			"side": _side_label(str(objective.get("side", "first"))),
		}),
	])
	if kind == "defeatTarget" or kind == "protectTarget":
		lines.append(
			_t("map.objective.summary_target", {"target": _objective_target_display_name(
				str(objective.get("targetPlacementId", ""))
			).to_upper()})
		)
	elif kind == "surviveRounds":
		lines.append(_t("map.objective.summary_rounds", {"rounds": int(objective.get("rounds", 1))}))
	return "\n".join(lines)


func _objective_target_display_name(target_id: String) -> String:
	for target: Dictionary in objective_targets:
		if str(target.get("id", "")) == target_id:
			return str(target.get("name", _t("map.fallback.character")))
	return _t("map.fallback.character")


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
		_t("map.objective.summary_kind", {
			"kind": _t(str(OBJECTIVE_KINDS[objective_kind_index].label_key)),
			"side": _side_label(objective_side),
		}),
	])
	if kind == "defeatTarget" or kind == "protectTarget":
		lines.append(
			_t("map.objective.summary_target", {"target": (
				str(objective_targets[objective_target_index].get("name", _t("map.fallback.character"))).to_upper()
				if not objective_targets.is_empty()
				else _t("map.objective.no_placement")
			)})
		)
	elif kind == "surviveRounds":
		lines.append(_t("map.objective.summary_rounds", {"rounds": objective_rounds}))
	return "\n".join(lines)


func _update_objective_view() -> void:
	objective_mode_button.text = _t("map.objective.mode." + objective_mode)
	var browsing := objective_mode != "new"
	objective_previous_button.visible = browsing
	objective_next_button.visible = browsing
	objective_options.visible = not browsing
	if browsing:
		objective_previous_button.disabled = objective_candidates.size() < 2
		objective_next_button.disabled = objective_candidates.size() < 2
		if objective_candidates.is_empty():
			objective_candidate_label.text = (
				_t("map.objective.none_assigned")
				if objective_mode == "assigned"
				else _t("map.objective.none_available")
			)
		else:
			objective_candidate_label.text = _objective_summary(
				objective_candidates[objective_candidate_index]
			)
		objective_primary_button.text = (
			_t("map.objective.remove" if objective_mode == "assigned" else "map.objective.attach")
		)
		objective_primary_button.disabled = objective_candidates.is_empty() or (
			objective_mode == "assigned" and _current_objective_ids().size() <= 1
		)
		return

	var kind := str(OBJECTIVE_KINDS[objective_kind_index].id)
	objective_candidate_label.text = _new_objective_summary()
	objective_kind_button.text = _t("map.objective.kind_label", {
		"kind": _t(str(OBJECTIVE_KINDS[objective_kind_index].label_key)),
	})
	objective_side_button.text = _t("map.objective.side_label", {"side": _side_label(objective_side)})
	var targeted := kind == "defeatTarget" or kind == "protectTarget"
	objective_target_button.visible = targeted
	objective_rounds_row.visible = kind == "surviveRounds"
	if targeted:
		objective_target_button.text = (
			_t("map.objective.target_label", {"target": str(
				objective_targets[objective_target_index].get("name", _t("map.fallback.unit"))
			)})
			if not objective_targets.is_empty()
			else _t("map.objective.target_none")
		)
	objective_rounds_label.text = str(objective_rounds)
	objective_primary_button.text = _t("map.objective.create_attach")
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
		_accept_objective_edit(added, _t("map.objective.created", {
			"name": _new_objective_name().to_upper(),
		}), objective_id)
		return

	var objective: Dictionary = objective_candidates[objective_candidate_index]
	var objective_id := str(objective.get("id", ""))
	var objective_name := str(objective.get("name", _t("map.fallback.objective")))
	var ids := _current_objective_ids()
	var message := ""
	if objective_mode == "assigned":
		ids.erase(objective_id)
		message = _t("map.objective.removed", {"name": objective_name.to_upper()})
	else:
		ids.append(objective_id)
		message = _t("map.objective.attached", {"name": objective_name.to_upper()})
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
		_set_status(_t("map.error.objective_refused", {"detail": _diagnostic_detail(updated)}), true)
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
	_set_status(_t("map.status.unsaved_detail", {"detail": message}), false)
	objective_mode_button.call_deferred("grab_focus")


func _open_forces() -> void:
	var encounter := _current_encounter()
	if encounter.is_empty():
		_set_status(_t("map.error.forces_need_battle"), true)
		return
	_collect_force_candidates()
	_load_force_draft()
	forces_layer.visible = true
	_update_forces_view()
	forces_first_button.call_deferred("grab_focus")


func _close_forces() -> void:
	forces_layer.visible = false
	setup_layer.visible = true
	setup_first_button.call_deferred("grab_focus")


func _collect_force_candidates(preferred_id := "") -> void:
	force_candidates.clear()
	var encounter := _current_encounter()
	if encounter.is_empty():
		force_candidate_index = 0
		return
	for placement_value: Variant in encounter.node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		if str(placement.get("side", "second")) != "second":
			continue
		force_candidates.append(placement)
		if str(placement.get("id", "")) == preferred_id:
			force_candidate_index = force_candidates.size() - 1
	force_candidate_index = clampi(
		force_candidate_index, 0, maxi(0, force_candidates.size() - 1)
	)


func _selected_force() -> Dictionary:
	if force_candidate_index < 0 or force_candidate_index >= force_candidates.size():
		return {}
	return force_candidates[force_candidate_index]


func _cycle_force_candidate(delta: int) -> void:
	if force_candidates.is_empty():
		return
	force_candidate_index = posmod(force_candidate_index + delta, force_candidates.size())
	_load_force_draft()
	_update_forces_view()


func _load_force_draft() -> void:
	var placement := _selected_force()
	if placement.is_empty():
		force_mode = "opening"
		force_round = 2
		force_every = 2
		force_times = 2
		force_behavior = "hold"
		return
	var arrival: Dictionary = (
		placement.get("arrival", {})
		if placement.get("arrival", {}) is Dictionary else {}
	)
	if arrival.is_empty():
		force_mode = "opening"
	else:
		force_mode = "repeating" if arrival.has("every") else "once"
	force_round = clampi(int(arrival.get("round", 2)), 2, 4095)
	force_every = clampi(int(arrival.get("every", 2)), 1, 4095)
	force_times = clampi(int(arrival.get("times", 2)), 2, 64)
	force_behavior = str(placement.get("behavior", "hold"))
	if not FORCE_BEHAVIORS.has(force_behavior):
		force_behavior = "hold"


func _cycle_force_mode() -> void:
	var index := maxi(0, FORCE_MODES.find(force_mode))
	force_mode = FORCE_MODES[(index + 1) % FORCE_MODES.size()]
	_update_forces_view()


func _change_force_round(delta: int) -> void:
	force_round = clampi(force_round + delta, 2, 4095)
	_update_forces_view()


func _change_force_every(delta: int) -> void:
	force_every = clampi(force_every + delta, 1, 4095)
	_update_forces_view()


func _change_force_times(delta: int) -> void:
	force_times = clampi(force_times + delta, 2, 64)
	_update_forces_view()


func _cycle_force_behavior() -> void:
	var index := maxi(0, FORCE_BEHAVIORS.find(force_behavior))
	force_behavior = FORCE_BEHAVIORS[(index + 1) % FORCE_BEHAVIORS.size()]
	_update_forces_view()


func _toggle_force_scaling() -> void:
	var placement := _selected_force()
	if placement.is_empty():
		return
	var encounter := _current_encounter()
	var minimum_players := (
		1 if int(placement.get("minimumPlayers", 1)) >= 2 else 2
	)
	var updated: Dictionary = compiler.set_placement_minimum_players(
		source,
		str(encounter.get("campaign_id", "")),
		str(encounter.node.get("id", "")),
		str(placement.get("id", "")),
		minimum_players
	)
	if not updated.ok:
		_set_status(_t("map.error.player_scaling_refused", {"detail": _diagnostic_detail(updated)}), true)
		return
	var selected_id := str(placement.get("id", ""))
	_push_undo(source)
	source = updated.source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_collect_force_candidates(selected_id)
	_load_force_draft()
	_update_forces_view()
	_set_status(
		_t("map.status.unsaved_detail", {"detail": _t(
			"map.forces.present_both" if minimum_players == 1 else "map.forces.coop_addition"
		)}),
		false
	)


func _update_forces_view() -> void:
	var placement := _selected_force()
	var available := not placement.is_empty()
	if not available:
		forces_candidate_label.text = (
			_t("map.forces.none")
		)
	else:
		var arrival: Dictionary = (
			placement.get("arrival", {})
			if placement.get("arrival", {}) is Dictionary else {}
		)
		var schedule := _t("map.forces.schedule.opening")
		if not arrival.is_empty():
			schedule = _t("map.forces.schedule.arrives", {"round": int(arrival.get("round", 2))})
			if arrival.has("every"):
				schedule += _t("map.forces.schedule.repeat", {
					"every": int(arrival.get("every", 1)),
					"times": int(arrival.get("times", 2)),
				})
		forces_candidate_label.text = _t("map.forces.choice", {
			"name": _placement_display_name(placement).to_upper(),
			"schedule": schedule,
			"x": int(placement.get("x", 0)), "y": int(placement.get("y", 0)),
		})
	forces_mode_button.text = _t("map.forces.mode." + force_mode)
	forces_round_label.text = _t("map.forces.round", {"round": force_round})
	forces_repeat_row.visible = force_mode == "repeating"
	forces_every_label.text = _t("map.forces.every", {"every": force_every})
	forces_times_label.text = _t("map.forces.waves_total", {"times": force_times})
	forces_behavior_button.text = _t("map.forces.behavior." + force_behavior)
	forces_patrol_button.visible = force_behavior == "patrol"
	forces_patrol_button.disabled = not available
	if available:
		var point_count := (_selected_force().get("patrolPoints", []) as Array).size()
		forces_patrol_button.text = (
			_t("map.forces.patrol_count", {"count": point_count})
		)
	forces_scaling_button.text = (
		_t("map.forces.players.coop_only")
		if int(placement.get("minimumPlayers", 1)) >= 2
		else _t("map.forces.players.both")
	) if available else _t("map.forces.players")
	forces_mode_button.disabled = not available
	forces_behavior_button.disabled = not available
	forces_scaling_button.disabled = not available
	forces_apply_button.disabled = (
		not available
		or (force_behavior == "patrol" and (_selected_force().get("patrolPoints", []) as Array).is_empty())
	)
	forces_apply_button.tooltip_text = (
		_t("map.forces.patrol_required")
		if available and force_behavior == "patrol" and forces_apply_button.disabled else ""
	)


func _apply_force_schedule() -> void:
	var selected := _selected_force()
	var encounter := _current_encounter()
	if selected.is_empty() or encounter.is_empty():
		return
	var selected_id := str(selected.get("id", ""))
	var candidate: Dictionary = project.duplicate(true)
	var changed := false
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		if str(campaign.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		var flow: Dictionary = campaign.get("flow", {})
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			if str(node.get("id", "")) != str(encounter.node.get("id", "")):
				continue
			for placement_value: Variant in node.get("placements", []):
				if not placement_value is Dictionary:
					continue
				var placement: Dictionary = placement_value
				if str(placement.get("id", "")) != selected_id:
					continue
				placement.behavior = force_behavior
				if force_behavior != "patrol":
					placement.erase("patrolPoints")
				if force_mode == "opening":
					placement.erase("arrival")
				else:
					placement.arrival = {"round": force_round}
					if force_mode == "repeating":
						placement.arrival.every = force_every
						placement.arrival.times = force_times
				changed = true
				break
			break
		break
	if not changed:
		return
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("map.error.force_schedule_refused", {"detail": _diagnostic_detail(checked)}), true)
		return
	if candidate_source != source:
		_push_undo(source)
		source = candidate_source
		redo_stack.clear()
		dirty = true
		_read_source()
		_refresh_maps_after_edit(0)
	_collect_force_candidates(selected_id)
	_load_force_draft()
	_update_forces_view()
	_set_status(
		_t("map.status.unsaved_detail", {"detail": _t(
			"map.forces.updated_opening" if force_mode == "opening"
			else "map.forces.updated_wave"
		)}),
		false
	)


func _open_moments() -> void:
	if _current_encounter().is_empty():
		_set_status(_t("map.error.moments_need_battle"), true)
		return
	moment_two_player_only = false
	_collect_moment_candidates()
	moments_layer.visible = true
	_update_moments_view()
	moments_first_button.call_deferred("grab_focus")


func _close_moments() -> void:
	moments_layer.visible = false
	setup_layer.visible = true
	setup_first_button.call_deferred("grab_focus")


func _collect_moment_candidates(preferred_id := "") -> void:
	moment_candidates.clear()
	moment_target_candidates.clear()
	var encounter := _current_encounter()
	if encounter.is_empty():
		return
	for value: Variant in encounter.node.get("moments", []):
		if value is Dictionary:
			moment_candidates.append(value)
			if str(value.get("id", "")) == preferred_id:
				moment_candidate_index = moment_candidates.size() - 1
	for value: Variant in encounter.node.get("placements", []):
		if value is Dictionary:
			moment_target_candidates.append(value)
	moment_candidate_index = clampi(moment_candidate_index, 0, maxi(0, moment_candidates.size() - 1))
	moment_target_index = clampi(moment_target_index, 0, maxi(0, moment_target_candidates.size() - 1))


func _cycle_moment(delta: int) -> void:
	if moment_candidates.is_empty():
		return
	moment_candidate_index = posmod(moment_candidate_index + delta, moment_candidates.size())
	_update_moments_view()


func _cycle_moment_target() -> void:
	if moment_target_candidates.is_empty():
		return
	moment_target_index = (moment_target_index + 1) % moment_target_candidates.size()
	_update_moments_view()


func _toggle_moment_players() -> void:
	moment_two_player_only = not moment_two_player_only
	_update_moments_view()


func _moment_dialogue_name(dialogue_id: String) -> String:
	for value: Variant in project.get("dialogues", []):
		if value is Dictionary and str(value.get("id", "")) == dialogue_id:
			return str(value.get("name", dialogue_id))
	return dialogue_id


func _moment_placement_name(placement_id: String) -> String:
	var encounter := _current_encounter()
	if not encounter.is_empty():
		for value: Variant in encounter.node.get("placements", []):
			if value is Dictionary and str(value.get("id", "")) == placement_id:
				return _placement_display_name(value)
	return placement_id


func _update_moments_view() -> void:
	var available := not moment_candidates.is_empty()
	if not available:
		moments_summary_label.text = _t("map.moments.none")
	else:
		var moment := moment_candidates[moment_candidate_index]
		var when: Dictionary = moment.get("when", {})
		var kind := str(when.get("kind", "stageOpens"))
		var trigger := _t("map.moments.trigger.opening")
		if kind == "characterTalked":
			trigger = _t("map.moments.trigger.talk", {"name": _moment_placement_name(
				str(when.get("placementId", ""))
			).to_upper()})
		elif kind == "characterFalls":
			trigger = _t("map.moments.trigger.fall", {"name": _moment_placement_name(
				str(when.get("placementId", ""))
			).to_upper()})
		moments_summary_label.text = _t("map.moments.summary", {
			"current": moment_candidate_index + 1,
			"total": moment_candidates.size(),
			"scene": _moment_dialogue_name(str(moment.get("dialogueId", ""))).to_upper(),
			"trigger": trigger,
			"players": _t(
				"map.players.coop_only" if int(moment.get("minimumPlayers", 1)) >= 2
				else "map.players.both"
			),
		})
	var target_name := _t("map.moments.add_character_first")
	if not moment_target_candidates.is_empty():
		target_name = _placement_display_name(moment_target_candidates[moment_target_index]).to_upper()
	moments_target_button.text = _t("map.moments.fall_target", {"target": target_name})
	moments_target_button.disabled = moment_target_candidates.is_empty()
	moments_players_button.text = _t("map.moments.new_scenes", {"players": _t(
		"map.players.coop_only" if moment_two_player_only else "map.players.both"
	)})
	moments_edit_button.disabled = not available
	moments_earlier_button.disabled = not available or moment_candidate_index == 0
	moments_later_button.disabled = not available or moment_candidate_index + 1 >= moment_candidates.size()
	var is_talk := false
	if available:
		var when: Variant = moment_candidates[moment_candidate_index].get("when", {})
		is_talk = when is Dictionary and str(when.get("kind", "")) == "characterTalked"
	moments_remove_button.disabled = not available or is_talk
	moments_remove_button.tooltip_text = (
		_t("map.moments.remove_talk_tooltip")
		if is_talk else _t("map.moments.remove_tooltip")
	)


func _unique_battle_moment_id(base: String, values: Array) -> String:
	var ids: Array[String] = []
	for value: Variant in values:
		if value is Dictionary:
			ids.append(str(value.get("id", "")))
	return _unique_project_id(base, ids)


func _add_battle_moment(kind: String) -> void:
	var encounter := _current_encounter()
	if encounter.is_empty() or (kind == "characterFalls" and moment_target_candidates.is_empty()):
		return
	var candidate: Dictionary = project.duplicate(true)
	var dialogues: Array = candidate.get("dialogues", [])
	var node: Dictionary = {}
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		var flow: Dictionary = campaign_value.get("flow", {})
		for node_value: Variant in flow.get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == str(encounter.node.get("id", "")):
				node = node_value
				break
		break
	if node.is_empty():
		return
	var target: Dictionary = {}
	if kind == "characterFalls":
		target = moment_target_candidates[moment_target_index]
	var suffix := "opening" if kind == "stageOpens" else "%s_falls" % str(target.get("id", "character"))
	var dialogue_id := _unique_battle_moment_id("%s_%s_scene" % [str(node.get("id", "battle")), suffix], dialogues)
	var moments: Array = node.get("moments", [])
	var moment_id := _unique_battle_moment_id("%s_moment" % dialogue_id, moments)
	var speaker := "Narrator" if target.is_empty() else _placement_display_name(target)
	var dialogue: Dictionary = {
		"id": dialogue_id,
		"name": "%s / %s" % [str(node.get("name", "Battle")), "Opening" if target.is_empty() else "%s falls" % speaker],
		"lines": [{"speaker": speaker.left(160), "text": "..."}],
	}
	if not target.is_empty():
		dialogue["cast"] = [{"speaker": speaker.left(160), "unitTypeId": str(target.get("unitTypeId", ""))}]
	dialogues.append(dialogue)
	candidate["dialogues"] = dialogues
	var when: Dictionary = {"kind": kind}
	if not target.is_empty():
		when["placementId"] = str(target.get("id", ""))
	var moment: Dictionary = {"id": moment_id, "when": when, "dialogueId": dialogue_id}
	if moment_two_player_only or int(target.get("minimumPlayers", 1)) >= 2:
		moment["minimumPlayers"] = 2
	moments.append(moment)
	node["moments"] = moments
	if _accept_moment_candidate(candidate, _t("map.moments.created")):
		_collect_moment_candidates(moment_id)
		_update_moments_view()


func _accept_moment_candidate(candidate: Dictionary, message: String) -> bool:
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("map.error.moment_refused", {"detail": _diagnostic_detail(checked)}), true)
		return false
	_push_undo(source)
	source = candidate_source
	redo_stack.clear()
	dirty = true
	_read_source()
	_refresh_maps_after_edit(0)
	_set_status(_t("map.status.unsaved_detail", {"detail": message}), false)
	return true


func _move_battle_moment(delta: int) -> void:
	if moment_candidates.is_empty():
		return
	var destination := moment_candidate_index + delta
	if destination < 0 or destination >= moment_candidates.size():
		return
	var selected_id := str(moment_candidates[moment_candidate_index].get("id", ""))
	var candidate: Dictionary = project.duplicate(true)
	var encounter := _current_encounter()
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		for node_value: Variant in (campaign_value.get("flow", {}) as Dictionary).get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == str(encounter.node.get("id", "")):
				var moments: Array = node_value.get("moments", [])
				var moved: Variant = moments.pop_at(moment_candidate_index)
				moments.insert(destination, moved)
				node_value["moments"] = moments
				break
		break
	if _accept_moment_candidate(candidate, _t("map.moments.moved")):
		_collect_moment_candidates(selected_id)
		_update_moments_view()


func _remove_battle_moment() -> void:
	if moment_candidates.is_empty() or moments_remove_button.disabled:
		return
	var selected_id := str(moment_candidates[moment_candidate_index].get("id", ""))
	var candidate: Dictionary = project.duplicate(true)
	var encounter := _current_encounter()
	for campaign_value: Variant in candidate.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != str(encounter.get("campaign_id", "")):
			continue
		for node_value: Variant in (campaign_value.get("flow", {}) as Dictionary).get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("id", "")) != str(encounter.node.get("id", "")):
				continue
			var moments: Array = node_value.get("moments", [])
			for at in range(moments.size() - 1, -1, -1):
				if moments[at] is Dictionary and str(moments[at].get("id", "")) == selected_id:
					moments.remove_at(at)
			if moments.is_empty():
				node_value.erase("moments")
			else:
				node_value["moments"] = moments
			break
		break
	if _accept_moment_candidate(candidate, _t("map.moments.removed")):
		_collect_moment_candidates()
		_update_moments_view()


func _edit_battle_moment() -> void:
	if moment_candidates.is_empty():
		return
	var dialogue_id := str(moment_candidates[moment_candidate_index].get("dialogueId", ""))
	if dialogue_id.is_empty() or (dirty and not _save()):
		return
	moments_layer.visible = false
	CreatorContext.choose_dialogue(dialogue_id)
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _open_surroundings() -> void:
	if deployment_editing:
		_set_status(_t("map.error.finish_deployment_first"), true)
		return
	var map := _current_map()
	if map.is_empty():
		_set_status(_t("map.error.surroundings_need_map"), true)
		return
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	surroundings_mode = str(value.get("mode", "continue"))
	if surroundings_mode == "automatic":
		surroundings_mode = "continue"
	if surroundings_mode not in ["continue", "fill", "blend", "solid", "manual"]:
		surroundings_mode = "continue"
	surroundings_fill_terrain = str(value.get("terrainId", selected_terrain))
	if surroundings_fill_terrain not in TERRAIN_PALETTE:
		surroundings_fill_terrain = selected_terrain
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
	_refresh_grid()
	setup_button.call_deferred("grab_focus")


func _cycle_surroundings_mode() -> void:
	var modes := ["continue", "fill", "blend"]
	var current := modes.find(surroundings_mode)
	surroundings_mode = modes[(current + 1) % modes.size()] if current >= 0 else modes[0]
	_update_surroundings_view()


func _cycle_surroundings_terrain() -> void:
	var current := TERRAIN_PALETTE.find(surroundings_fill_terrain)
	surroundings_fill_terrain = TERRAIN_PALETTE[(current + 1) % TERRAIN_PALETTE.size()]
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
	surroundings_mode_button.text = _t("map.surroundings.mode." + surroundings_mode)
	surroundings_colour_controls.visible = surroundings_mode == "solid"
	surroundings_depth_row.visible = surroundings_mode != "solid"
	surroundings_terrain_row.visible = surroundings_mode in ["fill", "blend"]
	surroundings_terrain_button.text = _t("map.surroundings.terrain_value", {
		"terrain": _terrain_label(surroundings_fill_terrain),
	})
	surroundings_depth_label.text = _t("map.surroundings.depth", {"depth": surroundings_depth})
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
		surroundings_guidance_label.text = _t("map.surroundings.solid_help")
		surroundings_guidance_label.add_theme_color_override("font_color", Color("72e08f"))
		if surroundings_layer.visible:
			_refresh_grid()
		return
	var required := _surrounding_perimeter_count(width, height, surroundings_depth)
	var recommended := _surrounding_perimeter_count(
		width, height, SURROUNDINGS_RECOMMENDED_DEPTH
	)
	var coverage := (
		_t("map.surroundings.coverage_ok", {"depth": SURROUNDINGS_RECOMMENDED_DEPTH})
		if surroundings_depth >= SURROUNDINGS_RECOMMENDED_DEPTH
		else (
			_t("map.surroundings.coverage_warning", {
				"cells": recommended - required,
				"depth": SURROUNDINGS_RECOMMENDED_DEPTH,
			})
		)
	)
	if surroundings_mode in ["continue", "automatic"]:
		surroundings_guidance_label.text = _t("map.surroundings.automatic_help", {
			"cells": required, "coverage": coverage,
		})
	elif surroundings_mode in ["fill", "blend"]:
		surroundings_guidance_label.text = _t(
			"map.surroundings.%s_help" % surroundings_mode,
			{
				"cells": required,
				"terrain": _terrain_label(surroundings_fill_terrain),
				"coverage": coverage,
			}
		)
	else:
		var value: Dictionary = (
			map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
		)
		var authored := 0
		if str(value.get("mode", "")) == "manual" and int(value.get("depth", 0)) == surroundings_depth:
			authored = value.get("terrain", []).size()
		surroundings_guidance_label.text = _t("map.surroundings.manual_help", {
			"required": required, "authored": authored, "coverage": coverage,
		})
	surroundings_guidance_label.add_theme_color_override(
		"font_color",
		Color("ffca63") if surroundings_depth < SURROUNDINGS_RECOMMENDED_DEPTH else Color("72e08f")
	)
	if surroundings_layer.visible:
		_refresh_grid()


func _surroundings_preview_value(map: Dictionary) -> Dictionary:
	if surroundings_layer == null or not surroundings_layer.visible:
		return {}
	var preview := {
		"mode": surroundings_mode,
		"depth": surroundings_depth,
		"terrainId": surroundings_fill_terrain,
	}
	if surroundings_mode == "solid":
		preview["color"] = "#" + surroundings_colour.to_html(false)
	if surroundings_mode == "manual":
		var current: Variant = map.get("surroundings", {})
		if current is Dictionary:
			preview["terrain"] = current.get("terrain", [])
	return preview


func _natural_blend_uses_fill(
	x: int, y: int, width: int, height: int, depth: int
) -> bool:
	var outside_x := -x if x < 0 else (x - width + 1 if x >= width else 0)
	var outside_y := -y if y < 0 else (y - height + 1 if y >= height else 0)
	var distance := maxi(outside_x, outside_y)
	if distance >= depth:
		return true
	var sample := (((x + depth) * 1103515245) ^ ((y + depth) * 12345)) & 0xffffffff
	return depth > 0 and sample % depth < distance


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
		terrain_ids,
		surroundings_fill_terrain if surroundings_mode in ["fill", "blend"] else ""
	)
	if not updated.ok:
		_set_status(_t("map.error.surroundings_refused", {"detail": _diagnostic_detail(updated)}), true)
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
		_t("map.surroundings.manual_ready")
		if surroundings_mode == "manual"
		else _t("map.surroundings.applied", {"mode": _t("map.surroundings.name." + surroundings_mode)})
	)
	_set_status(_t("map.status.unsaved_detail", {"detail": message}), false)


func _paint_surrounding_cell(ring_index: int) -> void:
	var map := _current_map()
	var value: Dictionary = (
		map.get("surroundings", {}) if map.get("surroundings", {}) is Dictionary else {}
	)
	if str(value.get("mode", "")) != "manual":
		_set_status(_t("map.error.choose_manual_surroundings"), true)
		return
	var changed: Array = value.get("terrain", []).duplicate()
	if ring_index < 0 or ring_index >= changed.size():
		_set_status(_t("map.error.outside_cell_not_editable"), true)
		return
	if str(changed[ring_index]) == selected_terrain:
		return
	changed[ring_index] = selected_terrain
	var terrain_ids := PackedStringArray()
	for terrain_id: Variant in changed:
		terrain_ids.append(str(terrain_id))
	var updated: Dictionary = compiler.set_map_surroundings(
		source, str(map.get("id", "")), "manual", "",
		int(value.get("depth", SURROUNDINGS_RECOMMENDED_DEPTH)), terrain_ids, ""
	)
	if not updated.ok:
		_set_status(_t("map.error.outside_edit_refused", {"detail": _diagnostic_detail(updated)}), true)
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
		_t("map.status.outside_cell", {
			"current": ring_index + 1, "total": changed.size(),
			"terrain": _terrain_label(selected_terrain),
		}), false
	)


func _open_visibility() -> void:
	var map := _current_map()
	if map.is_empty():
		_set_status(_t("map.error.visibility_need_map"), true)
		return
	var value: Dictionary = (
		map.get("visualVisibility", {})
		if map.get("visualVisibility", {}) is Dictionary else {}
	)
	visibility_radius = clampi(int(value.get("revealRadius", 2)), 0, 8)
	_update_visibility_summary()
	interior_visibility_layer.visible = true
	visibility_first_button.call_deferred("grab_focus")


func _close_visibility() -> void:
	interior_visibility_layer.visible = false
	setup_layer.visible = true
	tool_buttons["visibility"].call_deferred("grab_focus")


func _begin_visibility_paint() -> void:
	interior_visibility_layer.visible = false
	setup_layer.visible = false
	_select_tool("visibility")


func _change_visibility_radius(delta: int) -> void:
	visibility_radius = clampi(visibility_radius + delta, 0, 8)
	var cells := _visibility_cells(_current_map())
	if not cells.is_empty():
		_apply_visual_visibility(
			cells, -1,
			_t("map.visibility.radius_changed", {"radius": visibility_radius})
		)
	_update_visibility_summary()


func _clear_visibility() -> void:
	if _visibility_cells(_current_map()).is_empty():
		return
	_apply_visual_visibility([], -1, _t("map.visibility.cleared"))
	_update_visibility_summary()


func _toggle_visibility_cell(index: int) -> void:
	var cells := _visibility_cells(_current_map())
	var removed := cells.has(index)
	if removed:
		cells.erase(index)
	else:
		cells.append(index)
		cells.sort()
	_apply_visual_visibility(
		cells, index,
		_t("map.visibility.cell_removed" if removed else "map.visibility.cell_added")
	)


func _apply_visual_visibility(cells: Array[int], focus_index: int, message: String) -> void:
	var map := _current_map()
	var packed := PackedInt32Array(cells)
	var updated: Dictionary = compiler.set_map_visual_visibility(
		source, str(map.get("id", "")), visibility_radius, packed
	)
	if not bool(updated.get("ok", false)):
		_set_status(
			_t("map.error.visibility_refused", {"detail": _diagnostic_detail(updated)}), true
		)
		return
	if str(updated.source) == source:
		return
	_push_undo(source)
	redo_stack.clear()
	source = str(updated.source)
	dirty = true
	_read_source()
	_refresh_maps_after_edit(focus_index)
	_set_status(_t("map.status.unsaved_detail", {"detail": message}), false)


func _update_visibility_summary() -> void:
	if visibility_radius_label == null:
		return
	var count := _visibility_cells(_current_map()).size()
	visibility_radius_label.text = _t(
		"map.visibility.radius", {"radius": visibility_radius}
	)
	visibility_summary_label.text = _t(
		"map.visibility.summary", {"count": count}
	)


func _open_resize() -> void:
	var map := _current_map()
	if map.is_empty():
		_set_status(_t("map.error.resize_need_map"), true)
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
	resize_fill_label.text = _t("map.resize.new_cells", {"terrain": _terrain_label(selected_terrain)})
	var lines := PackedStringArray([
		_t("map.resize.dimensions", {
			"old_width": old_width, "old_height": old_height,
			"width": resize_width, "height": resize_height,
		}),
		_t("map.resize.terrain_summary", {
			"retained": retained, "clipped": terrain_clipped, "added": terrain_added,
		}),
		_t("map.resize.battles.one" if encounters == 1 else "map.resize.battles.many", {
			"count": encounters,
		}),
	])
	if manual_outside:
		lines.append(
			_t("map.resize.manual_ring", {
				"before": outside_before, "after": outside_after,
				"terrain": _terrain_label(selected_terrain),
			})
		)
	if blocked:
		lines.append(
			_t("map.resize.blocked", {
				"placements": placement_clipped, "tiles": deployment_clipped,
			})
		)
	else:
		lines.append(_t("map.resize.safe"))
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
		_set_status(_t("map.error.resize_refused", {"detail": _diagnostic_code(resized)}), true)
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
		_t("map.status.resized", {"width": resize_width, "height": resize_height}),
		false
	)


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status(_t("map.error.save", {"message": saved.message}), true)
		return false
	dirty = false
	_set_status(_t("map.status.saved"), false)
	_update_actions()
	return true


func _play_draft() -> void:
	if compiler == null:
		_set_status(_t("map.error.compiler_unavailable"), true)
		return
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not compiled.ok:
		_set_status(_t("map.error.not_playable", {"detail": _diagnostic_code(compiled)}), true)
		return
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty():
		_set_status(_t("map.error.play_needs_campaign"), true)
		return
	var campaign := str(_current_encounter().get("campaign_id", CreatorContext.campaign_id))
	if campaign.is_empty() and campaigns.size() == 1 and campaigns[0] is Dictionary:
		campaign = str(campaigns[0].get("id", ""))
	if campaign.is_empty():
		_set_status(_t("map.error.play_choose_battle"), true)
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
		_set_status(_t("map.error.unsaved_changes"), true)
		return
	get_tree().change_scene_to_file("res://campaign_editor.tscn")


func _diagnostic_code(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return _t("map.error.project_incomplete")
	return str(diagnostics[0].get("code", _t("map.error.project_incomplete"))).replace("_", " ").to_upper()


func _diagnostic_token(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return "PROJECT_INCOMPLETE"
	return str(diagnostics[0].get("code", "PROJECT_INCOMPLETE")).to_upper()


func _diagnostic_detail(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return _t("map.error.project_incomplete")
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
	var depth := clampi(int(value.get("depth", 0)), 0, 8) if mode in ["continue", "automatic", "fill", "blend", "manual"] else 0
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
	delete_map_button.disabled = maps.is_empty()


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label(_t("map.heading"), 24, Color("69d9ff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(_t("map.subtitle"), 30, Color("f2f7f9"))
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button(_t("map.save"), 150, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button(_t("map.undo"), 130, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	play_button = _button(_t("map.test"), 240, 58)
	play_button.tooltip_text = _t("map.test_tooltip")
	play_button.pressed.connect(_play_draft)
	toolbar.add_child(play_button)
	var story := _button(_t("map.next_story"), 220, 58)
	story.tooltip_text = _t("map.next_story_tooltip")
	story.pressed.connect(_open_story)
	toolbar.add_child(story)
	more_button = _button(_t("map.more"), 160, 58)
	more_button.pressed.connect(_open_more)
	toolbar.add_child(more_button)
	var toolbar_hint := _label(_t("map.controls_hint"), 17, Color("718896"))
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
	tools_column.add_child(_label(_t("map.map_label"), 17, Color("69d9ff")))
	map_select = OptionButton.new()
	map_select.custom_minimum_size = Vector2(330, 58)
	map_select.add_theme_font_size_override("font_size", 19)
	map_select.item_selected.connect(_select_map)
	tools_column.add_child(map_select)
	tools_column.add_child(_label(_t("map.battle_preview"), 17, Color("69d9ff")))
	encounter_select = OptionButton.new()
	encounter_select.custom_minimum_size = Vector2(330, 52)
	encounter_select.add_theme_font_size_override("font_size", 15)
	encounter_select.item_selected.connect(_select_encounter)
	tools_column.add_child(encounter_select)
	tools_column.add_child(_label(_t("map.experience_preview"), 17, Color("69d9ff")))
	experience_select = OptionButton.new()
	experience_select.custom_minimum_size = Vector2(330, 52)
	experience_select.add_theme_font_size_override("font_size", 15)
	experience_select.add_item(_t("map.preview.option.solo"))
	experience_select.set_item_metadata(0, 1)
	experience_select.add_item(_t("map.preview.option.coop"))
	experience_select.set_item_metadata(1, 2)
	experience_select.select(0)
	experience_select.tooltip_text = _t("map.preview.tooltip")
	experience_select.item_selected.connect(_select_experience_preview)
	tools_column.add_child(experience_select)
	tools_column.add_child(_label(_t("map.tool_label"), 17, Color("69d9ff")))
	var tool_row := HBoxContainer.new()
	tool_row.add_theme_constant_override("separation", 6)
	tools_column.add_child(tool_row)
	for tool_name: String in ["brush", "fill", "rectangle"]:
		var tool_button := _button(_tool_label(tool_name), 102, 48)
		tool_button.add_theme_font_size_override("font_size", 14)
		tool_button.pressed.connect(_select_tool.bind(tool_name))
		tool_row.add_child(tool_button)
		tool_buttons[tool_name] = tool_button
	setup_button = _button(_t("map.setup.open"), 330, 52)
	setup_button.add_theme_font_size_override("font_size", 15)
	setup_button.tooltip_text = _t("map.setup.open_tooltip")
	setup_button.pressed.connect(_open_setup)
	tools_column.add_child(setup_button)
	deployment_actions = HBoxContainer.new()
	deployment_actions.add_theme_constant_override("separation", 6)
	deployment_actions.visible = false
	tools_column.add_child(deployment_actions)
	deployment_apply_button = _button(_t("map.deployment.apply"), 162, 46)
	deployment_apply_button.add_theme_font_size_override("font_size", 13)
	deployment_apply_button.pressed.connect(_apply_deployment_edit)
	deployment_actions.add_child(deployment_apply_button)
	var deployment_cancel := _button(_t("map.cancel"), 162, 46)
	deployment_cancel.add_theme_font_size_override("font_size", 13)
	deployment_cancel.pressed.connect(_cancel_deployment_edit)
	deployment_actions.add_child(deployment_cancel)
	deployment_capacity_actions = HBoxContainer.new()
	deployment_capacity_actions.add_theme_constant_override("separation", 6)
	deployment_capacity_actions.visible = false
	tools_column.add_child(deployment_capacity_actions)
	deployment_capacity_minus = _button("−", 46, 42)
	deployment_capacity_minus.tooltip_text = _t("map.deployment.capacity_tooltip")
	deployment_capacity_minus.pressed.connect(_change_deployment_capacity.bind(-1))
	deployment_capacity_actions.add_child(deployment_capacity_minus)
	deployment_capacity_label = _label(_t("map.deployment.no_limit"), 12, Color("ffca63"))
	deployment_capacity_label.custom_minimum_size = Vector2(226, 42)
	deployment_capacity_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	deployment_capacity_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	deployment_capacity_actions.add_child(deployment_capacity_label)
	deployment_capacity_plus = _button("+", 46, 42)
	deployment_capacity_plus.pressed.connect(_change_deployment_capacity.bind(1))
	deployment_capacity_actions.add_child(deployment_capacity_plus)
	patrol_actions = HBoxContainer.new()
	patrol_actions.add_theme_constant_override("separation", 6)
	patrol_actions.visible = false
	tools_column.add_child(patrol_actions)
	patrol_apply_button = _button(_t("map.patrol.apply"), 98, 44)
	patrol_apply_button.pressed.connect(_apply_patrol_edit)
	patrol_actions.add_child(patrol_apply_button)
	var patrol_undo := _button(_t("map.patrol.undo_point"), 94, 44)
	patrol_undo.pressed.connect(_remove_last_patrol_point)
	patrol_actions.add_child(patrol_undo)
	var patrol_clear := _button(_t("map.clear"), 55, 44)
	patrol_clear.pressed.connect(_clear_patrol_points)
	patrol_actions.add_child(patrol_clear)
	var patrol_cancel := _button(_t("map.cancel"), 55, 44)
	patrol_cancel.pressed.connect(_cancel_patrol_edit)
	patrol_actions.add_child(patrol_cancel)
	tools_column.add_child(_label(_t("map.landmarks"), 17, Color("69d9ff")))
	var landmark_row := HBoxContainer.new()
	landmark_row.add_theme_constant_override("separation", 6)
	tools_column.add_child(landmark_row)
	for landmark_tool: String in ["landmark_house", "landmark_grove", "landmark_remove"]:
		var landmark_button := _button(_t("map.tool." + landmark_tool), 102, 46)
		landmark_button.add_theme_font_size_override("font_size", 12)
		landmark_button.tooltip_text = _t("map.tool." + landmark_tool + "_tooltip")
		landmark_button.pressed.connect(_select_tool.bind(landmark_tool))
		landmark_row.add_child(landmark_button)
		tool_buttons[landmark_tool] = landmark_button
	_update_tool_selection()
	tools_column.add_child(_label(_t("map.terrain_label"), 17, Color("69d9ff")))
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
	var board_title := _label(_t("map.board"), 17, Color("69d9ff"))
	board_column.add_child(board_title)
	target_budget_label = _label(_t("map.targets.checking"), 15, Color("a9bdc9"))
	target_budget_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	target_budget_label.custom_minimum_size.y = 24
	board_column.add_child(target_budget_label)
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

	status_label = _label(_t("map.opening"), 17, Color("a9bdc9"))
	status_label.position = Vector2(82, 1010)
	status_label.size = Vector2(1750, 44)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)
	_build_placement_overlay()
	_build_objective_overlay()
	_build_forces_overlay()
	_build_moments_overlay()
	_build_resize_overlay()
	_build_surroundings_overlay()
	_build_visibility_overlay()
	_build_setup_overlay()
	_build_talk_overlay()
	_build_more_overlay()


func _open_setup() -> void:
	if deployment_editing:
		_set_status(_t("map.error.finish_deployment_first"), true)
		return
	if patrol_editing:
		_set_status(_t("map.error.finish_patrol_first"), true)
		return
	_collect_copy_setup_candidates()
	copy_setup_button.disabled = copy_setup_candidates.is_empty()
	copy_setup_button.text = (
		_t("map.copy_setup.need_another")
		if copy_setup_candidates.is_empty()
		else _t("map.copy_setup.open")
	)
	copy_setup_button.tooltip_text = (
		_t("map.copy_setup.none_tooltip")
		if copy_setup_candidates.is_empty()
		else _t("map.copy_setup.open_tooltip")
	)
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


func _setup_forces() -> void:
	setup_layer.visible = false
	_open_forces()


func _setup_moments() -> void:
	setup_layer.visible = false
	_open_moments()


func _setup_surroundings() -> void:
	setup_layer.visible = false
	_open_surroundings()


func _setup_visibility() -> void:
	setup_layer.visible = false
	_open_visibility()


func _collect_copy_setup_candidates() -> void:
	copy_setup_candidates.clear()
	var current := _current_encounter()
	if current.is_empty():
		return
	var campaign_id := str(current.get("campaign_id", ""))
	var current_node_id := str(current.node.get("id", ""))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary or str(campaign_value.get("id", "")) != campaign_id:
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			return
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			if str(node_value.get("kind", "")) != "encounter" or str(node_value.get("id", "")) == current_node_id:
				continue
			copy_setup_candidates.append({
				"node": node_value,
				"node_id": str(node_value.get("id", "")),
				"name": str(node_value.get("name", node_value.get("id", _t("map.fallback.battle")))),
				"map_name": _map_name_for_id(str(node_value.get("mapId", ""))),
			})
		return


func _map_name_for_id(map_id: String) -> String:
	for value: Variant in project.get("maps", []):
		if value is Dictionary and str(value.get("id", "")) == map_id:
			return str(value.get("name", value.get("id", _t("map.fallback.map"))))
	return _t("map.fallback.unknown_map")


func _setup_copy() -> void:
	_collect_copy_setup_candidates()
	if copy_setup_candidates.is_empty():
		_set_status(_t("map.error.copy_needs_battle"), true)
		copy_setup_button.disabled = true
		return
	setup_layer.visible = false
	copy_setup_select.clear()
	for candidate: Dictionary in copy_setup_candidates:
		copy_setup_select.add_item(_t("map.copy_setup.choice", {
			"battle": candidate.name, "map": candidate.map_name,
		}))
		copy_setup_select.set_item_metadata(
			copy_setup_select.item_count - 1, candidate.node_id
		)
	copy_setup_select.select(0)
	_refresh_copy_setup_summary()
	copy_setup_layer.visible = true
	copy_setup_select.call_deferred("grab_focus")


func _close_copy_setup() -> void:
	copy_setup_layer.visible = false
	setup_layer.visible = true
	copy_setup_button.call_deferred("grab_focus")


func _refresh_copy_setup_summary(_index: int = 0) -> void:
	if copy_setup_select.selected < 0 or copy_setup_select.selected >= copy_setup_candidates.size():
		copy_setup_summary.text = _t("map.copy_setup.no_source")
		return
	var node: Dictionary = copy_setup_candidates[copy_setup_select.selected].node
	var goal_count: int = node.get("objectiveIds", []).size()
	copy_setup_summary.text = _t("map.copy_setup.summary", {
		"characters": node.get("placements", []).size(),
		"goals": goal_count,
		"starts": _t("map.copy_setup.deployment" if node.has("deployment") else "map.copy_setup.fixed"),
	})


func _copied_setup_placements(source_node: Dictionary) -> Array:
	var placements: Array = source_node.get("placements", []).duplicate(true)
	for index in placements.size():
		if placements[index] is Dictionary:
			placements[index].erase("talk")
	return placements


func _apply_copy_setup() -> void:
	if copy_setup_select.selected < 0 or copy_setup_select.selected >= copy_setup_candidates.size():
		return
	var current := _current_encounter()
	if current.is_empty():
		_set_status(_t("map.error.copy_target_missing"), true)
		return
	var source_node: Dictionary = copy_setup_candidates[copy_setup_select.selected].node
	var source_name := str(copy_setup_candidates[copy_setup_select.selected].name)
	var campaign_id := str(current.get("campaign_id", ""))
	var target_node_id := str(current.node.get("id", ""))
	var candidate: Dictionary = project.duplicate(true)
	var candidate_campaigns: Array = candidate.get("campaigns", [])
	for campaign_index in candidate_campaigns.size():
		if not candidate_campaigns[campaign_index] is Dictionary or str(candidate_campaigns[campaign_index].get("id", "")) != campaign_id:
			continue
		var flow: Dictionary = candidate_campaigns[campaign_index].get("flow", {}).duplicate(true)
		var nodes: Array = flow.get("nodes", []).duplicate(true)
		for node_index in nodes.size():
			if not nodes[node_index] is Dictionary or str(nodes[node_index].get("id", "")) != target_node_id:
				continue
			var target: Dictionary = nodes[node_index].duplicate(true)
			for field: String in ["placements", "objectiveIds", "turnOrder", "deployment"]:
				if source_node.has(field):
					target[field] = (
						_copied_setup_placements(source_node)
						if field == "placements"
						else source_node[field].duplicate(true)
					)
				else:
					target.erase(field)
			nodes[node_index] = target
			break
		flow.nodes = nodes
		candidate_campaigns[campaign_index].flow = flow
		break
	candidate.campaigns = candidate_campaigns
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("map.error.setup_not_copied", {"detail": _diagnostic_detail(checked)}), true)
		return
	if candidate_source != source:
		_push_undo(source)
		redo_stack.clear()
		source = candidate_source
		dirty = true
		_read_source()
	copy_setup_layer.visible = false
	setup_layer.visible = false
	_refresh_maps_after_edit(0)
	_set_status(_t("map.status.setup_copied", {"name": source_name.to_upper()}), false)
	setup_button.call_deferred("grab_focus")


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
	panel.position = Vector2(390, 70)
	panel.size = Vector2(1140, 930)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	setup_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("map.setup.heading"), 24, Color("69d9ff")))
	var explanation := _label(
		_t("map.setup.help"),
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
	add_unit_button = _button(_t("map.tool.add_unit"), 500, 66)
	add_unit_button.pressed.connect(_setup_add_character)
	choices.add_child(add_unit_button)
	setup_first_button = add_unit_button
	tool_buttons["add_unit"] = add_unit_button
	var move_unit := _button(_t("map.tool.unit"), 500, 66)
	move_unit.pressed.connect(_choose_setup_tool.bind("unit"))
	choices.add_child(move_unit)
	tool_buttons["unit"] = move_unit
	var remove_unit := _button(_t("map.tool.remove_unit"), 500, 66)
	remove_unit.pressed.connect(_choose_setup_tool.bind("remove_unit"))
	choices.add_child(remove_unit)
	tool_buttons["remove_unit"] = remove_unit
	deployment_button = _button(_t("map.tool.deployment"), 500, 66)
	deployment_button.pressed.connect(_setup_deployment)
	choices.add_child(deployment_button)
	tool_buttons["deployment"] = deployment_button
	objective_button = _button(_t("map.objective.open"), 500, 66)
	objective_button.pressed.connect(_setup_objectives)
	choices.add_child(objective_button)
	var ownership_button := _button(_t("map.setup.ownership"), 500, 66)
	ownership_button.pressed.connect(_choose_setup_tool.bind("ownership"))
	choices.add_child(ownership_button)
	tool_buttons["ownership"] = ownership_button
	var forces_button := _button(_t("map.forces.open"), 500, 66)
	forces_button.pressed.connect(_setup_forces)
	choices.add_child(forces_button)
	var moments_button := _button(_t("map.moments.open"), 500, 66)
	moments_button.tooltip_text = _t("map.moments.open_tooltip")
	moments_button.pressed.connect(_setup_moments)
	choices.add_child(moments_button)
	surroundings_button = _button(_t("map.tool.surroundings"), 500, 66)
	surroundings_button.pressed.connect(_setup_surroundings)
	choices.add_child(surroundings_button)
	tool_buttons["surroundings"] = surroundings_button
	var visibility_button := _button(_t("map.visibility.open"), 500, 66)
	visibility_button.tooltip_text = _t("map.visibility.open_tooltip")
	visibility_button.pressed.connect(_setup_visibility)
	choices.add_child(visibility_button)
	tool_buttons["visibility"] = visibility_button
	var talk_button := _button(
		_t("map.setup.talk"), 1014, 72
	)
	talk_button.tooltip_text = (
		_t("map.setup.talk_tooltip")
	)
	talk_button.pressed.connect(_choose_setup_tool.bind("talk"))
	column.add_child(talk_button)
	tool_buttons["talk"] = talk_button
	var coop_story_button := _button(
		_t("map.setup.coop_story"), 1014, 66
	)
	coop_story_button.tooltip_text = (
		_t("map.setup.coop_story_tooltip")
	)
	coop_story_button.pressed.connect(_write_coop_opening_story)
	column.add_child(coop_story_button)
	copy_setup_button = _button(_t("map.copy_setup.open"), 1014, 66)
	copy_setup_button.pressed.connect(_setup_copy)
	column.add_child(copy_setup_button)
	var back := _button(_t("map.back_map"), 500, 62)
	back.pressed.connect(_close_setup)
	column.add_child(back)
	_update_tool_selection()
	_build_copy_setup_overlay()


func _build_talk_overlay() -> void:
	talk_layer = Control.new()
	talk_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	talk_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	talk_layer.visible = false
	talk_layer.z_index = 36
	add_child(talk_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.90)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	talk_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(510, 260)
	panel.size = Vector2(900, 550)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	talk_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("map.talk.heading"), 20, Color("69d9ff")))
	talk_title = _label(_t("map.talk.default_title"), 30, Color("f2f7f9"))
	column.add_child(talk_title)
	talk_guidance = _label("", 18, Color("a9bdc9"))
	talk_guidance.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	talk_guidance.custom_minimum_size.y = 86
	column.add_child(talk_guidance)
	talk_write_button = _button(_t("map.talk.write"), 840, 66)
	talk_write_button.pressed.connect(_write_talk_conversation)
	column.add_child(talk_write_button)
	talk_remove_button = _button(_t("map.talk.remove"), 840, 66)
	talk_remove_button.pressed.connect(_remove_talk_choice)
	column.add_child(talk_remove_button)
	var done := _button(_t("map.talk.done"), 840, 66)
	done.pressed.connect(_close_talk_choice)
	column.add_child(done)


func _build_copy_setup_overlay() -> void:
	copy_setup_layer = Control.new()
	copy_setup_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	copy_setup_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	copy_setup_layer.visible = false
	copy_setup_layer.z_index = 40
	add_child(copy_setup_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	copy_setup_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 250)
	panel.size = Vector2(1060, 580)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("ffca63"))
	)
	copy_setup_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 17)
	panel.add_child(column)
	column.add_child(_label(_t("map.copy_setup.heading"), 20, Color("ffca63")))
	column.add_child(_label(_t("map.copy_setup.title"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("map.copy_setup.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 72
	column.add_child(explanation)
	column.add_child(_label(_t("map.copy_setup.from"), 15, Color("ffca63")))
	copy_setup_select = OptionButton.new()
	copy_setup_select.custom_minimum_size = Vector2(960, 64)
	copy_setup_select.add_theme_font_size_override("font_size", 20)
	copy_setup_select.item_selected.connect(_refresh_copy_setup_summary)
	column.add_child(copy_setup_select)
	copy_setup_summary = _label("", 19, Color("f2f7f9"))
	copy_setup_summary.custom_minimum_size.y = 48
	copy_setup_summary.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	column.add_child(copy_setup_summary)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var apply := _button(_t("map.copy_setup.apply"), 340, 68)
	apply.pressed.connect(_apply_copy_setup)
	actions.add_child(apply)
	var cancel := _button(_t("map.cancel"), 260, 68)
	cancel.pressed.connect(_close_copy_setup)
	actions.add_child(cancel)


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


func _more_delete_map() -> void:
	var map := _current_map()
	dependency_category = "map"
	dependency_id = str(map.get("id", ""))
	more_layer.visible = false
	_refresh_dependency_review()
	dependency_layer.visible = true
	if dependency_confirm_button.disabled:
		dependency_close_button.call_deferred("grab_focus")
	else:
		dependency_confirm_button.call_deferred("grab_focus")


func _close_dependency_review() -> void:
	dependency_layer.visible = false
	more_layer.visible = true
	delete_map_button.call_deferred("grab_focus")


func _dependency_decision(category: String, identity: String) -> Dictionary:
	return ProjectContentDependencyIndex.new(project).deletion_decision(
		category, identity
	)


func _refresh_dependency_review() -> Dictionary:
	var decision := _dependency_decision(dependency_category, dependency_id)
	var category_label := _t(
		"map.kind.map" if dependency_category == "map" else "map.kind.terrain_theme"
	)
	dependency_heading.text = _t("map.delete.where_used", {
		"kind": category_label, "name": str(decision.get("name", dependency_id)).to_upper(),
	})
	dependency_summary.text = _dependency_summary(decision)
	var lines: Array[String] = []
	for value: Variant in decision.get("uses", []):
		if value is Dictionary:
			lines.append(_t("map.delete.use", {
				"section": _t("map.delete.section.battles"),
				"owner": _dependency_use_owner(value),
			}))
	dependency_uses.text = (
		"\n\n".join(lines)
		if not lines.is_empty()
		else _t("map.delete.no_uses")
	)
	var allowed := (
		dependency_category == "map"
		and bool(decision.get("allowed", false))
	)
	dependency_confirm_button.disabled = not allowed
	dependency_confirm_button.text = _t("map.delete.confirm" if allowed else "map.delete.cannot")
	return decision


func _confirm_map_delete() -> bool:
	var decision := _refresh_dependency_review()
	if (
		dependency_category != "map"
		or not bool(decision.get("allowed", false))
	):
		_set_status(
			_t("map.error.not_deleted", {"reason": _delete_reason(str(
				decision.get("reason", "unsupported_request")
			))}),
			true
		)
		return false
	var candidate: Dictionary = project.duplicate(true)
	var candidate_maps: Array = candidate.get("maps", []).duplicate(true)
	var remove_index := -1
	for index in candidate_maps.size():
		if (
			candidate_maps[index] is Dictionary
			and str(candidate_maps[index].get("id", "")) == dependency_id
		):
			remove_index = index
			break
	if remove_index < 0:
		_set_status(_t("map.error.delete_missing"), true)
		_refresh_dependency_review()
		return false
	var removed_name := str(candidate_maps[remove_index].get("name", dependency_id))
	candidate_maps.remove_at(remove_index)
	candidate.maps = candidate_maps
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(
			_t("map.error.map_not_deleted", {"detail": _diagnostic_detail(checked)}), true
		)
		return false
	_push_undo(source)
	redo_stack.clear()
	source = candidate_source
	dirty = true
	_read_source()
	map_index = clampi(remove_index, 0, maxi(0, maps.size() - 1))
	dependency_layer.visible = false
	more_layer.visible = false
	_refresh_maps_after_edit(0)
	_set_status(
		_t("map.status.deleted", {"name": removed_name.to_upper()}), false
	)
	map_select.call_deferred("grab_focus")
	return true


func _more_rename() -> void:
	more_layer.visible = false
	rename_map_input.text = _current_map_name()
	var current := _current_encounter()
	var has_battle := not current.is_empty()
	rename_battle_input.editable = has_battle
	rename_battle_input.text = (
		str(current.node.get("name", current.node.get("id", _t("map.fallback.battle"))))
		if has_battle
		else _t("map.rename.no_battle")
	)
	rename_apply_button.text = _t("map.rename.apply_both" if has_battle else "map.rename.apply_map")
	rename_layer.visible = true
	rename_map_input.grab_focus()
	rename_map_input.select_all()


func _close_rename() -> void:
	rename_layer.visible = false
	more_layer.visible = true
	rename_button.call_deferred("grab_focus")


func _apply_rename_from_text(_value: String) -> void:
	_apply_rename()


func _apply_rename() -> void:
	var map_name := rename_map_input.text.strip_edges()
	if map_name.is_empty():
		_set_status(_t("map.error.map_name_empty"), true)
		rename_map_input.grab_focus()
		return
	var current := _current_encounter()
	var battle_name := rename_battle_input.text.strip_edges()
	if not current.is_empty() and battle_name.is_empty():
		_set_status(_t("map.error.battle_name_empty"), true)
		rename_battle_input.grab_focus()
		return
	var candidate: Dictionary = project.duplicate(true)
	var map_id := str(_current_map().get("id", ""))
	var candidate_maps: Array = candidate.get("maps", [])
	for index in candidate_maps.size():
		if candidate_maps[index] is Dictionary and str(candidate_maps[index].get("id", "")) == map_id:
			candidate_maps[index].name = map_name
			break
	candidate.maps = candidate_maps
	if not current.is_empty():
		var campaign_id := str(current.get("campaign_id", ""))
		var node_id := str(current.node.get("id", ""))
		var candidate_campaigns: Array = candidate.get("campaigns", [])
		for campaign_index in candidate_campaigns.size():
			if not candidate_campaigns[campaign_index] is Dictionary or str(candidate_campaigns[campaign_index].get("id", "")) != campaign_id:
				continue
			var flow: Dictionary = candidate_campaigns[campaign_index].get("flow", {}).duplicate(true)
			var nodes: Array = flow.get("nodes", []).duplicate(true)
			for node_index in nodes.size():
				if nodes[node_index] is Dictionary and str(nodes[node_index].get("id", "")) == node_id:
					nodes[node_index].name = battle_name
					break
			flow.nodes = nodes
			candidate_campaigns[campaign_index].flow = flow
			break
		candidate.campaigns = candidate_campaigns
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("map.error.names_not_changed", {"detail": _diagnostic_detail(checked)}), true)
		return
	if candidate_source != source:
		_push_undo(source)
		redo_stack.clear()
		source = candidate_source
		dirty = true
		_read_source()
		rename_layer.visible = false
		more_layer.visible = false
		_refresh_maps_after_edit(0)
		_set_status(
			_t("map.status.renamed_both")
			if not current.is_empty()
			else _t("map.status.renamed_map"),
			false
		)
	else:
		_close_rename()
	map_select.call_deferred("grab_focus")


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if typeof(value) == TYPE_FLOAT and value == floor(value):
		return int(value)
	return value


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
		_set_status(_t("map.error.look_not_changed", {"detail": _diagnostic_detail(updated)}), true)
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
		_t("map.status.look_changed", {"choice": _presentation_label(field, str(options[index]))}),
		false
	)
	_update_actions()


func _refresh_look_preview() -> void:
	var terrain_names := ["grass", "forest", "road", "water"]
	for index in look_terrain_previews.size():
		look_terrain_previews[index].texture = _terrain_icon(terrain_names[index], 0)
	look_character_preview.texture = null
	look_character_label.text = _t("map.look.character_preview")
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
	image.atlas = art.character_frame_texture(frame)
	image.region = Rect2(Vector2.ZERO, Vector2(frame.cell_size))
	look_character_preview.texture = image
	look_character_label.text = str(unit.get("name", _t("map.fallback.character"))).to_upper()


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
		_set_status(_t("map.error.chapter_needs_battle"), true)
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
		_set_status(_t("map.error.chapter_name"), true)
		chapter_title.grab_focus()
		return
	var encounter := _current_encounter()
	if encounter.is_empty():
		_set_status(_t("map.error.preceding_battle_missing"), true)
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
		_set_status(_t("map.error.chapter_not_created", {"detail": _diagnostic_detail(added)}), true)
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
		_t("map.status.chapter_created", {"name": title.to_upper()}),
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
	panel.position = Vector2(560, 55)
	panel.size = Vector2(800, 970)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("69d9ff"))
	)
	more_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 14)
	panel.add_child(column)
	column.add_child(_label(_t("map.more.heading"), 24, Color("69d9ff")))
	var explanation := _label(
		_t("map.more.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 54
	column.add_child(explanation)
	redo_button = _button(_t("map.more.redo"), 680, 70)
	redo_button.pressed.connect(_more_redo)
	column.add_child(redo_button)
	var new_map_button := _button(_t("map.chapter.open"), 680, 70)
	new_map_button.pressed.connect(_more_new_map)
	column.add_child(new_map_button)
	more_first_button = new_map_button
	var army_button := _button(_t("map.more.army"), 680, 70)
	army_button.pressed.connect(_more_army)
	column.add_child(army_button)
	look_button = _button(_t("map.look.open"), 680, 70)
	look_button.pressed.connect(_more_look)
	column.add_child(look_button)
	rename_button = _button(_t("map.rename.open"), 680, 70)
	rename_button.pressed.connect(_more_rename)
	column.add_child(rename_button)
	var resize_button := _button(_t("map.resize.open"), 680, 70)
	resize_button.pressed.connect(_more_resize)
	column.add_child(resize_button)
	delete_map_button = _button(_t("map.delete.open"), 680, 70)
	delete_map_button.tooltip_text = _t("map.delete.open_tooltip")
	delete_map_button.pressed.connect(_more_delete_map)
	column.add_child(delete_map_button)
	var projects := _button(_t("map.more.projects"), 680, 70)
	projects.pressed.connect(_more_projects)
	column.add_child(projects)
	var back := _button(_t("map.back_map"), 680, 70)
	back.pressed.connect(_close_more)
	column.add_child(back)
	_build_chapter_overlay()
	_build_look_overlay()
	_build_rename_overlay()
	_build_dependency_overlay()


func _build_dependency_overlay() -> void:
	dependency_layer = Control.new()
	dependency_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dependency_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.visible = false
	dependency_layer.z_index = 45
	add_child(dependency_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.94)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(330, 115)
	panel.size = Vector2(1260, 850)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color(0.20, 0.62, 0.78, 0.72))
	)
	dependency_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("map.delete.safe"), 18, Color("69d9ff")))
	dependency_heading = _label(_t("map.delete.heading"), 29, Color("f2f7f9"))
	dependency_heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_heading.custom_minimum_size.y = 70
	column.add_child(dependency_heading)
	dependency_summary = _label("", 19, Color("a9bdc9"))
	dependency_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_summary.custom_minimum_size.y = 82
	column.add_child(dependency_summary)
	var uses_panel := PanelContainer.new()
	uses_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	uses_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 14, Color(0.35, 0.45, 0.72, 0.42))
	)
	column.add_child(uses_panel)
	dependency_uses = RichTextLabel.new()
	dependency_uses.custom_minimum_size = Vector2(1120, 410)
	dependency_uses.add_theme_font_size_override("normal_font_size", 18)
	dependency_uses.add_theme_color_override("default_color", Color("dce8ed"))
	uses_panel.add_child(dependency_uses)
	var note := _label(
		_t("map.delete.help"),
		16,
		Color("718896")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 42
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	dependency_confirm_button = _button(_t("map.delete.cannot"), 360, 62)
	dependency_confirm_button.pressed.connect(_confirm_map_delete)
	actions.add_child(dependency_confirm_button)
	dependency_close_button = _button(_t("map.delete.keep"), 260, 62)
	dependency_close_button.pressed.connect(_close_dependency_review)
	actions.add_child(dependency_close_button)


func _build_rename_overlay() -> void:
	rename_layer = Control.new()
	rename_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	rename_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	rename_layer.visible = false
	rename_layer.z_index = 40
	add_child(rename_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.92)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	rename_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 270)
	panel.size = Vector2(1060, 540)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 22, Color("69d9ff"))
	)
	rename_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("map.rename.heading"), 20, Color("69d9ff")))
	column.add_child(_label(_t("map.rename.title"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("map.rename.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 58
	column.add_child(explanation)
	column.add_child(_label(_t("map.rename.map_name"), 15, Color("69d9ff")))
	rename_map_input = LineEdit.new()
	rename_map_input.max_length = 160
	rename_map_input.custom_minimum_size = Vector2(960, 62)
	rename_map_input.add_theme_font_size_override("font_size", 21)
	rename_map_input.text_submitted.connect(_apply_rename_from_text)
	column.add_child(rename_map_input)
	column.add_child(_label(_t("map.rename.battle_name"), 15, Color("69d9ff")))
	rename_battle_input = LineEdit.new()
	rename_battle_input.max_length = 160
	rename_battle_input.custom_minimum_size = Vector2(960, 62)
	rename_battle_input.add_theme_font_size_override("font_size", 21)
	rename_battle_input.text_submitted.connect(_apply_rename_from_text)
	column.add_child(rename_battle_input)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	rename_apply_button = _button(_t("map.rename.apply_both"), 360, 68)
	rename_apply_button.pressed.connect(_apply_rename)
	actions.add_child(rename_apply_button)
	var cancel := _button(_t("map.cancel"), 260, 68)
	cancel.pressed.connect(_close_rename)
	actions.add_child(cancel)


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
	column.add_child(_label(_t("map.chapter.heading"), 20, Color("69d9ff")))
	column.add_child(_label(_t("map.chapter.title"), 34, Color("f2f7f9")))
	var explanation := _label(
		_t("map.chapter.help"),
		19,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 130
	column.add_child(explanation)
	column.add_child(_label(_t("map.chapter.name"), 16, Color("69d9ff")))
	chapter_title = LineEdit.new()
	chapter_title.custom_minimum_size = Vector2(940, 68)
	chapter_title.max_length = 140
	chapter_title.add_theme_font_size_override("font_size", 24)
	chapter_title.text_submitted.connect(_create_chapter_from_text)
	column.add_child(chapter_title)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	chapter_create_button = _button(_t("map.chapter.create"), 420, 72)
	chapter_create_button.pressed.connect(_create_chapter)
	actions.add_child(chapter_create_button)
	var back := _button(_t("map.back"), 260, 72)
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
	column.add_child(_label(_t("map.look.heading"), 20, Color("69d9ff")))
	column.add_child(_label(_t("map.look.title"), 34, Color("f2f7f9")))
	var explanation := _label(
		_t("map.look.help"),
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
	theme_column.add_child(_label(_t("map.look.world_theme"), 15, Color("69d9ff")))
	look_theme = OptionButton.new()
	look_theme.custom_minimum_size = Vector2(540, 60)
	look_theme.add_theme_font_size_override("font_size", 20)
	for name: String in art.THEME_NAMES:
		look_theme.add_item(_t("map.theme." + name))
	look_theme.item_selected.connect(_select_look.bind("themeId"))
	theme_column.add_child(look_theme)
	var style_column := VBoxContainer.new()
	style_column.custom_minimum_size.x = 540
	style_column.add_theme_constant_override("separation", 7)
	choices.add_child(style_column)
	style_column.add_child(_label(_t("map.look.character_style"), 15, Color("69d9ff")))
	look_style = OptionButton.new()
	look_style.custom_minimum_size = Vector2(540, 60)
	look_style.add_theme_font_size_override("font_size", 20)
	for name: String in art.STYLE_NAMES:
		look_style.add_item(_t("map.style." + name))
	look_style.item_selected.connect(_select_look.bind("characterStyleId"))
	style_column.add_child(look_style)
	column.add_child(_label(_t("map.look.actual_assets"), 15, Color("69d9ff")))
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
	look_character_label = _label(_t("map.look.character_preview"), 15, Color("f2f7f9"))
	look_character_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	character_column.add_child(look_character_label)
	var done := _button(_t("map.done"), 320, 64)
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
	column.add_child(_label(_t("map.placement.heading"), 24, Color("69d9ff")))
	var explanation := _label(
		_t("map.placement.help"),
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 72
	column.add_child(explanation)
	placement_first_button = _button(_t("map.placement.toggle_side"), 360, 64)
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
	var previous := _button(_t("map.previous"), 240, 60)
	previous.pressed.connect(_cycle_placement_candidate.bind(-1))
	candidate_row.add_child(previous)
	var next := _button(_t("map.next"), 240, 60)
	next.pressed.connect(_cycle_placement_candidate.bind(1))
	candidate_row.add_child(next)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	placement_apply_button = _button(_t("map.placement.apply"), 300, 64)
	placement_apply_button.pressed.connect(_begin_add_placement)
	actions.add_child(placement_apply_button)
	var cancel := _button(_t("map.cancel"), 220, 64)
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
	column.add_child(_label(_t("map.objective.heading"), 24, Color("ffca63")))
	var explanation := _label(
		_t("map.objective.help"),
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 66
	column.add_child(explanation)
	objective_first_button = _button(_t("map.objective.view"), 420, 60)
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
	objective_previous_button = _button(_t("map.previous"), 240, 56)
	objective_previous_button.pressed.connect(_cycle_objective_candidate.bind(-1))
	navigation.add_child(objective_previous_button)
	objective_next_button = _button(_t("map.next"), 240, 56)
	objective_next_button.pressed.connect(_cycle_objective_candidate.bind(1))
	navigation.add_child(objective_next_button)

	objective_options = VBoxContainer.new()
	objective_options.add_theme_constant_override("separation", 10)
	column.add_child(objective_options)
	var kind_side := HBoxContainer.new()
	kind_side.add_theme_constant_override("separation", 12)
	objective_options.add_child(kind_side)
	objective_kind_button = _button(_t("map.objective.kind"), 610, 56)
	objective_kind_button.add_theme_font_size_override("font_size", 15)
	objective_kind_button.pressed.connect(_cycle_objective_kind.bind(1))
	kind_side.add_child(objective_kind_button)
	objective_side_button = _button(_t("map.objective.side"), 300, 56)
	objective_side_button.add_theme_font_size_override("font_size", 15)
	objective_side_button.pressed.connect(_toggle_objective_side)
	kind_side.add_child(objective_side_button)
	objective_target_button = _button(_t("map.objective.target"), 922, 56)
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
	objective_rounds_row.add_child(_label(_t("map.objective.rounds_to_survive"), 17, Color("718896")))

	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	objective_primary_button = _button(_t("map.apply"), 420, 62)
	objective_primary_button.add_theme_font_size_override("font_size", 15)
	objective_primary_button.pressed.connect(_apply_objective_action)
	actions.add_child(objective_primary_button)
	var close := _button(_t("map.close"), 220, 62)
	close.pressed.connect(_close_objectives)
	actions.add_child(close)


func _build_forces_overlay() -> void:
	forces_layer = Control.new()
	forces_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	forces_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	forces_layer.visible = false
	forces_layer.z_index = 34
	add_child(forces_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.90)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	forces_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(390, 105)
	panel.size = Vector2(1140, 870)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("ffca63"))
	)
	forces_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	column.add_child(_label(_t("map.forces.heading"), 24, Color("ffca63")))
	var explanation := _label(
		_t("map.forces.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 64
	column.add_child(explanation)
	forces_candidate_label = _label("", 22, Color("f2f7f9"))
	forces_candidate_label.custom_minimum_size.y = 80
	forces_candidate_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	column.add_child(forces_candidate_label)
	var navigation := HBoxContainer.new()
	navigation.add_theme_constant_override("separation", 12)
	column.add_child(navigation)
	forces_first_button = _button(_t("map.forces.previous"), 330, 52)
	forces_first_button.pressed.connect(_cycle_force_candidate.bind(-1))
	navigation.add_child(forces_first_button)
	var next := _button(_t("map.forces.next"), 330, 52)
	next.pressed.connect(_cycle_force_candidate.bind(1))
	navigation.add_child(next)
	forces_mode_button = _button(_t("map.forces.mode.opening"), 1014, 56)
	forces_mode_button.pressed.connect(_cycle_force_mode)
	column.add_child(forces_mode_button)
	var round_row := HBoxContainer.new()
	round_row.add_theme_constant_override("separation", 10)
	column.add_child(round_row)
	var round_minus := _button("−", 110, 52)
	round_minus.pressed.connect(_change_force_round.bind(-1))
	round_row.add_child(round_minus)
	forces_round_label = _label(_t("map.forces.round", {"round": 2}), 21, Color("f2f7f9"))
	forces_round_label.custom_minimum_size = Vector2(260, 52)
	forces_round_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	forces_round_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	round_row.add_child(forces_round_label)
	var round_plus := _button("+", 110, 52)
	round_plus.pressed.connect(_change_force_round.bind(1))
	round_row.add_child(round_plus)
	round_row.add_child(_label(_t("map.forces.first_arrival"), 16, Color("718896")))
	forces_repeat_row = HBoxContainer.new()
	forces_repeat_row.add_theme_constant_override("separation", 8)
	column.add_child(forces_repeat_row)
	var every_minus := _button("−", 70, 50)
	every_minus.pressed.connect(_change_force_every.bind(-1))
	forces_repeat_row.add_child(every_minus)
	forces_every_label = _label(_t("map.forces.every", {"every": 2}), 17, Color("f2f7f9"))
	forces_every_label.custom_minimum_size = Vector2(220, 50)
	forces_every_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	forces_every_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	forces_repeat_row.add_child(forces_every_label)
	var every_plus := _button("+", 70, 50)
	every_plus.pressed.connect(_change_force_every.bind(1))
	forces_repeat_row.add_child(every_plus)
	var times_minus := _button("−", 70, 50)
	times_minus.pressed.connect(_change_force_times.bind(-1))
	forces_repeat_row.add_child(times_minus)
	forces_times_label = _label(_t("map.forces.waves_total", {"times": 2}), 17, Color("f2f7f9"))
	forces_times_label.custom_minimum_size = Vector2(210, 50)
	forces_times_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	forces_times_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	forces_repeat_row.add_child(forces_times_label)
	var times_plus := _button("+", 70, 50)
	times_plus.pressed.connect(_change_force_times.bind(1))
	forces_repeat_row.add_child(times_plus)
	forces_behavior_button = _button(_t("map.forces.behavior.hold"), 1014, 54)
	forces_behavior_button.pressed.connect(_cycle_force_behavior)
	column.add_child(forces_behavior_button)
	forces_patrol_button = _button(_t("map.forces.patrol"), 1014, 54)
	forces_patrol_button.tooltip_text = _t("map.forces.patrol_tooltip")
	forces_patrol_button.pressed.connect(_begin_patrol_edit)
	forces_patrol_button.visible = false
	column.add_child(forces_patrol_button)
	forces_scaling_button = _button(_t("map.forces.players"), 1014, 54)
	forces_scaling_button.pressed.connect(_toggle_force_scaling)
	column.add_child(forces_scaling_button)
	var note := _label(
		_t("map.forces.tip"),
		16, Color("72e08f")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 48
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	forces_apply_button = _button(_t("map.forces.apply"), 430, 58)
	forces_apply_button.pressed.connect(_apply_force_schedule)
	actions.add_child(forces_apply_button)
	var close := _button(_t("map.done"), 220, 58)
	close.pressed.connect(_close_forces)
	actions.add_child(close)


func _build_moments_overlay() -> void:
	moments_layer = Control.new()
	moments_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	moments_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	moments_layer.visible = false
	moments_layer.z_index = 35
	add_child(moments_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.90)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	moments_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(390, 105)
	panel.size = Vector2(1140, 870)
	panel.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), 18, Color("b58cff")))
	moments_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	panel.add_child(column)
	column.add_child(_label(_t("map.moments.heading"), 24, Color("b58cff")))
	var explanation := _label(
		_t("map.moments.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 58
	column.add_child(explanation)
	moments_summary_label = _label("", 20, Color("f2f7f9"))
	moments_summary_label.custom_minimum_size.y = 82
	moments_summary_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	column.add_child(moments_summary_label)
	var navigation := HBoxContainer.new()
	navigation.add_theme_constant_override("separation", 10)
	column.add_child(navigation)
	moments_first_button = _button(_t("map.moments.previous"), 250, 50)
	moments_first_button.pressed.connect(_cycle_moment.bind(-1))
	navigation.add_child(moments_first_button)
	var next := _button(_t("map.moments.next"), 250, 50)
	next.pressed.connect(_cycle_moment.bind(1))
	navigation.add_child(next)
	moments_earlier_button = _button(_t("map.moments.earlier"), 225, 50)
	moments_earlier_button.pressed.connect(_move_battle_moment.bind(-1))
	navigation.add_child(moments_earlier_button)
	moments_later_button = _button(_t("map.moments.later"), 225, 50)
	moments_later_button.pressed.connect(_move_battle_moment.bind(1))
	navigation.add_child(moments_later_button)
	moments_players_button = _button(_t("map.moments.new_scenes", {"players": _t("map.players.both")}), 1014, 52)
	moments_players_button.pressed.connect(_toggle_moment_players)
	column.add_child(moments_players_button)
	var add_opening := _button(_t("map.moments.add_opening"), 1014, 56)
	add_opening.pressed.connect(_add_battle_moment.bind("stageOpens"))
	column.add_child(add_opening)
	moments_target_button = _button(_t("map.moments.fall_target_short"), 1014, 52)
	moments_target_button.pressed.connect(_cycle_moment_target)
	column.add_child(moments_target_button)
	var add_fall := _button(_t("map.moments.add_fall"), 1014, 56)
	add_fall.pressed.connect(_add_battle_moment.bind("characterFalls"))
	column.add_child(add_fall)
	var hint := _label(
		_t("map.moments.talk_help"),
		16, Color("72e08f")
	)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.custom_minimum_size.y = 44
	column.add_child(hint)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 10)
	column.add_child(actions)
	moments_edit_button = _button(_t("map.moments.edit"), 360, 58)
	moments_edit_button.pressed.connect(_edit_battle_moment)
	actions.add_child(moments_edit_button)
	moments_remove_button = _button(_t("map.moments.remove"), 250, 58)
	moments_remove_button.pressed.connect(_remove_battle_moment)
	actions.add_child(moments_remove_button)
	var done := _button(_t("map.done"), 180, 58)
	done.pressed.connect(_close_moments)
	actions.add_child(done)


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
	column.add_child(_label(_t("map.resize.heading"), 24, Color("69d9ff")))
	var explanation := _label(
		_t("map.resize.help"),
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
	width_column.add_child(_label(_t("map.resize.width"), 16, Color("718896")))
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
	height_column.add_child(_label(_t("map.resize.height"), 16, Color("718896")))
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

	resize_fill_label = _label(_t("map.resize.new_cells_short"), 17, Color("69d9ff"))
	column.add_child(resize_fill_label)
	resize_preview_label = _label("", 19, Color("72e08f"))
	resize_preview_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	resize_preview_label.custom_minimum_size.y = 180
	column.add_child(resize_preview_label)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	column.add_child(actions)
	resize_apply_button = _button(_t("map.resize.apply"), 300, 64)
	resize_apply_button.pressed.connect(_apply_resize)
	actions.add_child(resize_apply_button)
	var cancel := _button(_t("map.cancel"), 220, 64)
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
	column.add_child(_label(_t("map.surroundings.heading"), 24, Color("72e08f")))
	var explanation := _label(
		_t("map.surroundings.help"),
		19, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 72
	column.add_child(explanation)
	surroundings_first_button = _button(_t("map.surroundings.mode"), 520, 60)
	surroundings_first_button.pressed.connect(_cycle_surroundings_mode)
	surroundings_mode_button = surroundings_first_button
	column.add_child(surroundings_mode_button)

	surroundings_depth_row = HBoxContainer.new()
	surroundings_depth_row.add_theme_constant_override("separation", 12)
	column.add_child(surroundings_depth_row)
	var depth_minus := _button("−", 110, 58)
	depth_minus.pressed.connect(_change_surroundings_depth.bind(-1))
	surroundings_depth_row.add_child(depth_minus)
	surroundings_depth_label = _label(_t("map.surroundings.depth_short"), 22, Color("f2f7f9"))
	surroundings_depth_label.custom_minimum_size = Vector2(360, 58)
	surroundings_depth_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	surroundings_depth_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	surroundings_depth_row.add_child(surroundings_depth_label)
	var depth_plus := _button("+", 110, 58)
	depth_plus.pressed.connect(_change_surroundings_depth.bind(1))
	surroundings_depth_row.add_child(depth_plus)

	surroundings_terrain_row = HBoxContainer.new()
	surroundings_terrain_row.add_theme_constant_override("separation", 16)
	column.add_child(surroundings_terrain_row)
	var terrain_title := _label(_t("map.surroundings.terrain"), 18, Color("718896"))
	terrain_title.custom_minimum_size.x = 180
	terrain_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	surroundings_terrain_row.add_child(terrain_title)
	surroundings_terrain_button = _button("", 650, 58)
	surroundings_terrain_button.pressed.connect(_cycle_surroundings_terrain)
	surroundings_terrain_row.add_child(surroundings_terrain_button)

	surroundings_colour_controls = VBoxContainer.new()
	surroundings_colour_controls.add_theme_constant_override("separation", 8)
	column.add_child(surroundings_colour_controls)
	var colour_header := HBoxContainer.new()
	colour_header.add_theme_constant_override("separation", 16)
	surroundings_colour_controls.add_child(colour_header)
	colour_header.add_child(_label(_t("map.surroundings.colour"), 16, Color("718896")))
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
		var channel_name: String = [
			_t("map.colour.red"), _t("map.colour.green"), _t("map.colour.blue")
		][channel]
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
	surroundings_apply_button = _button(_t("map.surroundings.apply"), 360, 62)
	surroundings_apply_button.pressed.connect(_apply_surroundings)
	actions.add_child(surroundings_apply_button)
	var cancel := _button(_t("map.cancel"), 220, 62)
	cancel.pressed.connect(_close_surroundings)
	actions.add_child(cancel)


func _build_visibility_overlay() -> void:
	interior_visibility_layer = Control.new()
	interior_visibility_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	interior_visibility_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	interior_visibility_layer.visible = false
	interior_visibility_layer.z_index = 34
	add_child(interior_visibility_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.88)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	interior_visibility_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(470, 190)
	panel.size = Vector2(980, 700)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color("b58cff"))
	)
	interior_visibility_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("map.visibility.heading"), 24, Color("b58cff")))
	visibility_help_label = _label(_t("map.visibility.help"), 19, Color("a9bdc9"))
	visibility_help_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	visibility_help_label.custom_minimum_size.y = 112
	column.add_child(visibility_help_label)
	visibility_summary_label = _label("", 20, Color("f2f7f9"))
	column.add_child(visibility_summary_label)
	var radius_row := HBoxContainer.new()
	radius_row.add_theme_constant_override("separation", 12)
	column.add_child(radius_row)
	visibility_first_button = _button("−", 100, 60)
	visibility_first_button.pressed.connect(_change_visibility_radius.bind(-1))
	radius_row.add_child(visibility_first_button)
	visibility_radius_label = _label("", 22, Color("d6c8ff"))
	visibility_radius_label.custom_minimum_size = Vector2(430, 60)
	visibility_radius_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	visibility_radius_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	radius_row.add_child(visibility_radius_label)
	var radius_plus := _button("+", 100, 60)
	radius_plus.pressed.connect(_change_visibility_radius.bind(1))
	radius_row.add_child(radius_plus)
	var paint := _button(_t("map.visibility.paint"), 880, 68)
	paint.pressed.connect(_begin_visibility_paint)
	column.add_child(paint)
	var clear := _button(_t("map.visibility.clear"), 880, 62)
	clear.pressed.connect(_clear_visibility)
	column.add_child(clear)
	var back := _button(_t("map.back_setup"), 420, 62)
	back.pressed.connect(_close_visibility)
	column.add_child(back)


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


func _terrain_label(terrain_id: String) -> String:
	var known := [
		"grass", "forest", "mountain", "water", "road", "bridge", "sand",
		"ruins", "farmland", "snow", "swamp", "hills", "bamboo", "paved",
	]
	return (
		_t("map.terrain." + terrain_id)
		if known.has(terrain_id)
		else terrain_id.replace("_", " ").to_upper()
	)


func _tool_label(tool: String, compact := false) -> String:
	var known := [
		"brush", "fill", "rectangle", "add_unit", "unit", "move",
		"remove_unit", "deployment", "ownership", "scaling", "talk",
		"visibility",
		"surroundings", "landmark_house", "landmark_grove", "landmark_remove",
		"patrol",
	]
	if not known.has(tool):
		return tool.replace("_", " ").to_upper()
	if compact and tool == "deployment":
		return _t("map.tool.deployment_compact")
	return _t("map.tool." + tool)


func _side_label(side: String) -> String:
	return _t({
		"first": "map.side.first",
		"second": "map.side.second",
		"unit": "map.side.unit",
	}.get(side, "map.side.unit"))


func _field_label(field: String) -> String:
	var normalized := field.replace("_", "").to_lower()
	return _t({
		"map": "map.field.map",
		"placements": "map.field.placements",
		"deployment": "map.field.deployment",
		"objectives": "map.field.objectives",
		"objectiveids": "map.field.objectives",
		"terrain": "map.field.terrain",
		"surroundings": "map.field.surroundings",
		"projectincomplete": "map.error.project_incomplete",
	}.get(normalized, "map.field.battle_setup"))


func _landmark_kind_label(kind: String) -> String:
	return _t(
		"map.landmark.ancient_grove" if kind == "ancient_grove"
		else "map.landmark.village_house"
	)


func _presentation_label(field: String, value: String) -> String:
	return _t(("map.theme." if field == "themeId" else "map.style.") + value)


func _dependency_summary(decision: Dictionary) -> String:
	var reason := str(decision.get("reason", "unsupported_request"))
	var name := str(decision.get("name", dependency_id))
	var count: int = decision.get("uses", []).size()
	match reason:
		"library_content":
			return _t("map.delete.summary.library")
		"content_not_found":
			return _t("map.delete.summary.missing")
		"unused":
			return _t("map.delete.summary.unused", {"name": name})
		"in_use":
			return _t(
				"map.delete.summary.in_use_one" if count == 1
				else "map.delete.summary.in_use_many",
				{"name": name, "count": count}
			)
	return _t("map.delete.summary.unavailable")


func _dependency_use_owner(use: Dictionary) -> String:
	var owner_id := str(use.get("owner_id", ""))
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var flow: Variant = campaign.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if node_value is Dictionary and str(node_value.get("id", "")) == owner_id:
				return _t("map.delete.owner_in_campaign", {
					"battle": str(node_value.get("name", owner_id)),
					"campaign": str(campaign.get("name", campaign.get("id", _t("map.fallback.campaign")))),
				})
	return _t("map.delete.unknown_owner")


func _delete_reason(reason: String) -> String:
	return _t({
		"unsupported_request": "map.delete.reason.unavailable",
		"library_content": "map.delete.reason.shared",
		"content_not_found": "map.delete.reason.missing",
		"in_use": "map.delete.reason.in_use",
	}.get(reason, "map.delete.reason.unsafe"))


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
