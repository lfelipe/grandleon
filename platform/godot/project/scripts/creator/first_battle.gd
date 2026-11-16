# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const CampaignRoster = preload("res://scripts/creator/campaign_roster.gd")
const CampaignEditor = preload("res://scripts/creator/campaign_editor.gd")

var library
var compiler
var project: Dictionary = {}
var campaign: Dictionary = {}
var encounter: Dictionary = {}
var source := ""
var campaign_select: OptionButton
var battle_select: OptionButton
var title_label: Label
var hero_summary: Label
var map_summary: Label
var sides_summary: Label
var objective_summary: Label
var test_summary: Label
var status_label: Label
var solo_button: Button
var coop_button: Button
var create_hero_button: Button
var edit_heroes_button: Button
var map_button: Button
var sides_button: Button
var objective_button: Button
var next_battle_button: Button

func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	CreatorContext.guided_workflow = true
	_build_interface()
	_fit_mobile_canvas()
	get_viewport().size_changed.connect(_fit_mobile_canvas)
	_load_project()

func _load_project() -> void:
	var loaded: Dictionary = library.load_source(CreatorContext.project_id)
	if not bool(loaded.get("ok", false)):
		_set_status(_t("first_battle.open_error"), true)
		return
	source = str(loaded.source)
	project = loaded.project
	campaign_select.clear()
	for value: Variant in project.get("campaigns", []):
		if not value is Dictionary: continue
		campaign_select.add_item(str(value.get("name", value.get("id", ""))))
		campaign_select.set_item_metadata(campaign_select.item_count - 1, str(value.get("id", "")))
		if str(value.get("id", "")) == CreatorContext.campaign_id:
			campaign_select.select(campaign_select.item_count - 1)
	campaign_select.disabled = campaign_select.item_count <= 1
	_select_campaign(campaign_select.selected)

func _select_campaign(index: int) -> void:
	campaign = {}
	if index >= 0:
		campaign = _record(project.get("campaigns", []), str(campaign_select.get_item_metadata(index)))
	CreatorContext.choose_campaign(str(campaign.get("id", "")))
	battle_select.clear()
	for value: Variant in campaign.get("flow", {}).get("nodes", []):
		if not value is Dictionary or str(value.get("kind", "")) != "encounter": continue
		battle_select.add_item(str(value.get("name", value.get("id", ""))))
		battle_select.set_item_metadata(battle_select.item_count - 1, str(value.get("id", "")))
		if str(value.get("id", "")) == CreatorContext.encounter_id:
			battle_select.select(battle_select.item_count - 1)
	battle_select.disabled = battle_select.item_count <= 1
	_select_battle(battle_select.selected)

func _select_battle(index: int) -> void:
	encounter = {}
	if index >= 0:
		encounter = _record(campaign.get("flow", {}).get("nodes", []), str(battle_select.get_item_metadata(index)))
	CreatorContext.choose_map(str(encounter.get("mapId", "")), str(encounter.get("id", "")))
	_refresh_facts()

func _refresh_facts() -> void:
	title_label.text = _t("first_battle.title" if battle_select.item_count <= 1 else "first_battle.stage_title")
	var heroes: Array[String] = []
	var later_heroes: Array[String] = []
	for member: Dictionary in CampaignRoster.members(campaign):
		var hero_name := str(member.get("name", member.get("id", "")))
		heroes.append(hero_name)
		if not str(member.join_node_id).is_empty():
			later_heroes.append(_t("first_battle.hero_joins_after", {"name": hero_name, "stage": str(member.join_node_name)}))
	hero_summary.text = _t("first_battle.heroes_summary", {"count": heroes.size(), "names": ", ".join(heroes)}) if not heroes.is_empty() else _t("first_battle.no_heroes")
	if not later_heroes.is_empty(): hero_summary.text += "\n" + "\n".join(later_heroes)
	var map := _record(project.get("maps", []), str(encounter.get("mapId", "")))
	map_summary.text = _t("first_battle.map_summary", {"name": str(map.get("name", "")), "width": int(map.get("width", 0)), "height": int(map.get("height", 0))})
	var solo_allies := 0
	var solo_enemies := 0
	var coop_allies := 0
	var coop_enemies := 0
	var player_two := false
	for value: Variant in encounter.get("placements", []):
		if not value is Dictionary: continue
		var ally := str(value.get("side", "second")) == "first"
		if ally:
			coop_allies += 1
			player_two = player_two or int(value.get("player", 1)) == 2
			if int(value.get("minimumPlayers", 1)) <= 1: solo_allies += 1
		else:
			coop_enemies += 1
			if int(value.get("minimumPlayers", 1)) <= 1: solo_enemies += 1
	sides_summary.text = _t("first_battle.sides_summary", {"solo_allies": solo_allies, "solo_enemies": solo_enemies, "coop_allies": coop_allies, "coop_enemies": coop_enemies})
	var objectives: Array[String] = []
	for identity: Variant in encounter.get("objectiveIds", []):
		var objective := _record(project.get("objectives", []), str(identity))
		objectives.append(str(objective.get("name", identity)))
	objective_summary.text = ", ".join(objectives) if not objectives.is_empty() else _t("first_battle.no_objective")
	var mode := str(campaign.get("playerMode", "one"))
	solo_button.disabled = encounter.is_empty() or mode == "two"
	coop_button.disabled = encounter.is_empty() or mode == "one" or not player_two
	solo_button.tooltip_text = _t("campaign.test.solo_unavailable_tooltip" if mode == "two" else "campaign.test.solo_tooltip")
	coop_button.tooltip_text = _t("campaign.test.coop_unavailable_tooltip" if coop_button.disabled else "campaign.test.coop_tooltip")
	test_summary.text = _t("first_battle.test_help")
	if mode != "one" and not player_two:
		test_summary.text += "\n" + _t("first_battle.needs_player_two")
	for button: Button in [map_button, sides_button, objective_button, next_battle_button]:
		button.disabled = encounter.is_empty()
	create_hero_button.disabled = campaign.is_empty()
	edit_heroes_button.disabled = campaign.is_empty()

func _record(values: Variant, identity: String) -> Dictionary:
	if values is Array:
		for value: Variant in values:
			if value is Dictionary and str(value.get("id", "")) == identity: return value
	return {}

func _open_heroes(create: bool) -> void:
	CreatorContext.army_action = "create_hero" if create else ""
	get_tree().change_scene_to_file("res://army_editor.tscn")

func _open_map(action: String) -> void:
	CreatorContext.request_map_action(action)
	get_tree().change_scene_to_file("res://map_editor.tscn")

func _open_story() -> void:
	# Start with a scene attached to this campaign, keeping reusable scenes in
	# the story editor available without silently choosing another campaign.
	CreatorContext.choose_dialogue("")
	for value: Variant in campaign.get("flow", {}).get("nodes", []):
		if value is Dictionary and str(value.get("kind", "")) == "story":
			var scenes: Array = value.get("dialogueIds", [])
			if not scenes.is_empty():
				CreatorContext.choose_dialogue(str(scenes[0]))
				break
	get_tree().change_scene_to_file("res://story_editor.tscn")

func _open_outline() -> void:
	CreatorContext.guided_workflow = false
	get_tree().change_scene_to_file("res://campaign_editor.tscn")

func _open_translations() -> void:
	get_tree().change_scene_to_file("res://translations_editor.tscn")

func _open_export() -> void:
	CreatorContext.hub_action = "export"
	get_tree().change_scene_to_file("res://creator.tscn")

func _open_projects() -> void:
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")

func _play_battle(players: int) -> void:
	if (players == 1 and solo_button.disabled) or (players == 2 and coop_button.disabled): return
	# Reuse the existing direct-battle builder. This transient control never
	# enters the tree, opens UI, or saves its temporary preview source.
	var builder = CampaignEditor.new()
	builder.project = project
	builder.campaign = campaign
	var preview: Dictionary = builder._battle_preview_source(str(encounter.get("id", "")))
	builder.free()
	if not bool(preview.get("ok", false)):
		_set_status(str(preview.get("message", _t("first_battle.test_error"))), true)
		return
	var compiled: Dictionary = compiler.compile_source_json(str(preview.source))
	if not bool(compiled.get("ok", false)):
		_set_status(_t("first_battle.test_error"), true)
		return
	LaunchContext.stage_package(compiled.package, str(campaign.get("id", "")), true, players,
		"res://first_battle.tscn", str(encounter.get("name", "")))
	get_tree().change_scene_to_file("res://main.tscn")

func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	var frame := MarginContainer.new()
	frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for edge: String in ["left", "right", "top", "bottom"]: frame.add_theme_constant_override("margin_" + edge, 48)
	add_child(frame)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	frame.add_child(scroll)
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 16)
	scroll.add_child(column)
	title_label = _label(_t("first_battle.title"), 34)
	column.add_child(title_label)
	column.add_child(_label(_t("first_battle.intro"), 19))
	var choices := _row(column)
	campaign_select = _choice(choices)
	campaign_select.item_selected.connect(_select_campaign)
	battle_select = _choice(choices)
	battle_select.item_selected.connect(_select_battle)
	var cards := GridContainer.new()
	cards.columns = 2
	cards.set_meta("mobile_fit_columns", true)
	cards.add_theme_constant_override("h_separation", 20)
	cards.add_theme_constant_override("v_separation", 16)
	column.add_child(cards)
	var heroes := _card(cards, "first_battle.heroes")
	hero_summary = _label("", 19)
	heroes.add_child(hero_summary)
	var hero_actions := _row(heroes)
	edit_heroes_button = _button(hero_actions, "first_battle.edit_heroes", _open_heroes.bind(false))
	create_hero_button = _button(hero_actions, "first_battle.create_hero", _open_heroes.bind(true))
	var terrain := _card(cards, "first_battle.map")
	map_summary = _label("", 19)
	terrain.add_child(map_summary)
	map_button = _button(terrain, "first_battle.edit_map", _open_map.bind("terrain"))
	var sides := _card(cards, "first_battle.sides")
	sides_summary = _label("", 19)
	sides.add_child(sides_summary)
	sides.add_child(_label(_t("first_battle.extras_help"), 17))
	sides_button = _button(sides, "first_battle.place_sides", _open_map.bind("place_sides"))
	var objective := _card(cards, "first_battle.objective")
	objective_summary = _label("", 19)
	objective.add_child(objective_summary)
	objective_button = _button(objective, "first_battle.edit_objective", _open_map.bind("objectives"))
	var test := _card(cards, "first_battle.test")
	test_summary = _label("", 18)
	test.add_child(test_summary)
	var tests := _row(test)
	solo_button = _button(tests, "campaign.test.battle_one", _play_battle.bind(1))
	coop_button = _button(tests, "campaign.test.battle_two", _play_battle.bind(2))
	var grow := _card(cards, "first_battle.grow")
	grow.add_child(_label(_t("first_battle.grow_help"), 18))
	next_battle_button = _button(grow, "first_battle.next_battle", _open_map.bind("add_chapter"))
	var tools_row := GridContainer.new()
	tools_row.columns = 3
	tools_row.set_meta("mobile_fit_columns", true)
	tools_row.add_theme_constant_override("h_separation", 12)
	tools_row.add_theme_constant_override("v_separation", 12)
	column.add_child(tools_row)
	_button(tools_row, "first_battle.story", _open_story)
	_button(tools_row, "first_battle.outline", _open_outline)
	_button(tools_row, "first_battle.translations", _open_translations)
	_button(tools_row, "first_battle.export", _open_export)
	_button(tools_row, "first_battle.projects", _open_projects)
	status_label = _label("", 18)
	column.add_child(status_label)

func _card(parent: Container, heading: String) -> VBoxContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size.x = 760
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0c1d29")
	style.border_color = Color("275268")
	style.set_border_width_all(1)
	style.set_corner_radius_all(14)
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 16
	style.content_margin_bottom = 16
	panel.add_theme_stylebox_override("panel", style)
	parent.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	var title := _label(_t(heading), 23)
	title.add_theme_color_override("font_color", Color("69d9ff"))
	column.add_child(title)
	return column

func _row(parent: Container) -> BoxContainer:
	var row := BoxContainer.new()
	row.set_meta("mobile_wrap_actions", true)
	row.add_theme_constant_override("separation", 12)
	parent.add_child(row)
	return row

func _choice(parent: Container) -> OptionButton:
	var choice := OptionButton.new()
	choice.custom_minimum_size = Vector2(300, 58)
	choice.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	choice.fit_to_longest_item = false
	choice.clip_text = true
	choice.add_theme_font_size_override("font_size", 20)
	parent.add_child(choice)
	return choice

func _button(parent: Container, key: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = _t(key)
	button.custom_minimum_size.y = 58
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	button.add_theme_font_size_override("font_size", 20)
	button.pressed.connect(action)
	parent.add_child(button)
	return button

func _label(value: String, font_size: int) -> Label:
	var label := Label.new()
	label.text = value
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", Color("dbe8ee"))
	return label

func _set_status(value: String, failure := false) -> void:
	status_label.text = value
	status_label.add_theme_color_override("font_color", Color("ff7a83") if failure else Color("72e08f"))

func _t(key: String, values: Dictionary = {}) -> String:
	return Localization.text(key, values)

func _fit_mobile_canvas() -> void:
	load("res://scripts/mobile_layout.gd").fit_canvas(self)
	if OS.has_feature("android"):
		load("res://scripts/creator/mobile_creator_layout.gd").install.call_deferred(self)

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		var viewport := get_viewport()
		_open_projects()
		if viewport != null: viewport.set_input_as_handled()

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_GO_BACK_REQUEST:
		_open_projects()
