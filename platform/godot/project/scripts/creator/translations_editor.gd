# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const LANGUAGE_PATTERN := "^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$"
const FILTERS := ["all", "missing", "outdated"]
const NAMED_COLLECTIONS := [
	"classes", "weaponTypes", "itemTypes", "unitTypes", "weapons", "items",
	"maps", "factions", "abilities", "objectives",
]

var library
var compiler
var project_id := ""
var source := ""
var project: Dictionary = {}
var messages: Array[Dictionary] = []
var visible_messages: Array[Dictionary] = []
var selected_language := ""
var selected_message_id := ""
var active_filter := "all"

var title_label: Label
var source_language_input: LineEdit
var language_select: OptionButton
var add_language_input: LineEdit
var filter_select: OptionButton
var search_input: LineEdit
var summary_label: Label
var message_list: ItemList
var source_label: Label
var translation_input: TextEdit
var preview_label: Label
var status_label: Label
var save_translation_button: Button


func _ready() -> void:
	library = ProjectLibrary.new()
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	project_id = CreatorContext.project_id
	_build_interface()
	_load_project()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		_back_to_projects()
		get_viewport().set_input_as_handled()


static func gather_player_strings(value: Dictionary) -> Array[Dictionary]:
	var gathered: Array[Dictionary] = []
	_add_message(gathered, "project.title", str(value.get("title", "")), "Game title")
	# Gather display copy only. Stable ids, transition keys, script values and
	# author notes are not presented to a player and must not become translation
	# work merely because they contain text.
	for collection: String in NAMED_COLLECTIONS:
		for entry_value: Variant in value.get(collection, []):
			if not entry_value is Dictionary:
				continue
			var entry: Dictionary = entry_value
			var identity: String = str(entry.get("id", ""))
			if identity.is_empty():
				continue
			_add_message(
				gathered,
				"%s.%s.name" % [_snake_case(collection), identity],
				str(entry.get("name", "")),
				"%s name" % _human_collection(collection)
			)
			if entry.has("description"):
				_add_message(
					gathered,
					"%s.%s.description" % [_snake_case(collection), identity],
					str(entry.get("description", "")),
					"%s description" % _human_collection(collection)
				)
	for dialogue_value: Variant in value.get("dialogues", []):
		if not dialogue_value is Dictionary:
			continue
		var dialogue: Dictionary = dialogue_value
		var dialogue_id: String = str(dialogue.get("id", ""))
		_add_message(
			gathered, "dialogues.%s.name" % dialogue_id,
			str(dialogue.get("name", "")), "Scene name"
		)
		var lines: Array = dialogue.get("lines", [])
		for line_index: int in lines.size():
			if not lines[line_index] is Dictionary:
				continue
			var line: Dictionary = lines[line_index]
			_add_message(
				gathered,
				"dialogues.%s.lines.%d.speaker" % [dialogue_id, line_index],
				str(line.get("speaker", "")), "Speaker name"
			)
			_add_message(
				gathered,
				"dialogues.%s.lines.%d.text" % [dialogue_id, line_index],
				str(line.get("text", "")), "Dialogue"
			)
	for campaign_value: Variant in value.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var campaign: Dictionary = campaign_value
		var campaign_id: String = str(campaign.get("id", ""))
		_add_message(
			gathered, "campaigns.%s.name" % campaign_id,
			str(campaign.get("name", "")), "Campaign name"
		)
		_add_member_names(gathered, campaign_id, campaign.get("roster", []), "roster")
		var flow_value: Variant = campaign.get("flow", {})
		if not flow_value is Dictionary:
			continue
		for node_value: Variant in flow_value.get("nodes", []):
			if not node_value is Dictionary:
				continue
			var node: Dictionary = node_value
			var node_id: String = str(node.get("id", ""))
			var node_prefix := "campaigns.%s.nodes.%s" % [campaign_id, node_id]
			_add_message(gathered, node_prefix + ".name", str(node.get("name", "")), "Stage name")
			_add_member_names(gathered, campaign_id, node.get("recruits", []), "recruits")
			_add_placement_names(
				gathered, campaign_id, node_id,
				str(node.get("name", node_id)), node.get("placements", [])
			)
	gathered.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		return str(left.id) < str(right.id)
	)
	return gathered


static func _add_placement_names(
	gathered: Array[Dictionary],
	campaign_id: String,
	node_id: String,
	node_name: String,
	values: Variant
) -> void:
	if campaign_id.is_empty() or node_id.is_empty() or not values is Array:
		return
	for placement_value: Variant in values:
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		var placement_id := str(placement.get("id", ""))
		var placement_name := str(placement.get("name", ""))
		if placement_id.is_empty() or placement_name.is_empty():
			continue
		_add_message(
			gathered,
			"campaigns.%s.nodes.%s.placements.%s.name" % [
				campaign_id, node_id, placement_id,
			],
			placement_name,
			"Named battle character / %s" % node_name
		)


static func _add_member_names(
	gathered: Array[Dictionary], campaign_id: String, values: Variant, kind: String
) -> void:
	if not values is Array:
		return
	for member_value: Variant in values:
		if not member_value is Dictionary:
			continue
		var member: Dictionary = member_value
		_add_message(
			gathered,
			"campaigns.%s.members.%s.name" % [campaign_id, str(member.get("id", ""))],
			str(member.get("name", "")),
			"Character name (%s)" % kind
		)


static func _add_message(
	gathered: Array[Dictionary], identity: String, text: String, context: String
) -> void:
	if identity.is_empty() or text.is_empty():
		return
	for existing: Dictionary in gathered:
		if str(existing.id) == identity:
			return
	gathered.append({"id": identity, "source": text, "context": context})


static func _snake_case(value: String) -> String:
	var result := ""
	for index: int in value.length():
		var character := value[index]
		if character == character.to_upper() and character != character.to_lower():
			result += "_" + character.to_lower()
		else:
			result += character
	return result


static func _human_collection(value: String) -> String:
	return _snake_case(value).replace("_", " ").trim_suffix("s").capitalize()


func translation_status(language: String, message_id: String) -> String:
	var message := _message_by_id(message_id)
	if message.is_empty():
		return "missing"
	var entry := _entry(language, message_id)
	if entry.is_empty() or str(entry.get("text", "")).is_empty():
		return "missing"
	if str(entry.get("source", "")) != str(message.source):
		return "outdated"
	return "translated"


func fallback_text(language: String, message_id: String) -> String:
	var message := _message_by_id(message_id)
	if message.is_empty():
		return ""
	if language == str(project.get("sourceLanguage", "en")):
		return str(message.source)
	var entry := _entry(language, message_id)
	if entry.is_empty() or str(entry.get("source", "")) != str(message.source):
		return str(message.source)
	return str(entry.get("text", message.source))


func _load_project() -> void:
	if project_id.is_empty():
		_set_status(_t("translations.error.no_project"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not bool(loaded.get("ok", false)):
		_set_status(_t("translations.error.open"), true)
		return
	source = str(loaded.source)
	project = loaded.project
	messages = gather_player_strings(project)
	title_label.text = "%s  /  %s" % [
		str(project.get("title", "Untitled Game")).to_upper(),
		_t("translations.heading")
	]
	source_language_input.text = str(project.get("sourceLanguage", "en"))
	_refresh_languages()
	_refresh_messages()
	_set_status(_t("translations.ready"), false)


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	title_label = _label(_t("translations.heading"), 30, Color("f2f7f9"))
	title_label.position = Vector2(60, 34)
	title_label.size = Vector2(1800, 50)
	add_child(title_label)

	var top := HBoxContainer.new()
	top.position = Vector2(60, 98)
	top.size = Vector2(1800, 70)
	top.add_theme_constant_override("separation", 12)
	add_child(top)
	top.add_child(_label(_t("translations.source_language"), 17, Color("69d9ff")))
	source_language_input = LineEdit.new()
	source_language_input.custom_minimum_size = Vector2(150, 54)
	source_language_input.max_length = 35
	top.add_child(source_language_input)
	var save_source := _button(_t("translations.save_source"), 210)
	save_source.pressed.connect(_save_source_language)
	top.add_child(save_source)
	top.add_child(_label(_t("translations.language"), 17, Color("69d9ff")))
	language_select = OptionButton.new()
	language_select.custom_minimum_size = Vector2(250, 54)
	language_select.item_selected.connect(_select_catalog)
	top.add_child(language_select)
	add_language_input = LineEdit.new()
	add_language_input.placeholder_text = _t("translations.language_hint")
	add_language_input.custom_minimum_size = Vector2(150, 54)
	add_language_input.max_length = 35
	top.add_child(add_language_input)
	var add := _button(_t("translations.add_language"), 190)
	add.pressed.connect(_add_language)
	top.add_child(add)
	var portuguese := _button("+ PT-BR", 135)
	portuguese.tooltip_text = _t("translations.add_portuguese")
	portuguese.pressed.connect(_add_language_code.bind("pt-BR"))
	top.add_child(portuguese)

	var filters := HBoxContainer.new()
	filters.position = Vector2(60, 177)
	filters.size = Vector2(1800, 60)
	filters.add_theme_constant_override("separation", 12)
	add_child(filters)
	filters.add_child(_label(_t("translations.show"), 17, Color("69d9ff")))
	filter_select = OptionButton.new()
	filter_select.custom_minimum_size = Vector2(250, 50)
	for filter: String in FILTERS:
		filter_select.add_item(_t("translations.filter.%s" % filter))
		filter_select.set_item_metadata(filter_select.item_count - 1, filter)
	filter_select.item_selected.connect(_select_filter)
	filters.add_child(filter_select)
	search_input = LineEdit.new()
	search_input.placeholder_text = _t("translations.search")
	search_input.clear_button_enabled = true
	search_input.custom_minimum_size = Vector2(440, 50)
	search_input.add_theme_font_size_override("font_size", 17)
	search_input.text_changed.connect(_search_messages)
	filters.add_child(search_input)
	summary_label = _label("", 17, Color("a9bdc9"))
	summary_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	filters.add_child(summary_label)

	var left := PanelContainer.new()
	left.position = Vector2(60, 250)
	left.size = Vector2(760, 690)
	left.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), Color(0.20, 0.62, 0.78, 0.35)))
	add_child(left)
	message_list = ItemList.new()
	message_list.add_theme_font_size_override("font_size", 17)
	message_list.item_selected.connect(_select_message)
	left.add_child(message_list)

	var right := PanelContainer.new()
	right.position = Vector2(845, 250)
	right.size = Vector2(1015, 690)
	right.add_theme_stylebox_override("panel", _panel(Color("0c1d29"), Color(0.20, 0.62, 0.78, 0.35)))
	add_child(right)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	right.add_child(column)
	column.add_child(_label(_t("translations.source_copy"), 16, Color("69d9ff")))
	source_label = _label(_t("translations.choose_string"), 20, Color("f2f7f9"))
	source_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	source_label.custom_minimum_size.y = 130
	column.add_child(source_label)
	column.add_child(_label(_t("translations.translation"), 16, Color("69d9ff")))
	translation_input = TextEdit.new()
	translation_input.custom_minimum_size.y = 190
	translation_input.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	column.add_child(translation_input)
	save_translation_button = _button(_t("translations.save_translation"), 0)
	save_translation_button.disabled = true
	save_translation_button.pressed.connect(_save_translation)
	column.add_child(save_translation_button)
	column.add_child(_label(_t("translations.player_preview"), 16, Color("69d9ff")))
	preview_label = _label("", 19, Color("a9bdc9"))
	preview_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	preview_label.custom_minimum_size.y = 85
	column.add_child(preview_label)

	status_label = _label("", 16, Color("a9bdc9"))
	status_label.position = Vector2(60, 960)
	status_label.size = Vector2(1450, 55)
	add_child(status_label)
	var back := _button(_t("common.back"), 300)
	back.position = Vector2(1560, 952)
	back.size = Vector2(300, 62)
	back.pressed.connect(_back_to_projects)
	add_child(back)


func _refresh_languages() -> void:
	var keep := selected_language
	language_select.clear()
	for catalog_value: Variant in project.get("translations", []):
		if not catalog_value is Dictionary:
			continue
		var language: String = str(catalog_value.get("language", ""))
		language_select.add_item(_language_name(language))
		language_select.set_item_metadata(language_select.item_count - 1, language)
		if language == keep:
			language_select.select(language_select.item_count - 1)
	if language_select.item_count == 0:
		selected_language = ""
		language_select.add_item(_t("translations.no_language"))
		language_select.disabled = true
	else:
		language_select.disabled = false
		if keep.is_empty() or not _catalog(keep):
			language_select.select(0)
		selected_language = str(language_select.get_item_metadata(language_select.selected))


func _refresh_messages() -> void:
	visible_messages.clear()
	message_list.clear()
	var query := "" if search_input == null else search_input.text.strip_edges().to_lower()
	var missing := 0
	var outdated := 0
	var translated := 0
	for message: Dictionary in messages:
		var state := translation_status(selected_language, str(message.id))
		match state:
			"missing": missing += 1
			"outdated": outdated += 1
			_: translated += 1
		if active_filter != "all" and state != active_filter:
			continue
		var searchable := "%s %s %s" % [
			str(message.get("id", "")),
			str(message.get("context", "")),
			str(message.get("source", "")),
		]
		if not query.is_empty() and not searchable.to_lower().contains(query):
			continue
		visible_messages.append(message)
		var marker := "✓" if state == "translated" else ("!" if state == "outdated" else "·")
		message_list.add_item("%s  %s\n%s" % [marker, str(message.context), str(message.source)])
		message_list.set_item_metadata(message_list.item_count - 1, str(message.id))
	summary_label.text = _t(
		"translations.summary",
		{"translated": translated, "missing": missing, "outdated": outdated}
	)
	var chosen := -1
	for index: int in message_list.item_count:
		if str(message_list.get_item_metadata(index)) == selected_message_id:
			chosen = index
			break
	if chosen < 0 and message_list.item_count > 0:
		chosen = 0
	if chosen >= 0:
		message_list.select(chosen)
		_select_message(chosen)
	else:
		selected_message_id = ""
		source_label.text = _t("translations.no_strings")
		translation_input.text = ""
		preview_label.text = ""
		save_translation_button.disabled = true


func _select_catalog(index: int) -> void:
	if language_select.disabled or index < 0:
		return
	selected_language = str(language_select.get_item_metadata(index))
	_refresh_messages()


func _select_filter(index: int) -> void:
	if index < 0:
		return
	active_filter = str(filter_select.get_item_metadata(index))
	_refresh_messages()


func _search_messages(_value: String) -> void:
	_refresh_messages()


func _select_message(index: int) -> void:
	if index < 0 or index >= message_list.item_count:
		return
	selected_message_id = str(message_list.get_item_metadata(index))
	var message := _message_by_id(selected_message_id)
	var entry := _entry(selected_language, selected_message_id)
	source_label.text = "%s\n%s" % [str(message.context).to_upper(), str(message.source)]
	translation_input.text = str(entry.get("text", ""))
	preview_label.text = "%s  /  %s" % [
		translation_status(selected_language, selected_message_id).to_upper(),
		fallback_text(selected_language, selected_message_id)
	]
	save_translation_button.disabled = selected_language.is_empty()


func _add_language() -> void:
	_add_language_code(add_language_input.text.strip_edges())


func _add_language_code(language: String) -> void:
	var expression := RegEx.new()
	expression.compile(LANGUAGE_PATTERN)
	if expression.search(language) == null:
		_set_status(_t("translations.error.language_tag"), true)
		return
	if language == str(project.get("sourceLanguage", "en")):
		_set_status(_t("translations.error.source_catalog"), true)
		return
	if not _catalog(language).is_empty():
		selected_language = language
		_refresh_languages()
		_refresh_messages()
		_set_status(_t("translations.language_exists"), false)
		return
	var candidate := project.duplicate(true)
	var catalogs: Array = candidate.get("translations", []).duplicate(true)
	catalogs.append({"language": language, "entries": []})
	candidate["translations"] = catalogs
	if _save_candidate(candidate):
		selected_language = language
		add_language_input.text = ""
		_refresh_languages()
		_refresh_messages()
		_set_status(_t("translations.language_added", {"language": _language_name(language)}), false)


func _save_source_language() -> void:
	var language := source_language_input.text.strip_edges()
	var expression := RegEx.new()
	expression.compile(LANGUAGE_PATTERN)
	if expression.search(language) == null:
		_set_status(_t("translations.error.language_tag"), true)
		return
	if not _catalog(language).is_empty():
		_set_status(_t("translations.error.source_catalog"), true)
		return
	var candidate := project.duplicate(true)
	candidate["sourceLanguage"] = language
	if _save_candidate(candidate):
		_set_status(_t("translations.source_saved", {"language": language}), false)


func _save_translation() -> void:
	if selected_language.is_empty() or selected_message_id.is_empty():
		return
	var text := translation_input.text.strip_edges()
	if text.is_empty():
		_set_status(_t("translations.error.empty"), true)
		return
	var message := _message_by_id(selected_message_id)
	var candidate := project.duplicate(true)
	var catalogs: Array = candidate.get("translations", [])
	for catalog_index: int in catalogs.size():
		if not catalogs[catalog_index] is Dictionary:
			continue
		if str(catalogs[catalog_index].get("language", "")) != selected_language:
			continue
		var catalog: Dictionary = catalogs[catalog_index]
		var entries: Array = catalog.get("entries", [])
		var replaced := false
		for entry_index: int in entries.size():
			if entries[entry_index] is Dictionary and str(entries[entry_index].get("id", "")) == selected_message_id:
				entries[entry_index] = {
					"id": selected_message_id,
					"source": str(message.source),
					"text": text,
				}
				replaced = true
				break
		if not replaced:
			entries.append({
				"id": selected_message_id,
				"source": str(message.source),
				"text": text,
			})
		entries.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
			return str(left.id) < str(right.id)
		)
		catalog["entries"] = entries
		catalogs[catalog_index] = catalog
		break
	candidate["translations"] = catalogs
	if _save_candidate(candidate):
		_refresh_messages()
		_set_status(_t("translations.translation_saved"), false)


func _save_candidate(candidate: Dictionary) -> bool:
	var portable: Dictionary = _integer_numbers(candidate)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	for diagnostic: Dictionary in checked.get("diagnostics", []):
		if str(diagnostic.get("phase", "")) == "source":
			_set_status(_t("translations.error.invalid"), true)
			return false
	var saved: Dictionary = library.save_source(project_id, candidate_source)
	if not bool(saved.get("ok", false)):
		_set_status(_t("translations.error.save"), true)
		return false
	project = portable
	source = candidate_source
	messages = gather_player_strings(project)
	return true


func _catalog(language: String) -> Dictionary:
	for value: Variant in project.get("translations", []):
		if value is Dictionary and str(value.get("language", "")) == language:
			return value
	return {}


func _entry(language: String, message_id: String) -> Dictionary:
	var catalog := _catalog(language)
	for value: Variant in catalog.get("entries", []):
		if value is Dictionary and str(value.get("id", "")) == message_id:
			return value
	return {}


func _message_by_id(message_id: String) -> Dictionary:
	for message: Dictionary in messages:
		if str(message.id) == message_id:
			return message
	return {}


func _language_name(language: String) -> String:
	match language:
		"en": return "English  /  en"
		"es": return "Español  /  es"
		"pt-BR": return "Português (Brasil)  /  pt-BR"
		_: return language


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


func _back_to_projects() -> void:
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _set_status(message: String, failure: bool) -> void:
	status_label.text = message
	status_label.add_theme_color_override("font_color", Color("ff7a83") if failure else Color("72e08f"))


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, 54)
	button.add_theme_font_size_override("font_size", 17)
	return button


func _panel(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(16)
	style.set_content_margin_all(18)
	return style
