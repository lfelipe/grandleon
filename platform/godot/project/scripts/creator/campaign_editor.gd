# SPDX-License-Identifier: MIT
extends Control

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const CampaignReadiness = preload("res://scripts/creator/campaign_readiness.gd")
const ProjectContentDependencyIndex = preload(
	"res://scripts/creator/project_content_dependency_index.gd"
)
const HISTORY_LIMIT := 64

var library
var compiler
var project_id := ""
var source := ""
var saved_source := ""
var project: Dictionary = {}
var campaign: Dictionary = {}
var route: Array[Dictionary] = []
var other_nodes: Array[Dictionary] = []
var dirty := false
var undo_stack: Array[String] = []
var redo_stack: Array[String] = []

var title_label: Label
var route_list: VBoxContainer
var route_filter: LineEdit
var target_budget_label: Label
var status_label: Label
var fix_button: Button
var campaign_select: OptionButton
var player_mode_select: OptionButton
var player_mode_help: Label
var test_one_button: Button
var test_two_button: Button
var pending_diagnostic: Dictionary = {}
var first_button: Button
var save_button: Button
var undo_button: Button
var redo_button: Button
var node_buttons: Dictionary = {}
var outcome_buttons: Dictionary = {}
var battle_test_one_buttons: Dictionary = {}
var battle_test_two_buttons: Dictionary = {}
var battle_dependency_buttons: Dictionary = {}
var outcome_layer: Control
var outcome_panel: PanelContainer
var outcome_title: Label
var outcome_select: OptionButton
var outcome_condition: OptionButton
var outcome_objective: OptionButton
var outcome_item: OptionButton
var outcome_flag: OptionButton
var outcome_objective_column: VBoxContainer
var outcome_item_column: VBoxContainer
var outcome_flag_column: VBoxContainer
var outcome_detail_row: HBoxContainer
var outcome_quantity_row: HBoxContainer
var outcome_quantity_label: Label
var outcome_quantity_buttons: Array[Button] = []
var outcome_compound_row: HBoxContainer
var outcome_compound_summary: Label
var outcome_compound_button: Button
var outcome_target: OptionButton
var outcome_apply_button: Button
var outcome_remove_button: Button
var outcome_earlier_button: Button
var outcome_later_button: Button
var outcome_status: Label
var outcome_node_id := ""
var outcome_refreshing := false
var compound_layer: Control
var compound_mode: OptionButton
var compound_select: OptionButton
var compound_kind: OptionButton
var compound_objective: OptionButton
var compound_item: OptionButton
var compound_flag: OptionButton
var compound_detail_row: HBoxContainer
var compound_objective_column: VBoxContainer
var compound_item_column: VBoxContainer
var compound_flag_column: VBoxContainer
var compound_quantity_row: HBoxContainer
var compound_quantity_label: Label
var compound_remove_button: Button
var compound_status: Label
var compound_conditions: Array[Dictionary] = []
var compound_refreshing := false
var add_route_layer: Control
var add_route_button: Button
var add_existing_button: Button
var add_branch_button: Button
var branch_layer: Control
var branch_name: LineEdit
var ending_layer: Control
var ending_kicker: Label
var ending_question: Label
var ending_explanation: Label
var ending_name: LineEdit
var ending_create_button: Button
var ending_node_id := ""
var chapter_order_layer: Control
var chapter_order_button: Button
var chapter_order_select: OptionButton
var chapter_earlier_button: Button
var chapter_later_button: Button
var chapter_order_status: Label
var new_campaign_layer: Control
var new_campaign_name: LineEdit
var new_campaign_create_button: Button
var campaign_dependency_button: Button
var dependency_layer: Control
var dependency_heading: Label
var dependency_summary: Label
var dependency_uses: RichTextLabel
var dependency_confirm_button: Button
var dependency_close_button: Button
var dependency_category := ""
var dependency_id := ""
var dependency_return_button: Button


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
		elif compound_layer != null and compound_layer.visible:
			_close_compound()
		elif new_campaign_layer != null and new_campaign_layer.visible:
			_close_new_campaign()
		elif chapter_order_layer != null and chapter_order_layer.visible:
			_close_chapter_order()
		elif branch_layer != null and branch_layer.visible:
			_close_new_branch()
		elif ending_layer != null and ending_layer.visible:
			_close_new_ending()
		elif add_route_layer != null and add_route_layer.visible:
			_close_add_route()
		elif outcome_layer != null and outcome_layer.visible:
			_close_outcomes()
		else:
			_open_projects()
		get_viewport().set_input_as_handled()


func _load_project() -> void:
	if project_id.is_empty():
		_set_status(_t("campaign.error.no_project"), true)
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status(_t("campaign.error.open", {"message": str(loaded.message)}), true)
		return
	source = str(loaded.get("source", ""))
	saved_source = source
	var value: Variant = loaded.get("project", {})
	if not value is Dictionary:
		_set_status(_t("campaign.error.source_unreadable"), true)
		return
	project = value
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status(_t("campaign.error.add_campaign_first"), true)
		return
	_refresh_campaign_choices(CreatorContext.campaign_id)
	if not _set_campaign_from_selection():
		_set_status(_t("campaign.error.choose_campaign"), true)
		return
	_collect_route()
	_refresh_route()
	_refresh_player_mode()
	_refresh_target_budget()
	_update_actions()
	_set_status(_t("campaign.status.ready"), false)
	if first_button != null:
		first_button.call_deferred("grab_focus")


func _refresh_campaign_choices(preferred_id := "") -> void:
	if campaign_select == null:
		return
	campaign_select.clear()
	var preferred_index := -1
	var campaigns: Array = project.get("campaigns", [])
	for index in campaigns.size():
		if not campaigns[index] is Dictionary:
			continue
		var value: Dictionary = campaigns[index]
		var campaign_id := str(value.get("id", ""))
		campaign_select.add_item(str(value.get(
			"name", campaign_id if not campaign_id.is_empty() else _t("campaign.fallback.campaign")
		)))
		campaign_select.set_item_metadata(campaign_select.item_count - 1, campaign_id)
		if campaign_id == preferred_id:
			preferred_index = campaign_select.item_count - 1
	campaign_select.disabled = campaign_select.item_count <= 1
	if campaign_select.item_count > 0:
		campaign_select.select(preferred_index if preferred_index >= 0 else 0)


func _set_campaign_from_selection() -> bool:
	if campaign_select == null or campaign_select.selected < 0:
		campaign = {}
		return false
	var selected_id := str(campaign_select.get_item_metadata(campaign_select.selected))
	for value: Variant in project.get("campaigns", []):
		if value is Dictionary and str(value.get("id", "")) == selected_id:
			campaign = value
			CreatorContext.choose_campaign(selected_id)
			title_label.text = _t("campaign.project_title", {
				"project": str(project.get("title", _t("campaign.untitled_game"))).to_upper(),
				"campaign": str(value.get("name", _t("campaign.fallback.campaign"))).to_upper(),
			})
			return true
	campaign = {}
	return false


func _change_campaign(index: int) -> void:
	if campaign_select == null or index < 0 or index >= campaign_select.item_count:
		return
	if not _set_campaign_from_selection():
		_set_status(_t("campaign.error.campaign_missing"), true)
		return
	_collect_route()
	_refresh_route()
	_refresh_player_mode()
	_refresh_target_budget()
	_update_actions()
	_set_status(_t("campaign.status.editing", {
		"campaign": str(campaign.get(
			"name", campaign.get("id", _t("campaign.fallback.campaign"))
		)).to_upper(),
	}), false)
	if first_button != null:
		first_button.grab_focus()


func _open_new_campaign() -> void:
	new_campaign_name.text = "New Campaign"
	new_campaign_layer.visible = true
	new_campaign_name.grab_focus()
	new_campaign_name.select_all()


func _close_new_campaign() -> void:
	new_campaign_layer.visible = false
	campaign_select.grab_focus()


func _create_new_campaign_from_text(_value: String) -> void:
	_create_new_campaign()


func _create_new_campaign() -> void:
	var name := new_campaign_name.text.strip_edges()
	if name.is_empty():
		_set_status(_t("campaign.error.campaign_name"), true)
		new_campaign_name.grab_focus()
		return
	if campaign.is_empty():
		_set_status(_t("campaign.error.working_campaign_first"), true)
		return
	var candidate := project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	var added: Dictionary = campaign.duplicate(true)
	var added_id := _next_campaign_id(campaigns)
	added["id"] = added_id
	added["name"] = name
	campaigns.append(added)
	candidate["campaigns"] = campaigns
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("campaign.error.create_campaign", {
			"detail": _diagnostic_summary(checked),
		}), true)
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	campaign = {"id": added_id}
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	new_campaign_layer.visible = false
	_set_status(_t("campaign.status.created", {"name": name.to_upper()}), false)
	campaign_select.grab_focus()


func _next_campaign_id(campaigns: Array) -> String:
	var used: Dictionary = {}
	for value: Variant in campaigns:
		if value is Dictionary:
			used[str(value.get("id", ""))] = true
	var sequence := campaigns.size() + 1
	while used.has("campaign_%d" % sequence):
		sequence += 1
	return "campaign_%d" % sequence


func _open_dependency_review(category: String, identity: String, return_button: Button) -> void:
	dependency_category = category
	dependency_id = identity
	dependency_return_button = return_button
	_refresh_dependency_review()
	dependency_layer.visible = true
	if dependency_confirm_button.disabled:
		dependency_close_button.grab_focus()
	else:
		dependency_confirm_button.grab_focus()


func _close_dependency_review() -> void:
	dependency_layer.visible = false
	if (
		dependency_return_button != null
		and is_instance_valid(dependency_return_button)
		and dependency_return_button.is_inside_tree()
	):
		dependency_return_button.grab_focus()


func _dependency_decision(category: String, identity: String) -> Dictionary:
	var decision := ProjectContentDependencyIndex.new(project).deletion_decision(
		category, identity
	)
	if (
		category == "campaign"
		and bool(decision.get("allowed", false))
		and project.get("campaigns", []).size() <= 1
	):
		decision["allowed"] = false
		decision["blocked"] = true
		decision["reason"] = "last_campaign"
		decision["summary"] = _t("campaign.delete.summary.last_campaign")
	return decision


func _dependency_summary(decision: Dictionary) -> String:
	var reason := str(decision.get("reason", "unsupported_request"))
	var kind := _t(
		"campaign.kind.campaign_name"
		if dependency_category == "campaign" else "campaign.kind.battle_name"
	)
	var name := str(decision.get("name", dependency_id))
	var use_count: int = decision.get("uses", []).size()
	match reason:
		"last_campaign":
			return _t("campaign.delete.summary.last_campaign")
		"content_not_found":
			return _t("campaign.delete.summary.missing", {"kind": kind})
		"unused":
			return _t("campaign.delete.summary.unused", {"kind": kind, "name": name})
		"in_use":
			return _t(
				"campaign.delete.summary.in_use_one"
				if use_count == 1 else "campaign.delete.summary.in_use_many",
				{"kind": kind, "name": name, "count": use_count}
			)
		_:
			return _t("campaign.delete.summary.unavailable")


func _dependency_section_label(section: String) -> String:
	var message_id := str({
		"Project": "campaign.delete.section.project",
		"Campaigns": "campaign.delete.section.campaigns",
		"Battles": "campaign.delete.section.battles",
		"Dialogue": "campaign.delete.section.dialogue",
		"Maps": "campaign.delete.section.maps",
		"Objectives": "campaign.delete.section.objectives",
	}.get(section, ""))
	return _t(message_id) if not message_id.is_empty() else section.to_upper()


func _dependency_owner_name(use: Dictionary) -> String:
	var owner_kind := str(use.get("owner_category", ""))
	var owner_id := str(use.get("owner_id", ""))
	if owner_id.is_empty():
		return _t("campaign.delete.project_owner")
	if owner_kind == "campaign":
		var found := _record_name(project.get("campaigns", []), owner_id)
		if not found.is_empty():
			return found
	if owner_kind in ["battle", "campaign_node"]:
		for campaign_value: Variant in project.get("campaigns", []):
			if campaign_value is Dictionary:
				var flow: Variant = campaign_value.get("flow", {})
				if flow is Dictionary:
					var found := _record_name(flow.get("nodes", []), owner_id)
					if not found.is_empty():
						return _t("campaign.delete.owner_in_campaign", {
							"campaign": str(campaign_value.get(
								"name", campaign_value.get("id", _t("campaign.fallback.campaign"))
							)),
							"node": found,
						})
	return owner_id.replace("_", " ").capitalize()


func _record_name(values: Variant, identity: String) -> String:
	if values is Array:
		for value: Variant in values:
			if value is Dictionary and str(value.get("id", "")) == identity:
				return str(value.get("name", identity.replace("_", " ").capitalize()))
	return ""


func _delete_reason(reason: String) -> String:
	var message_id := str({
		"unsupported_request": "campaign.delete.reason.unavailable",
		"last_campaign": "campaign.delete.reason.last_campaign",
		"content_not_found": "campaign.delete.reason.missing",
		"in_use": "campaign.delete.reason.in_use",
	}.get(reason, "campaign.delete.reason.unsafe"))
	return _t(message_id)


func _campaign_after_delete(campaign_id: String) -> Dictionary:
	for value: Variant in project.get("campaigns", []):
		if value is Dictionary and str(value.get("id", "")) != campaign_id:
			return value
	return {}


func _refresh_dependency_review() -> Dictionary:
	var decision := _dependency_decision(dependency_category, dependency_id)
	var category_label := _t(
		"campaign.kind.campaign" if dependency_category == "campaign" else "campaign.kind.battle"
	)
	dependency_heading.text = _t("campaign.delete.where_used", {
		"kind": category_label,
		"name": str(decision.get("name", dependency_id)).to_upper(),
	})
	var summary := _dependency_summary(decision)
	if dependency_category == "campaign" and bool(decision.get("allowed", false)):
		var continuation := _campaign_after_delete(dependency_id)
		if continuation.is_empty():
			decision["allowed"] = false
			decision["blocked"] = true
			decision["reason"] = "last_campaign"
			summary = _t("campaign.delete.summary.no_continuation")
		else:
			summary += _t("campaign.delete.summary.continuation", {
				"campaign": str(continuation.get(
					"name", continuation.get("id", _t("campaign.fallback.campaign"))
				)),
			})
	dependency_summary.text = summary
	var lines: Array[String] = []
	for value: Variant in decision.get("uses", []):
		if value is Dictionary:
			lines.append(_t("campaign.delete.use", {
				"section": _dependency_section_label(str(value.get("section", "Project"))),
				"owner": _dependency_owner_name(value),
			}))
	if lines.is_empty():
		dependency_uses.text = (
			_t("campaign.delete.no_campaign_uses")
			if dependency_category == "campaign"
			else _t("campaign.delete.no_battle_uses")
		)
	else:
		dependency_uses.text = "\n\n".join(lines)
	var allowed := (
		dependency_category in ["campaign", "battle"]
		and bool(decision.get("allowed", false))
	)
	dependency_confirm_button.disabled = not allowed
	dependency_confirm_button.text = (
		_t("campaign.delete.confirm", {"kind": category_label})
		if allowed else _t("campaign.delete.cannot")
	)
	return decision


func _confirm_dependency_delete() -> bool:
	var decision := _refresh_dependency_review()
	if (
		dependency_category not in ["campaign", "battle"]
		or not bool(decision.get("allowed", false))
	):
		_set_status(_t("campaign.error.not_deleted", {
			"reason": _delete_reason(str(decision.get("reason", "unsupported_request"))),
		}), true)
		return false
	var selected_campaign_id := str(campaign.get("id", ""))
	if selected_campaign_id.is_empty():
		_set_status(_t("campaign.error.delete_choose_campaign"), true)
		return false
	if dependency_category == "campaign" and dependency_id != selected_campaign_id:
		_set_status(_t("campaign.error.delete_campaign_changed"), true)
		return false
	var candidate := project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	var removed_name := str(decision.get("name", dependency_id))
	var continuation_id := selected_campaign_id
	var removed := false
	if dependency_category == "campaign":
		for index in campaigns.size():
			if (
				campaigns[index] is Dictionary
				and str(campaigns[index].get("id", "")) == dependency_id
			):
				campaigns.remove_at(index)
				removed = true
				break
		var continuation := _campaign_after_delete(dependency_id)
		continuation_id = str(continuation.get("id", ""))
	else:
		for campaign_index in campaigns.size():
			if (
				not campaigns[campaign_index] is Dictionary
				or str(campaigns[campaign_index].get("id", "")) != selected_campaign_id
			):
				continue
			var changed_campaign: Dictionary = campaigns[campaign_index].duplicate(true)
			var flow: Dictionary = changed_campaign.get("flow", {}).duplicate(true)
			var nodes: Array = flow.get("nodes", []).duplicate(true)
			for node_index in nodes.size():
				if (
					nodes[node_index] is Dictionary
					and str(nodes[node_index].get("kind", "")) == "encounter"
					and str(nodes[node_index].get("id", "")) == dependency_id
				):
					nodes.remove_at(node_index)
					removed = true
					break
			flow["nodes"] = nodes
			changed_campaign["flow"] = flow
			campaigns[campaign_index] = changed_campaign
			break
	if not removed or continuation_id.is_empty():
		_set_status(_t("campaign.error.delete_missing"), true)
		_refresh_dependency_review()
		return false
	candidate["campaigns"] = campaigns
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("campaign.error.delete", {
			"detail": _diagnostic_summary(checked),
		}), true)
		return false
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	campaign = {"id": continuation_id}
	if not _read_source():
		return false
	_collect_route()
	_refresh_route()
	_update_actions()
	dependency_layer.visible = false
	_set_status(_t("campaign.status.deleted", {
		"kind": _t(
			"campaign.kind.campaign"
			if dependency_category == "campaign" else "campaign.kind.battle"
		),
		"name": removed_name.to_upper(),
	}), false)
	if dependency_category == "campaign":
		campaign_select.grab_focus()
	elif first_button != null:
		first_button.grab_focus()
	return true


func _collect_route() -> void:
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		route = []
		other_nodes = []
		return
	var flow: Dictionary = flow_value
	var nodes: Array = flow.get("nodes", [])
	route = _primary_route(nodes, str(flow.get("entryNodeId", "")))
	var primary_ids: Dictionary = {}
	for node: Dictionary in route:
		primary_ids[str(node.get("id", ""))] = true
	other_nodes.clear()
	for node_value: Variant in nodes:
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		if not primary_ids.has(str(node.get("id", ""))):
			other_nodes.append(node)


func _primary_route(nodes: Array, entry_id: String) -> Array[Dictionary]:
	var by_id: Dictionary = {}
	for node_value: Variant in nodes:
		if node_value is Dictionary:
			var node: Dictionary = node_value
			var node_id := str(node.get("id", ""))
			if not node_id.is_empty():
				by_id[node_id] = node
	var result: Array[Dictionary] = []
	var visited: Dictionary = {}
	var current_id := entry_id
	while not current_id.is_empty() and by_id.has(current_id) and not visited.has(current_id):
		visited[current_id] = true
		var current: Dictionary = by_id[current_id]
		result.append(current)
		var next_id := ""
		var best_priority := 2147483647
		for transition_value: Variant in current.get("transitions", []):
			if not transition_value is Dictionary:
				continue
			var transition: Dictionary = transition_value
			var target := str(transition.get("targetNodeId", ""))
			var priority := int(transition.get("priority", 0))
			if target.is_empty() or priority >= best_priority:
				continue
			best_priority = priority
			next_id = target
		current_id = next_id
	return result


func _chapter_pairs() -> Array[Dictionary]:
	var chapters: Array[Dictionary] = []
	for index in range(route.size() - 1):
		var story: Dictionary = route[index]
		var battle: Dictionary = route[index + 1]
		if (
			str(story.get("kind", "")) == "story"
			and str(battle.get("kind", "")) == "encounter"
		):
			chapters.append({"story": story, "battle": battle})
	return chapters


func _open_chapter_order() -> void:
	_refresh_chapter_order()
	chapter_order_layer.visible = true
	chapter_order_select.grab_focus()


func _close_chapter_order() -> void:
	chapter_order_layer.visible = false
	if chapter_order_button != null:
		chapter_order_button.grab_focus()


func _refresh_chapter_order(preferred_story_id := "") -> void:
	var previous_id := preferred_story_id
	if previous_id.is_empty() and chapter_order_select.selected >= 0:
		previous_id = str(chapter_order_select.get_item_metadata(
			chapter_order_select.selected
		))
	chapter_order_select.clear()
	var chapters := _chapter_pairs()
	for index in chapters.size():
		var chapter: Dictionary = chapters[index]
		var story: Dictionary = chapter.story
		var battle: Dictionary = chapter.battle
		chapter_order_select.add_item(_t("campaign.chapter.entry", {
			"number": index + 1,
			"story": str(story.get("name", _t("campaign.fallback.story"))).to_upper(),
			"battle": str(battle.get("name", _t("campaign.fallback.battle"))).to_upper(),
		}))
		chapter_order_select.set_item_metadata(
			chapter_order_select.item_count - 1, str(story.get("id", ""))
		)
	_select_metadata(chapter_order_select, previous_id)
	_refresh_chapter_order_actions()


func _refresh_chapter_order_actions(_index: int = 0) -> void:
	var index := chapter_order_select.selected
	chapter_earlier_button.disabled = index <= 0
	chapter_later_button.disabled = (
		index < 0 or index >= chapter_order_select.item_count - 1
	)
	_set_chapter_order_status(_t("campaign.chapter.move_help"), false)


func _move_chapter(direction: int) -> void:
	var story_id := _selected_metadata(chapter_order_select)
	if story_id.is_empty():
		return
	var moved: Dictionary = compiler.move_campaign_chapter(
		source, str(campaign.get("id", "")), story_id, direction
	)
	if not moved.ok:
		var detail := _t("campaign.error.chapter_cannot_move")
		if not moved.diagnostics.is_empty():
			detail = str(moved.diagnostics[0].get(
				"detail", moved.diagnostics[0].get("code", detail)
			))
		_set_chapter_order_status(_t("campaign.error.chapter_not_moved", {
			"detail": detail.to_upper(),
		}), true)
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = str(moved.source)
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	_refresh_chapter_order(story_id)
	_set_chapter_order_status(_t("campaign.chapter.moved", {
		"direction": _t(
			"campaign.chapter.earlier" if direction < 0 else "campaign.chapter.later"
		),
	}), false)
	chapter_order_select.grab_focus()


func _set_chapter_order_status(message: String, failure: bool) -> void:
	chapter_order_status.text = message
	chapter_order_status.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _refresh_route() -> void:
	for child in route_list.get_children():
		child.queue_free()
	first_button = null
	chapter_order_button = null
	node_buttons.clear()
	outcome_buttons.clear()
	battle_test_one_buttons.clear()
	battle_test_two_buttons.clear()
	battle_dependency_buttons.clear()
	if route.is_empty():
		var empty := _label(
			_t("campaign.route.empty"), 22, Color("ff7a83")
		)
		empty.custom_minimum_size.y = 90
		route_list.add_child(empty)
		return
	var query := "" if route_filter == null else route_filter.text.strip_edges().to_lower()
	var main_nodes: Array[Dictionary] = []
	var alternate_nodes: Array[Dictionary] = []
	for node: Dictionary in route:
		if _route_node_matches(node, query):
			main_nodes.append(node)
	for node: Dictionary in other_nodes:
		if _route_node_matches(node, query):
			alternate_nodes.append(node)
	if main_nodes.is_empty() and alternate_nodes.is_empty():
		var no_match := _label(
			_t("campaign.route.no_matches", {"query": route_filter.text.strip_edges()}),
			20,
			Color("718896")
		)
		no_match.custom_minimum_size.y = 80
		no_match.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		route_list.add_child(no_match)
		return
	route_list.add_child(_section_label(_t(
		"campaign.route.main" if query.is_empty() else "campaign.route.matching_main"
	)))
	var battle_number := 0
	for node: Dictionary in main_nodes:
		if str(node.get("kind", "")) == "encounter":
			battle_number = _route_battle_number(str(node.get("id", "")))
		_add_node_button(node, battle_number, false)
	if query.is_empty() and _chapter_pairs().size() > 1:
		chapter_order_button = _button(
			_t("campaign.chapter.order_button"), 160, 82
		)
		chapter_order_button.tooltip_text = _t("campaign.chapter.order_tooltip")
		chapter_order_button.pressed.connect(_open_chapter_order)
		route_list.add_child(chapter_order_button)
	if query.is_empty():
		var add_chapter := _button(
			_t("campaign.chapter.add_button"), 160, 92
		)
		add_chapter.tooltip_text = _t("campaign.chapter.add_tooltip")
		add_chapter.disabled = _last_route_encounter().is_empty()
		add_chapter.pressed.connect(_add_next_chapter)
		route_list.add_child(add_chapter)
	if not alternate_nodes.is_empty():
		route_list.add_child(_section_label(
			_t(
				"campaign.route.other_outcomes"
				if query.is_empty() else "campaign.route.matching_outcomes"
			)
		))
		for node: Dictionary in alternate_nodes:
			_add_node_button(node, 0, true)


func _route_node_matches(node: Dictionary, query: String) -> bool:
	if query.is_empty():
		return true
	var kind := str(node.get("kind", ""))
	var kind_name: String = str({
		"encounter": _t("campaign.kind.battle_name"),
		"story": _t("campaign.kind.story_name"),
		"terminal": _t("campaign.kind.ending_name"),
	}.get(kind, kind))
	return ("%s %s %s %s" % [
		node.get("name", ""),
		node.get("id", ""),
		kind_name,
		node.get("mapId", ""),
	]).to_lower().contains(query)


func _route_battle_number(node_id: String) -> int:
	var number := 0
	for node: Dictionary in route:
		if str(node.get("kind", "")) == "encounter":
			number += 1
		if str(node.get("id", "")) == node_id:
			return number
	return 0


func _filter_route(_value: String) -> void:
	_refresh_route()


func _add_node_button(node: Dictionary, battle_number: int, alternate: bool) -> void:
	var kind := str(node.get("kind", ""))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	route_list.add_child(row)
	var button := _button(_node_text(node, battle_number, alternate), 160)
	button.tooltip_text = _node_tooltip(node)
	match kind:
		"story":
			button.pressed.connect(_open_story_node.bind(node))
		"encounter":
			button.pressed.connect(_open_encounter_node.bind(node))
		"terminal":
			button.pressed.connect(_open_terminal_node.bind(node))
		_:
			button.disabled = true
	row.add_child(button)
	node_buttons[str(node.get("id", ""))] = button
	if kind == "encounter":
		var node_id := str(node.get("id", ""))
		var test_one := _button(_t("campaign.test.battle_one"), 150)
		test_one.tooltip_text = _t("campaign.test.battle_one_tooltip")
		test_one.pressed.connect(_play_battle.bind(node_id, 1))
		row.add_child(test_one)
		battle_test_one_buttons[node_id] = test_one
		var test_two := _button(_t("campaign.test.battle_two"), 160)
		test_two.tooltip_text = _t("campaign.test.battle_two_tooltip")
		test_two.pressed.connect(_play_battle.bind(node_id, 2))
		row.add_child(test_two)
		battle_test_two_buttons[node_id] = test_two
		_refresh_battle_test_button(node, 1, test_one)
		_refresh_battle_test_button(node, 2, test_two)
		var dependencies := _button(_t("campaign.delete.battle_button"), 180)
		dependencies.tooltip_text = _t("campaign.delete.battle_tooltip")
		dependencies.pressed.connect(
			_open_dependency_review.bind("battle", node_id, dependencies)
		)
		row.add_child(dependencies)
		battle_dependency_buttons[node_id] = dependencies
	if kind == "terminal":
		var spacer := Control.new()
		spacer.custom_minimum_size.x = 300
		row.add_child(spacer)
	else:
		var route_count: int = node.get("transitions", []).size()
		var outcomes := _button(
			_t(
				"campaign.outcome.button_one"
				if route_count == 1 else "campaign.outcome.button_many",
				{"count": route_count}
			),
			300
		)
		outcomes.size_flags_horizontal = Control.SIZE_SHRINK_END
		outcomes.tooltip_text = _t("campaign.outcome.button_tooltip")
		outcomes.pressed.connect(_open_outcomes.bind(str(node.get("id", ""))))
		row.add_child(outcomes)
		outcome_buttons[str(node.get("id", ""))] = outcomes
	if first_button == null and not button.disabled:
		first_button = button


func _node_text(node: Dictionary, battle_number: int, alternate: bool) -> String:
	var name := str(node.get(
		"name", node.get("id", _t("campaign.fallback.untitled_chapter"))
	)).to_upper()
	var kind := str(node.get("kind", ""))
	var alternate_text := _t("campaign.route.alternate") if alternate else ""
	if kind == "story":
		var scenes: Array = node.get("dialogueIds", [])
		return _t(
			"campaign.node.story_one" if scenes.size() == 1 else "campaign.node.story_many",
			{"alternate": alternate_text, "name": name, "count": scenes.size()}
		)
	if kind == "encounter":
		var prefix := _t(
			"campaign.node.battle_numbered" if battle_number > 0 else "campaign.kind.battle",
			{"number": battle_number}
		)
		return _t("campaign.node.battle", {
			"alternate": alternate_text, "prefix": prefix,
			"name": name, "summary": _encounter_summary(node),
		})
	return _t("campaign.node.ending", {"alternate": alternate_text, "name": name})


func _encounter_summary(node: Dictionary) -> String:
	var map_name := str(node.get("mapId", _t("campaign.fallback.map"))).replace("_", " ").to_upper()
	for map_value: Variant in project.get("maps", []):
		if map_value is Dictionary and str(map_value.get("id", "")) == str(node.get("mapId", "")):
			map_name = str(map_value.get("name", map_name)).to_upper()
			break
	var coop_ready := false
	var solo_allies := 0
	var coop_only_allies := 0
	var coop_only_enemies := 0
	var alternate_starts := 0
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		var side := str(placement.get("side", "first"))
		var two_only := int(placement.get("minimumPlayers", 1)) >= 2
		if side == "first" and not two_only:
			solo_allies += 1
		elif side == "first" and two_only:
			coop_only_allies += 1
		elif side == "second" and two_only:
			coop_only_enemies += 1
		if placement.has("twoPlayerPosition"):
			alternate_starts += 1
		if side == "first" and int(placement.get("player", 1)) == 2:
			coop_ready = true
	var coop_scenes := 0
	for moment_value: Variant in node.get("moments", []):
		if moment_value is Dictionary and int(moment_value.get("minimumPlayers", 1)) >= 2:
			coop_scenes += 1
	var variant := _t(
		"campaign.summary.solo_one" if solo_allies == 1 else "campaign.summary.solo_many",
		{"count": solo_allies}
	)
	if coop_only_allies > 0 or coop_only_enemies > 0:
		variant += _t("campaign.summary.coop_forces", {
			"allies": coop_only_allies,
			"ally_label": _t("campaign.summary.ally_one" if coop_only_allies == 1 else "campaign.summary.ally_many"),
			"enemies": coop_only_enemies,
			"enemy_label": _t("campaign.summary.enemy_one" if coop_only_enemies == 1 else "campaign.summary.enemy_many"),
		})
	if alternate_starts > 0:
		variant += _t(
			"campaign.summary.coop_start_one"
			if alternate_starts == 1 else "campaign.summary.coop_start_many",
			{"count": alternate_starts}
		)
	if coop_scenes > 0:
		variant += _t(
			"campaign.summary.coop_scene_one"
			if coop_scenes == 1 else "campaign.summary.coop_scene_many",
			{"count": coop_scenes}
		)
	return _t("campaign.summary.battle", {
		"map": map_name,
		"ready": _t("campaign.summary.two_ready") if coop_ready else "",
		"variant": variant,
	})


func _node_tooltip(node: Dictionary) -> String:
	match str(node.get("kind", "")):
		"story":
			return _t("campaign.node.story_tooltip")
		"encounter":
			return _t("campaign.node.battle_tooltip")
		"terminal":
			return _t("campaign.node.ending_tooltip")
	return _t("campaign.node.uneditable_tooltip")


func _open_story_node(node: Dictionary) -> void:
	if dirty and not _save():
		return
	var dialogue_ids: Array = node.get("dialogueIds", [])
	CreatorContext.choose_dialogue(str(dialogue_ids[0]) if not dialogue_ids.is_empty() else "")
	get_tree().change_scene_to_file("res://story_editor.tscn")


func _open_encounter_node(node: Dictionary) -> void:
	if dirty and not _save():
		return
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_terminal_node(node: Dictionary) -> void:
	ending_node_id = str(node.get("id", ""))
	ending_kicker.text = _t("campaign.ending.edit")
	ending_question.text = _t("campaign.ending.edit_question")
	ending_explanation.text = _t("campaign.ending.edit_help")
	ending_create_button.text = _t("campaign.ending.save")
	ending_name.text = str(node.get("name", _t("campaign.fallback.ending")))
	ending_layer.visible = true
	ending_name.grab_focus()
	ending_name.select_all()


func _last_route_encounter() -> Dictionary:
	for index in range(route.size() - 1, -1, -1):
		if str(route[index].get("kind", "")) == "encounter":
			return route[index]
	return {}


func _add_next_chapter() -> void:
	var node := _last_route_encounter()
	if node.is_empty():
		_set_status(_t("campaign.error.add_battle_before_chapter"), true)
		return
	if dirty and not _save():
		return
	CreatorContext.choose_map(str(node.get("mapId", "")), str(node.get("id", "")))
	CreatorContext.request_map_action("add_chapter")
	get_tree().change_scene_to_file("res://map_editor.tscn")


func _open_army() -> void:
	if dirty and not _save():
		return
	get_tree().change_scene_to_file("res://army_editor.tscn")


func _campaign_node(node_id: String) -> Dictionary:
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		return {}
	for node_value: Variant in flow_value.get("nodes", []):
		if node_value is Dictionary and str(node_value.get("id", "")) == node_id:
			return node_value
	return {}


func _encounter_has_player_two(node: Dictionary) -> bool:
	for placement_value: Variant in node.get("placements", []):
		if not placement_value is Dictionary:
			continue
		var placement: Dictionary = placement_value
		if (
			str(placement.get("side", "first")) == "first"
			and int(placement.get("player", 1)) == 2
			and int(placement.get("minimumPlayers", 1)) <= 2
		):
			return true
	return false


func _battle_player_count_allowed(node: Dictionary, players: int) -> bool:
	var mode := str(campaign.get("playerMode", "one"))
	if players == 1:
		return mode != "two"
	return mode != "one" and _encounter_has_player_two(node)


func _refresh_battle_test_button(
	node: Dictionary, players: int, button: Button
) -> void:
	button.disabled = not _battle_player_count_allowed(node, players)
	if players == 1:
		button.tooltip_text = _t(
			"campaign.test.solo_unavailable_tooltip"
			if button.disabled else "campaign.test.solo_tooltip"
		)
	else:
		button.tooltip_text = _t(
			"campaign.test.coop_unavailable_tooltip"
			if button.disabled else "campaign.test.coop_tooltip"
		)


func _refresh_battle_test_buttons() -> void:
	for node_id: Variant in battle_test_one_buttons:
		var node := _campaign_node(str(node_id))
		if not node.is_empty():
			_refresh_battle_test_button(node, 1, battle_test_one_buttons[node_id])
	for node_id: Variant in battle_test_two_buttons:
		var node := _campaign_node(str(node_id))
		if not node.is_empty():
			_refresh_battle_test_button(node, 2, battle_test_two_buttons[node_id])


func _battle_preview_source(node_id: String) -> Dictionary:
	var candidate: Dictionary = project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", [])
	var campaign_id := str(campaign.get("id", ""))
	for campaign_index in campaigns.size():
		if not campaigns[campaign_index] is Dictionary:
			continue
		var preview_campaign: Dictionary = campaigns[campaign_index]
		if str(preview_campaign.get("id", "")) != campaign_id:
			continue
		var flow_value: Variant = preview_campaign.get("flow", {})
		if not flow_value is Dictionary:
			return {"ok": false, "message": _t("campaign.error.flow_missing")}
		var flow: Dictionary = flow_value
		var nodes: Array = flow.get("nodes", [])
		var encounter_index := -1
		for index in nodes.size():
			if (
				nodes[index] is Dictionary
				and str(nodes[index].get("id", "")) == node_id
				and str(nodes[index].get("kind", "")) == "encounter"
			):
				encounter_index = index
				break
		if encounter_index < 0:
			return {"ok": false, "message": _t("campaign.error.battle_missing")}

		# A direct test is a clean-room campaign run. Make every authored recruit
		# available so a later battle can field its real roster without replaying
		# prior chapters; none of this disposable source is written to the project.
		var roster: Array = preview_campaign.get("roster", [])
		for member_index in roster.size():
			if roster[member_index] is Dictionary:
				var member: Dictionary = roster[member_index]
				member.erase("joinNodeId")
				roster[member_index] = member
		preview_campaign["roster"] = roster

		var terminal_id := "editor_battle_test_complete"
		var suffix := 1
		while _node_array_has_id(nodes, terminal_id):
			terminal_id = "editor_battle_test_complete_%d" % suffix
			suffix += 1
		var encounter: Dictionary = nodes[encounter_index]
		encounter["transitions"] = [{
			"id": "return_to_editor",
			"targetNodeId": terminal_id,
			"priority": 0,
		}]
		nodes[encounter_index] = encounter
		nodes.append({
			"id": terminal_id,
			"name": _t("campaign.test.return_to_editor"),
			"kind": "terminal",
			"transitions": [],
		})
		flow["entryNodeId"] = node_id
		flow["nodes"] = nodes
		preview_campaign["flow"] = flow
		campaigns[campaign_index] = preview_campaign
		candidate["campaigns"] = campaigns
		return {
			"ok": true,
			"source": JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n",
			"terminal_id": terminal_id,
		}
	return {"ok": false, "message": _t("campaign.error.campaign_missing")}


func _node_array_has_id(nodes: Array, node_id: String) -> bool:
	for node_value: Variant in nodes:
		if node_value is Dictionary and str(node_value.get("id", "")) == node_id:
			return true
	return false


func _play_battle(node_id: String, players: int) -> void:
	if compiler == null:
		_set_status(_t("campaign.error.compiler_unavailable"), true)
		return
	var node := _campaign_node(node_id)
	if node.is_empty():
		_set_status(_t("campaign.error.battle_missing"), true)
		return
	if not _battle_player_count_allowed(node, players):
		_set_status(_t("campaign.error.battle_player_count"), true)
		return
	if dirty and not _save():
		return
	var preview := _battle_preview_source(node_id)
	if not bool(preview.get("ok", false)):
		_set_status(str(preview.get("message", _t("campaign.error.battle_test_unavailable"))), true)
		return
	var compiled: Dictionary = compiler.compile_source_json(str(preview.source))
	if not compiled.ok:
		_set_status(_t("campaign.error.not_playable", {"detail": _diagnostic_summary(compiled)}), true)
		return
	var campaign_id := str(campaign.get("id", ""))
	if campaign_id.is_empty():
		_set_status(_t("campaign.error.campaign_id"), true)
		return
	CreatorContext.choose_map(str(node.get("mapId", "")), node_id)
	LaunchContext.stage_package(
		compiled.package,
		campaign_id,
		true,
		players,
		"res://campaign_editor.tscn",
		str(node.get("name", _t("campaign.fallback.battle")))
	)
	get_tree().change_scene_to_file("res://main.tscn")


func _play_campaign(players: int) -> void:
	if compiler == null:
		_set_status(_t("campaign.error.compiler_unavailable"), true)
		return
	var mode := str(campaign.get("playerMode", "one"))
	if (mode == "one" and players != 1) or (mode == "two" and players != 2):
		_set_status(_t("campaign.error.campaign_player_count"), true)
		return
	if players == 2:
		var gaps := _two_player_ownership_gaps()
		if not gaps.is_empty():
			_set_status(_t("campaign.error.assign_player_two", {
				"battles": ", ".join(gaps).to_upper()
			}), true)
			return
	if dirty and not _save():
		return
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.ok:
		_set_status(_t("campaign.error.open", {"message": loaded.message}), true)
		return
	var compiled: Dictionary = compiler.compile_source_json(loaded.source)
	if not compiled.ok:
		var diagnostics: Array = compiled.get("diagnostics", [])
		_set_status(_t("campaign.error.not_playable", {"detail": _diagnostic_summary(compiled)}), true)
		if not diagnostics.is_empty() and diagnostics[0] is Dictionary:
			pending_diagnostic = diagnostics[0].duplicate(true)
			fix_button.text = _diagnostic_fix_label(pending_diagnostic)
			fix_button.visible = true
			fix_button.call_deferred("grab_focus")
		return
	var campaign_id := str(campaign.get("id", ""))
	if campaign_id.is_empty():
		_set_status(_t("campaign.error.campaign_id"), true)
		return
	LaunchContext.stage_package(
		compiled.package,
		campaign_id,
		true,
		players,
		"res://campaign_editor.tscn",
		_t("campaign.test.whole_campaign")
	)
	get_tree().change_scene_to_file("res://main.tscn")


func _change_player_mode(index: int) -> void:
	if player_mode_select == null or index < 0 or index >= player_mode_select.item_count:
		return
	var mode := str(player_mode_select.get_item_metadata(index))
	if mode not in ["one", "two", "choice"] or mode == str(campaign.get("playerMode", "one")):
		return
	var candidate := project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	var campaign_id := str(campaign.get("id", ""))
	var campaign_index := -1
	for position in campaigns.size():
		if campaigns[position] is Dictionary and str(campaigns[position].get("id", "")) == campaign_id:
			campaign_index = position
			break
	if campaign_index < 0:
		_set_status(_t("campaign.error.players_need_campaign"), true)
		return
	var changed: Dictionary = campaigns[campaign_index].duplicate(true)
	changed["playerMode"] = mode
	campaigns[campaign_index] = changed
	candidate["campaigns"] = campaigns
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		_set_status(_t("campaign.error.player_choice", {"detail": _diagnostic_summary(checked)}), true)
		_refresh_player_mode()
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_update_actions()
	_refresh_player_mode()
	_set_status(_t("campaign.status.unsaved", {"change": _player_mode_label(mode)}), false)


func _refresh_player_mode() -> void:
	if player_mode_select == null:
		return
	var mode := str(campaign.get("playerMode", "one"))
	_select_metadata(player_mode_select, mode)
	var gaps := _two_player_ownership_gaps()
	match mode:
		"two":
			player_mode_help.text = _t("campaign.players.two_help", {
				"guidance": _two_player_guidance(gaps)
			})
		"choice":
			player_mode_help.text = _t("campaign.players.choice_help", {
				"guidance": _two_player_guidance(gaps)
			})
		_:
			player_mode_help.text = _t("campaign.players.one_help")
	if test_one_button != null:
		test_one_button.disabled = mode == "two"
	if test_two_button != null:
		test_two_button.disabled = mode == "one" or not gaps.is_empty()
	_refresh_battle_test_buttons()


func _two_player_guidance(gaps: Array[String]) -> String:
	if gaps.is_empty():
		var guidance := _t("campaign.players.coop_ready")
		var sameness := _two_player_variant_gaps()
		if not sameness.is_empty():
			guidance += _t("campaign.players.coop_distinct", {"battles": ", ".join(sameness)})
		return guidance
	return _t("campaign.players.assign_two", {"battles": ", ".join(gaps)})


func _two_player_variant_gaps() -> Array[String]:
	var gaps: Array[String] = []
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		return gaps
	for node_value: Variant in flow_value.get("nodes", []):
		if not node_value is Dictionary or str(node_value.get("kind", "")) != "encounter":
			continue
		var distinct := false
		for placement_value: Variant in node_value.get("placements", []):
			if not placement_value is Dictionary:
				continue
			if int(placement_value.get("minimumPlayers", 1)) >= 2 or placement_value.has("twoPlayerPosition"):
				distinct = true
				break
		if not distinct:
			for moment_value: Variant in node_value.get("moments", []):
				if moment_value is Dictionary and int(moment_value.get("minimumPlayers", 1)) >= 2:
					distinct = true
					break
		if not distinct:
			gaps.append(str(node_value.get("name", node_value.get("id", _t("campaign.fallback.unnamed_battle")))))
	return gaps


func _two_player_ownership_gaps() -> Array[String]:
	return CampaignReadiness.two_player_ownership_gaps(campaign)


func _player_mode_label(mode: String) -> String:
	return _t(str({
		"one": "campaign.players.mode_one_status",
		"two": "campaign.players.mode_two_status",
		"choice": "campaign.players.mode_choice_status",
	}.get(mode, "campaign.players.mode_one_status")))


func _diagnostic_summary(result: Dictionary) -> String:
	var diagnostics: Array = result.get("diagnostics", [])
	if diagnostics.is_empty() or not diagnostics[0] is Dictionary:
		return _t("campaign.error.project_incomplete")
	var diagnostic: Dictionary = diagnostics[0]
	var screen := str(diagnostic.get("screen", "project"))
	var destinations := {
		"map": "campaign.diagnostic.destination.map",
		"story": "campaign.diagnostic.destination.story",
		"army": "campaign.diagnostic.destination.army",
		"gear": "campaign.diagnostic.destination.gear",
		"campaign": "campaign.diagnostic.destination.campaign",
		"project": "campaign.diagnostic.destination.project",
	}
	var parts: Array[String] = [_t(str(destinations.get(screen, "campaign.diagnostic.destination.project")))]
	var subject := str(diagnostic.get("subject", "")).strip_edges()
	if not subject.is_empty():
		parts.append(subject.to_upper())
	var object_name := str(diagnostic.get("object", "")).strip_edges()
	if not object_name.is_empty() and object_name != subject:
		parts.append(object_name.to_upper())
	if diagnostic.has("x") and diagnostic.has("y"):
		parts.append(_t("campaign.diagnostic.cell", {"x": int(diagnostic.x), "y": int(diagnostic.y)}))
	var field := _diagnostic_field_label(str(diagnostic.get("field", "")))
	if not field.is_empty():
		parts.append(field)
	var detail := str(diagnostic.get("detail", "")).strip_edges()
	if detail.is_empty():
		detail = str(diagnostic.get("code", "project_incomplete")).replace("_", " ")
	parts.append(detail.to_upper())
	return "  /  ".join(parts)


func _diagnostic_field_label(field: String) -> String:
	var normalized := field.replace("_", "").to_lower()
	var key := str({
		"id": "identity", "name": "name", "classid": "role", "basestats": "base_stats",
		"health": "health", "strength": "strength", "defense": "defense",
		"resistance": "resistance", "movement": "movement", "actionpoints": "actions",
		"skill": "skill", "luck": "luck", "evasion": "evasion", "magic": "magic",
		"speed": "speed", "range": "range", "accuracy": "accuracy", "power": "power",
		"terrain": "terrain", "surroundings": "outside_terrain",
		"placements": "starting_position", "minimumplayers": "two_player_scaling",
		"localplayer": "player_ownership", "deployment": "deployment",
		"tiles": "deployment_cells", "objectiveids": "goals",
		"targetplacementid": "goal_target", "lines": "conversation", "speaker": "speaker",
		"text": "dialogue", "backgroundid": "backdrop", "roster": "characters",
		"transitions": "outcomes", "targetid": "destination", "priority": "outcome_order",
		"quantity": "amount",
	}.get(normalized, ""))
	return _t("campaign.diagnostic.field." + key) if not key.is_empty() else field.replace("_", " ").to_upper()


func _diagnostic_fix_label(diagnostic: Dictionary) -> String:
	return _t(str({
		"map": "campaign.diagnostic.fix.map", "story": "campaign.diagnostic.fix.story",
		"army": "campaign.diagnostic.fix.army", "gear": "campaign.diagnostic.fix.gear",
		"campaign": "campaign.diagnostic.fix.campaign", "project": "campaign.diagnostic.fix.project",
	}.get(str(diagnostic.get("screen", "project")), "campaign.diagnostic.fix.issue")))


func _stable_source_matches(source_key: String, stable_id: String) -> bool:
	return not source_key.is_empty() and not stable_id.is_empty() and str(
		compiler.stable_source_id(source_key)
	) == stable_id


func _map_target_for_diagnostic(diagnostic: Dictionary) -> Dictionary:
	var owner_id := str(diagnostic.get("owner_id", ""))
	var category := str(diagnostic.get("category", ""))
	if category == "maps":
		for map_value: Variant in project.get("maps", []):
			if map_value is Dictionary and _stable_source_matches(
				str(map_value.get("id", "")), owner_id
			):
				return {"map_id": str(map_value.id), "node_id": ""}
	if category == "objectives":
		var objective_id := ""
		for objective_value: Variant in project.get("objectives", []):
			if objective_value is Dictionary and _stable_source_matches(
				str(objective_value.get("id", "")), owner_id
			):
				objective_id = str(objective_value.id)
				break
		if not objective_id.is_empty():
			for campaign_value: Variant in project.get("campaigns", []):
				if not campaign_value is Dictionary:
					continue
				var flow: Variant = campaign_value.get("flow", {})
				if not flow is Dictionary:
					continue
				for node_value: Variant in flow.get("nodes", []):
					if node_value is Dictionary and node_value.get("objectiveIds", []).has(objective_id):
						return {"map_id": str(node_value.get("mapId", "")), "node_id": str(node_value.get("id", ""))}
	for campaign_value: Variant in project.get("campaigns", []):
		if not campaign_value is Dictionary:
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary or str(node_value.get("kind", "")) != "encounter":
				continue
			var composite := "%s/%s" % [campaign_value.get("id", ""), node_value.get("id", "")]
			if _stable_source_matches(composite, owner_id):
				return {"map_id": str(node_value.get("mapId", "")), "node_id": str(node_value.get("id", ""))}
	var fallback := _last_route_encounter()
	return {
		"map_id": str(fallback.get("mapId", "")),
		"node_id": str(fallback.get("id", "")),
	}


func _dialogue_target_for_diagnostic(diagnostic: Dictionary) -> String:
	var owner_id := str(diagnostic.get("owner_id", ""))
	for value: Variant in project.get("dialogues", []):
		if value is Dictionary and _stable_source_matches(str(value.get("id", "")), owner_id):
			return str(value.get("id", ""))
	return ""


func _fix_diagnostic() -> void:
	if pending_diagnostic.is_empty():
		return
	var diagnostic := pending_diagnostic.duplicate(true)
	match str(diagnostic.get("screen", "project")):
		"map":
			var target := _map_target_for_diagnostic(diagnostic)
			CreatorContext.choose_map(str(target.map_id), str(target.node_id))
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://map_editor.tscn")
		"story":
			CreatorContext.choose_dialogue(_dialogue_target_for_diagnostic(diagnostic))
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://story_editor.tscn")
		"army":
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://army_editor.tscn")
		"gear":
			CreatorContext.set_diagnostic_focus(diagnostic)
			get_tree().change_scene_to_file("res://gear_editor.tscn")
		"campaign":
			fix_button.visible = false
			pending_diagnostic = {}
			if first_button != null:
				first_button.grab_focus()
		"project", _:
			_open_projects()


func _open_projects() -> void:
	if dirty and not _save():
		return
	CreatorContext.clear()
	get_tree().change_scene_to_file("res://creator.tscn")


func _save() -> bool:
	var saved: Dictionary = library.save_source(project_id, source)
	if not saved.ok:
		_set_status(_t("campaign.error.save", {"message": saved.message}), true)
		return false
	saved_source = source
	dirty = false
	_update_actions()
	_set_status(_t("campaign.status.saved"), false)
	return true


func _undo() -> void:
	if undo_stack.is_empty():
		return
	redo_stack.append(source)
	source = undo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_collect_route()
	_refresh_route()
	_update_actions()
	_set_status(_t("campaign.status.undo"), false)


func _redo() -> void:
	if redo_stack.is_empty():
		return
	undo_stack.append(source)
	source = redo_stack.pop_back()
	_read_source()
	dirty = source != saved_source
	_collect_route()
	_refresh_route()
	_update_actions()
	_set_status(_t("campaign.status.redo"), false)


func _read_source() -> bool:
	var parsed: Variant = JSON.parse_string(source)
	if not parsed is Dictionary:
		_set_status(_t("campaign.error.source_unreadable"), true)
		return false
	project = parsed
	var selected_id := str(campaign.get("id", CreatorContext.campaign_id))
	var campaigns: Array = project.get("campaigns", [])
	if campaigns.is_empty() or not campaigns[0] is Dictionary:
		_set_status(_t("campaign.error.add_campaign_first"), true)
		return false
	_refresh_campaign_choices(selected_id)
	if not _set_campaign_from_selection():
		_set_status(_t("campaign.error.choose_campaign"), true)
		return false
	_refresh_player_mode()
	_refresh_target_budget()
	return true


func _refresh_target_budget() -> void:
	if compiler == null or target_budget_label == null:
		return
	var report: Dictionary = compiler.target_budget_report(source)
	if not bool(report.get("ok", false)):
		target_budget_label.text = _t("campaign.targets.unavailable")
		target_budget_label.add_theme_color_override("font_color", Color("ff7a83"))
		return
	var targets: Dictionary = {}
	for value: Variant in report.get("targets", []):
		if value is Dictionary:
			targets[str(value.get("id", ""))] = value
	var n64: Dictionary = targets.get("nintendo64", {})
	var ps1: Dictionary = targets.get("playstation", {})
	var n64_units := int(n64.get("maximum_units", 0))
	var ps1_units := int(ps1.get("maximum_units", 0))
	var over := not bool(n64.get("fits", true)) or not bool(ps1.get("fits", true))
	target_budget_label.text = _t("campaign.targets.summary", {
		"n64_max": n64_units, "n64_units": int(n64.get("units_per_encounter", 0)),
		"ps1_max": ps1_units, "ps1_units": int(ps1.get("units_per_encounter", 0)),
	})
	if over:
		var stage := str(n64.get(
			"worst_stage", ps1.get("worst_stage", _t("campaign.fallback.battle"))
		))
		target_budget_label.text += _t("campaign.targets.over", {"stage": stage.to_upper()})
	target_budget_label.add_theme_color_override(
		"font_color", Color("ffca63") if over else Color("a9bdc9")
	)
	target_budget_label.tooltip_text = _t("campaign.targets.tooltip")


func _update_actions() -> void:
	save_button.disabled = not dirty
	undo_button.disabled = undo_stack.is_empty()
	redo_button.disabled = redo_stack.is_empty()


func _open_outcomes(node_id: String) -> void:
	outcome_node_id = node_id
	_refresh_outcome_layer()
	outcome_layer.visible = true
	outcome_select.grab_focus()


func _close_outcomes() -> void:
	outcome_layer.visible = false
	if outcome_buttons.has(outcome_node_id):
		var button: Button = outcome_buttons[outcome_node_id]
		button.grab_focus()


func _current_outcome_node() -> Dictionary:
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return {}
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("id", "")) == outcome_node_id:
			return value
	return {}


func _unit_type_name(unit_type_id: String) -> String:
	for value: Variant in project.get("unitTypes", []):
		if value is Dictionary and str(value.get("id", "")) == unit_type_id:
			return str(value.get("name", _t("campaign.fallback.character")))
	return _t("campaign.fallback.character")


func _talk_placement_name(placement: Dictionary) -> String:
	var authored_name := str(placement.get("name", "")).strip_edges()
	if not authored_name.is_empty():
		return authored_name
	var member_id := str(placement.get("memberId", ""))
	for value: Variant in campaign.get("roster", []):
		if value is Dictionary and str(value.get("id", "")) == member_id:
			return str(value.get("name", _t("campaign.fallback.character")))
	return _unit_type_name(str(placement.get("unitTypeId", "")))


func _talk_choices() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var seen: Dictionary = {}
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return result
	for node_value: Variant in flow.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			var talk: Variant = placement.get("talk", {})
			if not talk is Dictionary:
				continue
			var flag_id := str(talk.get("flagId", ""))
			if flag_id.is_empty() or seen.has(flag_id):
				continue
			seen[flag_id] = true
			result.append({
				"id": flag_id,
				"name": _talk_placement_name(placement),
				"battle": str(node.get("name", _t("campaign.fallback.battle"))),
			})
	return result


func _is_talk_flag(flag_id: String) -> bool:
	for choice: Dictionary in _talk_choices():
		if str(choice.id) == flag_id:
			return true
	return false


func _editable_predicate_kind(predicate: Dictionary) -> String:
	match str(predicate.get("kind", "")):
		"objectiveResult":
			return "defeat" if str(predicate.get("result", "victory")) == "defeat" else "victory"
		"inventoryAtLeast":
			return "item"
		"worldFlagEquals":
			var value: Variant = predicate.get("value", true)
			if value is bool and _is_talk_flag(str(predicate.get("flagId", ""))):
				return "talked" if value else "not_talked"
	return "advanced"


func _editable_compound(when: Dictionary) -> bool:
	var kind := str(when.get("kind", ""))
	var values: Array = []
	if kind in ["all", "any"]:
		values = when.get("conditions", [])
	elif kind == "not" and when.get("condition", {}) is Dictionary:
		values = [when.condition]
	else:
		return false
	if values.is_empty():
		return false
	for value: Variant in values:
		if not value is Dictionary or _editable_predicate_kind(value) == "advanced":
			return false
	return true


func _predicate_text(predicate: Dictionary) -> String:
	match _editable_predicate_kind(predicate):
		"victory":
			return _t("campaign.condition.goal_won_named", {"name": _objective_name(str(predicate.get("objectiveId", "")))})
		"defeat":
			return _t("campaign.condition.goal_lost_named", {"name": _objective_name(str(predicate.get("objectiveId", "")))})
		"item":
			return _t("campaign.condition.have_item_named", {
				"count": maxi(1, int(predicate.get("quantity", 1))),
				"name": _item_name(str(predicate.get("itemId", ""))),
			})
		"talked":
			return _t("campaign.condition.talked_named", {"name": _talk_choice_name(str(predicate.get("flagId", "")))})
		"not_talked":
			return _t("campaign.condition.not_talked_named", {"name": _talk_choice_name(str(predicate.get("flagId", "")))})
	return _t("campaign.condition.advanced_requirement")


func _objective_name(objective_id: String) -> String:
	for value: Variant in project.get("objectives", []):
		if value is Dictionary and str(value.get("id", "")) == objective_id:
			return str(value.get("name", _t("campaign.fallback.goal"))).to_upper()
	return _t("campaign.fallback.goal").to_upper()


func _item_name(item_id: String) -> String:
	for value: Variant in project.get("items", []):
		if value is Dictionary and str(value.get("id", "")) == item_id:
			return str(value.get("name", _t("campaign.fallback.item"))).to_upper()
	return _t("campaign.fallback.item").to_upper()


func _talk_choice_name(flag_id: String) -> String:
	for choice: Dictionary in _talk_choices():
		if str(choice.id) == flag_id:
			return str(choice.name).to_upper()
	return _t("campaign.fallback.character").to_upper()


func _refresh_outcome_layer(preferred_id := "") -> void:
	outcome_refreshing = true
	var node := _current_outcome_node()
	outcome_title.text = _t("campaign.outcome.title", {"chapter": str(
		node.get("name", node.get("id", _t("campaign.fallback.chapter")))
	).to_upper()})
	var previous_id := preferred_id
	if previous_id.is_empty() and outcome_select.selected >= 0:
		previous_id = str(outcome_select.get_item_metadata(outcome_select.selected))
	outcome_select.clear()
	var transitions: Array = node.get("transitions", [])
	for index in transitions.size():
		if not transitions[index] is Dictionary:
			continue
		var transition: Dictionary = transitions[index]
		outcome_select.add_item(_outcome_text(transition, index))
		outcome_select.set_item_metadata(
			outcome_select.item_count - 1, str(transition.get("id", ""))
		)
	if outcome_select.item_count == 0:
		outcome_select.add_item(_t("campaign.outcome.none"))
		outcome_select.set_item_metadata(0, "")
	outcome_select.disabled = transitions.is_empty()
	_select_metadata(outcome_select, previous_id)

	outcome_objective.clear()
	for value: Variant in project.get("objectives", []):
		if value is Dictionary:
			outcome_objective.add_item(str(value.get("name", value.get("id", _t("campaign.fallback.goal")))))
			outcome_objective.set_item_metadata(
				outcome_objective.item_count - 1, str(value.get("id", ""))
			)
	outcome_item.clear()
	for value: Variant in project.get("items", []):
		if value is Dictionary:
			outcome_item.add_item(str(value.get("name", value.get("id", _t("campaign.fallback.item")))))
			outcome_item.set_item_metadata(
				outcome_item.item_count - 1, str(value.get("id", ""))
			)
	outcome_flag.clear()
	for choice: Dictionary in _talk_choices():
		outcome_flag.add_item(_t("campaign.outcome.talk_choice", {
			"character": str(choice.name).to_upper(),
			"battle": str(choice.battle).to_upper(),
		}))
		outcome_flag.set_item_metadata(outcome_flag.item_count - 1, choice.id)
	outcome_target.clear()
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if not value is Dictionary:
				continue
			outcome_target.add_item(_t("campaign.outcome.target", {
				"kind": _node_kind_label(str(value.get("kind", ""))),
				"name": str(value.get("name", value.get("id", _t("campaign.fallback.chapter")))),
			}))
			outcome_target.set_item_metadata(
				outcome_target.item_count - 1, str(value.get("id", ""))
			)
	outcome_refreshing = false
	_refresh_selected_outcome()


func _refresh_selected_outcome(_index: int = 0) -> void:
	if outcome_refreshing:
		return
	outcome_refreshing = true
	var transition := _selected_outcome()
	var condition_kind := "always"
	var objective_id := ""
	var item_id := ""
	var flag_id := ""
	var quantity := 1
	var target_id := _default_outcome_target()
	if not transition.is_empty():
		condition_kind = _outcome_condition_kind(transition)
		target_id = str(transition.get("targetNodeId", target_id))
		var when: Variant = transition.get("when", {})
		if when is Dictionary:
			objective_id = str(when.get("objectiveId", ""))
			item_id = str(when.get("itemId", ""))
			flag_id = str(when.get("flagId", ""))
			quantity = int(when.get("quantity", 1))
	outcome_condition.clear()
	var choices: Array[Dictionary] = [{"id": "always", "label": _t("campaign.condition.always")}]
	if outcome_objective.item_count > 0:
		choices.append({"id": "victory", "label": _t("campaign.condition.goal_won")})
		choices.append({"id": "defeat", "label": _t("campaign.condition.goal_lost")})
	if outcome_item.item_count > 0:
		choices.append({"id": "item", "label": _t("campaign.condition.have_item")})
	if outcome_flag.item_count > 0:
		choices.append({"id": "talked", "label": _t("campaign.condition.talked")})
		choices.append({"id": "not_talked", "label": _t("campaign.condition.not_talked")})
	if outcome_objective.item_count > 0 or outcome_item.item_count > 0 or outcome_flag.item_count > 0:
		choices.append({"id": "compound", "label": _t("campaign.condition.multiple")})
	for choice: Dictionary in choices:
		outcome_condition.add_item(str(choice.label))
		outcome_condition.set_item_metadata(outcome_condition.item_count - 1, choice.id)
	if condition_kind == "advanced":
		outcome_condition.add_item(_t("campaign.condition.advanced_keep"))
		outcome_condition.set_item_metadata(outcome_condition.item_count - 1, "advanced")
	_select_metadata(outcome_condition, condition_kind)
	_select_metadata(outcome_objective, objective_id)
	_select_metadata(outcome_item, item_id)
	_select_metadata(outcome_flag, flag_id)
	_select_metadata(outcome_target, target_id)
	outcome_quantity_label.text = str(maxi(1, quantity))
	var has_transition := not transition.is_empty()
	outcome_apply_button.disabled = not has_transition
	outcome_remove_button.disabled = not has_transition or _outcome_transitions().size() <= 1
	var transition_index := _selected_outcome_index()
	outcome_earlier_button.disabled = transition_index <= 0
	outcome_later_button.disabled = transition_index < 0 or transition_index >= _outcome_transitions().size() - 1
	outcome_refreshing = false
	_refresh_outcome_fields()
	_set_outcome_status(_t(
		"campaign.outcome.status.choose" if has_transition else "campaign.outcome.status.no_route"
	), false)


func _refresh_outcome_fields(_index: int = 0) -> void:
	if outcome_refreshing:
		return
	var kind := _selected_metadata(outcome_condition)
	outcome_detail_row.visible = kind in ["victory", "defeat", "item", "talked", "not_talked"]
	outcome_objective_column.visible = kind in ["victory", "defeat"]
	outcome_item_column.visible = kind == "item"
	outcome_flag_column.visible = kind in ["talked", "not_talked"]
	outcome_quantity_row.visible = kind == "item"
	outcome_compound_row.visible = kind == "compound"
	outcome_objective.disabled = kind not in ["victory", "defeat"] or outcome_objective.item_count == 0
	outcome_item.disabled = kind != "item" or outcome_item.item_count == 0
	outcome_flag.disabled = kind not in ["talked", "not_talked"] or outcome_flag.item_count == 0
	for button: Button in outcome_quantity_buttons:
		button.disabled = kind != "item"
	outcome_quantity_label.add_theme_color_override(
		"font_color", Color("f2f7f9") if kind == "item" else Color("718896")
	)
	if kind == "compound":
		var when: Variant = _selected_outcome().get("when", {})
		outcome_compound_summary.text = (
			_compound_summary(when) if when is Dictionary else _t("campaign.compound.add_requirements")
		)


func _selected_outcome() -> Dictionary:
	var selected_id := _selected_metadata(outcome_select)
	for value: Variant in _outcome_transitions():
		if value is Dictionary and str(value.get("id", "")) == selected_id:
			return value
	return {}


func _selected_outcome_index() -> int:
	var selected_id := _selected_metadata(outcome_select)
	var transitions := _outcome_transitions()
	for index in transitions.size():
		if transitions[index] is Dictionary and str(transitions[index].get("id", "")) == selected_id:
			return index
	return -1


func _outcome_transitions() -> Array:
	return _current_outcome_node().get("transitions", [])


func _outcome_condition_kind(transition: Dictionary) -> String:
	var when: Variant = transition.get("when", null)
	if not when is Dictionary:
		return "always"
	match str(when.get("kind", "")):
		"objectiveResult":
			return "defeat" if str(when.get("result", "victory")) == "defeat" else "victory"
		"inventoryAtLeast":
			return "item"
		"worldFlagEquals":
			return _editable_predicate_kind(when)
		"all", "any", "not":
			return "compound" if _editable_compound(when) else "advanced"
		_:
			return "advanced"


func _outcome_text(transition: Dictionary, index: int) -> String:
	var kind := _outcome_condition_kind(transition)
	var condition := _t("campaign.condition.always")
	match kind:
		"victory":
			condition = _t("campaign.condition.goal_won")
		"defeat":
			condition = _t("campaign.condition.goal_lost")
		"item":
			condition = _t("campaign.condition.have_item_short")
		"talked":
			condition = _t("campaign.condition.talked_short")
		"not_talked":
			condition = _t("campaign.condition.not_talked_short")
		"compound":
			condition = _compound_summary(transition.get("when", {}))
		"advanced":
			condition = _t("campaign.condition.advanced")
	return _t("campaign.outcome.entry", {
		"number": index + 1, "condition": condition,
		"target": _node_name(str(transition.get("targetNodeId", ""))),
	})


func _node_name(node_id: String) -> String:
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if value is Dictionary and str(value.get("id", "")) == node_id:
				return str(value.get("name", value.get("id", _t("campaign.fallback.chapter")))).to_upper()
	return _t("campaign.outcome.missing_destination")


func _node_kind_label(kind: String) -> String:
	return _t(str({
		"story": "campaign.kind.story",
		"encounter": "campaign.kind.battle",
		"terminal": "campaign.kind.ending",
	}.get(kind, "campaign.kind.chapter")))


func _default_outcome_target() -> String:
	var flow: Variant = campaign.get("flow", {})
	if not flow is Dictionary:
		return ""
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("kind", "")) == "terminal":
			return str(value.get("id", ""))
	for value: Variant in flow.get("nodes", []):
		if value is Dictionary and str(value.get("id", "")) != outcome_node_id:
			return str(value.get("id", ""))
	return outcome_node_id


func _selected_metadata(option: OptionButton) -> String:
	if option == null or option.selected < 0:
		return ""
	return str(option.get_item_metadata(option.selected))


func _select_metadata(option: OptionButton, value: String) -> void:
	for index in option.item_count:
		if str(option.get_item_metadata(index)) == value:
			option.select(index)
			return
	if option.item_count > 0:
		option.select(0)


func _adjust_outcome_quantity(amount: int) -> void:
	var value := clampi(int(outcome_quantity_label.text) + amount, 1, 65535)
	outcome_quantity_label.text = str(value)


func _open_add_route() -> void:
	var choice := _branch_choice()
	add_branch_button.disabled = choice.is_empty()
	add_branch_button.text = (
		_t("campaign.add_route.branch_available")
		if not choice.is_empty()
		else _t("campaign.add_route.branch_unavailable")
	)
	add_route_layer.visible = true
	add_existing_button.grab_focus()


func _close_add_route() -> void:
	add_route_layer.visible = false
	add_route_button.grab_focus()


func _choose_existing_route() -> void:
	add_route_layer.visible = false
	_add_outcome()


func _choose_new_ending() -> void:
	add_route_layer.visible = false
	_open_new_ending()


func _choose_new_branch() -> void:
	if _branch_choice().is_empty():
		return
	add_route_layer.visible = false
	_open_new_branch()


func _branch_choice() -> Dictionary:
	var node := _current_outcome_node()
	if str(node.get("kind", "")) != "encounter":
		return {}
	var objective_ids: Array = node.get("objectiveIds", [])
	if objective_ids.is_empty():
		return {}
	var objective_id := str(objective_ids[0])
	var has_victory := false
	var has_defeat := false
	var unconditional_count := 0
	var transitions: Array = node.get("transitions", [])
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		var transition: Dictionary = value
		var when: Variant = transition.get("when", null)
		if not when is Dictionary:
			unconditional_count += 1
			continue
		if (
			str(when.get("kind", "")) != "objectiveResult"
			or str(when.get("objectiveId", "")) != objective_id
		):
			continue
		has_victory = has_victory or str(when.get("result", "victory")) == "victory"
		has_defeat = has_defeat or str(when.get("result", "victory")) == "defeat"
	if has_victory and has_defeat:
		return {}
	if unconditional_count > 0 and transitions.size() != 1:
		return {}
	var selected := _selected_outcome()
	var target_id := str(selected.get("targetNodeId", _default_outcome_target()))
	if target_id.is_empty():
		return {}
	return {
		"objective_id": objective_id,
		"result": "victory" if has_defeat and not has_victory else "defeat",
		"target_id": target_id,
	}


func _next_chapter_sequence() -> int:
	var used: Array[String] = []
	for value: Variant in project.get("maps", []):
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var flow: Variant = campaign.get("flow", {})
	if flow is Dictionary:
		for value: Variant in flow.get("nodes", []):
			if value is Dictionary:
				used.append(str(value.get("id", "")))
	var sequence := 1
	while (
		used.has("chapter_%d_map" % sequence)
		or used.has("chapter_%d_story" % sequence)
		or used.has("chapter_%d_battle" % sequence)
	):
		sequence += 1
	return sequence


func _branch_fill_terrain() -> String:
	var node := _current_outcome_node()
	var map_id := str(node.get("mapId", ""))
	for value: Variant in project.get("maps", []):
		if not value is Dictionary or str(value.get("id", "")) != map_id:
			continue
		var counts: Dictionary = {}
		var best := ""
		var best_count := 0
		for terrain_value: Variant in value.get("terrain", []):
			var terrain_id := str(terrain_value)
			counts[terrain_id] = int(counts.get(terrain_id, 0)) + 1
			if int(counts[terrain_id]) > best_count:
				best = terrain_id
				best_count = int(counts[terrain_id])
		if not best.is_empty():
			return best
	return "grass"


func _open_new_branch() -> void:
	var sequence := _next_chapter_sequence()
	branch_name.text = "Alternate Chapter %d" % sequence
	branch_layer.visible = true
	branch_name.grab_focus()
	branch_name.select_all()


func _close_new_branch() -> void:
	branch_layer.visible = false
	add_route_button.grab_focus()


func _create_branch_from_text(_value: String) -> void:
	_create_branch()


func _create_branch() -> void:
	var name := branch_name.text.strip_edges()
	if name.is_empty():
		_set_outcome_status(_t("campaign.error.branch_name"), true)
		branch_name.grab_focus()
		return
	var choice := _branch_choice()
	if choice.is_empty():
		branch_layer.visible = false
		_set_outcome_status(_t("campaign.error.branch_route"), true)
		return
	var sequence := _next_chapter_sequence()
	var base := "chapter_%d" % sequence
	var added: Dictionary = compiler.add_campaign_branch(
		source,
		str(campaign.get("id", "")),
		outcome_node_id,
		str(choice.target_id),
		str(choice.objective_id),
		str(choice.result),
		base + "_map",
		name,
		base + "_story",
		"Before %s" % name,
		base + "_battle",
		"%s Battle" % name,
		_branch_fill_terrain()
	)
	if not added.ok:
		var detail := _t("campaign.error.project_incomplete")
		if not added.diagnostics.is_empty():
			detail = str(added.diagnostics[0].get(
				"detail", added.diagnostics[0].get("code", detail)
			))
		_set_outcome_status(_t("campaign.error.chapter_not_created", {"detail": detail.to_upper()}), true)
		return
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = str(added.source)
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	branch_layer.visible = false
	_refresh_outcome_layer(base + "_story_begins")
	_set_outcome_status(_t("campaign.status.branch_created", {"name": name.to_upper()}), false)


func _open_new_ending() -> void:
	ending_node_id = ""
	ending_kicker.text = _t("campaign.ending.new")
	ending_question.text = _t("campaign.ending.new_question")
	ending_explanation.text = _t("campaign.ending.new_help")
	ending_create_button.text = _t("campaign.ending.create_use")
	ending_name.text = "New Ending"
	ending_layer.visible = true
	ending_name.grab_focus()
	ending_name.select_all()


func _close_new_ending() -> void:
	ending_layer.visible = false
	if not ending_node_id.is_empty() and node_buttons.has(ending_node_id):
		var button: Button = node_buttons[ending_node_id]
		button.grab_focus()
	else:
		add_route_button.grab_focus()


func _create_ending_from_text(_value: String) -> void:
	_create_ending()


func _next_ending_id(nodes: Array) -> String:
	var used: Array[String] = []
	for value: Variant in nodes:
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	var sequence := 1
	while used.has("ending_%d" % sequence):
		sequence += 1
	return "ending_%d" % sequence


func _create_ending() -> void:
	var name := ending_name.text.strip_edges()
	if name.is_empty():
		_set_outcome_status(_t("campaign.error.ending_name"), true)
		ending_name.grab_focus()
		return
	if not ending_node_id.is_empty():
		_rename_ending(name)
		return
	var candidate := _candidate_outcome_node()
	if candidate.is_empty():
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var ending_id := _next_ending_id(candidate.nodes)
	var transition_id := _next_outcome_id(transitions)
	var maximum_priority := -10
	var has_victory := false
	var has_defeat := false
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		maximum_priority = maxi(maximum_priority, int(value.get("priority", 0)))
		has_victory = has_victory or _outcome_condition_kind(value) == "victory"
		has_defeat = has_defeat or _outcome_condition_kind(value) == "defeat"
	var added := {
		"id": transition_id,
		"targetNodeId": ending_id,
		"priority": maximum_priority + 10,
	}
	if outcome_objective.item_count > 0:
		added.when = {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory" if not has_victory else ("defeat" if not has_defeat else "victory"),
		}
	transitions.append(added)
	node.transitions = transitions
	candidate.node = node
	candidate.nodes.append({
		"id": ending_id,
		"name": name,
		"kind": "terminal",
		"transitions": [],
	})
	ending_layer.visible = false
	_accept_outcome_candidate(
		candidate, _t("campaign.status.ending_created", {"name": name.to_upper()}), transition_id
	)


func _rename_ending(name: String) -> void:
	var candidate: Dictionary = project.duplicate(true)
	var found := false
	var campaigns: Array = candidate.get("campaigns", [])
	for campaign_value: Variant in campaigns:
		if not campaign_value is Dictionary:
			continue
		if str(campaign_value.get("id", "")) != str(campaign.get("id", "")):
			continue
		var flow: Variant = campaign_value.get("flow", {})
		if not flow is Dictionary:
			continue
		for node_value: Variant in flow.get("nodes", []):
			if not node_value is Dictionary:
				continue
			if (
				str(node_value.get("id", "")) == ending_node_id
				and str(node_value.get("kind", "")) == "terminal"
			):
				node_value.name = name
				found = true
				break
		break
	if not found:
		ending_layer.visible = false
		_set_status(_t("campaign.error.ending_missing"), true)
		return
	var candidate_source := JSON.stringify(_integer_numbers(candidate), "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		var detail := _t("campaign.error.project_incomplete")
		if not checked.diagnostics.is_empty():
			detail = str(checked.diagnostics[0].get(
				"detail", checked.diagnostics[0].get("code", detail)
			))
		_set_outcome_status(_t("campaign.error.ending_not_changed", {"detail": detail.to_upper()}), true)
		return
	if candidate_source == source:
		_close_new_ending()
		return
	var renamed_id := ending_node_id
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	ending_layer.visible = false
	ending_node_id = ""
	_set_status(_t("campaign.status.ending_renamed"), false)
	if node_buttons.has(renamed_id):
		var button: Button = node_buttons[renamed_id]
		button.grab_focus()


func _candidate_outcome_node() -> Dictionary:
	var candidate: Dictionary = project.duplicate(true)
	var campaigns: Array = candidate.get("campaigns", []).duplicate(true)
	var campaign_id := str(campaign.get("id", ""))
	for campaign_index in campaigns.size():
		if not campaigns[campaign_index] is Dictionary or str(campaigns[campaign_index].get("id", "")) != campaign_id:
			continue
		var flow: Dictionary = campaigns[campaign_index].get("flow", {}).duplicate(true)
		var nodes: Array = flow.get("nodes", []).duplicate(true)
		for node_index in nodes.size():
			if nodes[node_index] is Dictionary and str(nodes[node_index].get("id", "")) == outcome_node_id:
				return {
					"project": candidate,
					"campaigns": campaigns,
					"campaign_index": campaign_index,
					"flow": flow,
					"nodes": nodes,
					"node_index": node_index,
					"node": nodes[node_index].duplicate(true),
				}
	return {}


func _when_from_controls(existing: Dictionary = {}) -> Variant:
	match _selected_metadata(outcome_condition):
		"always":
			return null
		"victory", "defeat":
			var objective_id := _selected_metadata(outcome_objective)
			if objective_id.is_empty():
				return {}
			return {
				"kind": "objectiveResult",
				"objectiveId": objective_id,
				"result": "defeat" if _selected_metadata(outcome_condition) == "defeat" else "victory",
			}
		"item":
			var item_id := _selected_metadata(outcome_item)
			if item_id.is_empty():
				return {}
			return {
				"kind": "inventoryAtLeast",
				"itemId": item_id,
				"quantity": maxi(1, int(outcome_quantity_label.text)),
			}
		"talked", "not_talked":
			var flag_id := _selected_metadata(outcome_flag)
			if flag_id.is_empty():
				return {}
			return {
				"kind": "worldFlagEquals",
				"flagId": flag_id,
				"value": _selected_metadata(outcome_condition) == "talked",
			}
		"compound":
			var existing_when: Variant = existing.get("when", {})
			return existing_when.duplicate(true) if existing_when is Dictionary else {}
		"advanced":
			return existing.get("when", {}).duplicate(true)
	return {}


func _apply_outcome() -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var target_id := _selected_metadata(outcome_target)
	if target_id.is_empty():
		_set_outcome_status(_t("campaign.error.choose_destination"), true)
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition: Dictionary = transitions[transition_index].duplicate(true)
	transition.targetNodeId = target_id
	var when: Variant = _when_from_controls(transition)
	if when is Dictionary and when.is_empty() and _selected_metadata(outcome_condition) != "advanced":
		_set_outcome_status(_t("campaign.error.choose_condition_content"), true)
		return
	if when == null:
		transition.erase("when")
	else:
		transition.when = when
	transitions[transition_index] = transition
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, _t("campaign.status.outcome_changed"), str(transition.get("id", "")))


func _compound_summary(when: Dictionary) -> String:
	match str(when.get("kind", "")):
		"all":
			return _t("campaign.compound.all_summary", {"count": when.get("conditions", []).size()})
		"any":
			return _t("campaign.compound.any_summary", {"count": when.get("conditions", []).size()})
		"not":
			return _t("campaign.compound.not_summary")
	return _t("campaign.compound.add_requirements")


func _default_compound_predicate() -> Dictionary:
	if outcome_objective.item_count > 0:
		return {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory",
		}
	if outcome_item.item_count > 0:
		return {
			"kind": "inventoryAtLeast",
			"itemId": str(outcome_item.get_item_metadata(0)),
			"quantity": 1,
		}
	if outcome_flag.item_count > 0:
		return {
			"kind": "worldFlagEquals",
			"flagId": str(outcome_flag.get_item_metadata(0)),
			"value": true,
		}
	return {}


func _open_compound() -> void:
	var transition := _selected_outcome()
	if transition.is_empty():
		return
	compound_kind.clear()
	if outcome_objective.item_count > 0:
		for choice: Dictionary in [
			{"id": "victory", "label": _t("campaign.condition.goal_won")},
			{"id": "defeat", "label": _t("campaign.condition.goal_lost")},
		]:
			compound_kind.add_item(choice.label)
			compound_kind.set_item_metadata(compound_kind.item_count - 1, choice.id)
	if outcome_item.item_count > 0:
		compound_kind.add_item(_t("campaign.condition.have_item"))
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "item")
	if outcome_flag.item_count > 0:
		compound_kind.add_item(_t("campaign.condition.talked"))
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "talked")
		compound_kind.add_item(_t("campaign.condition.not_talked"))
		compound_kind.set_item_metadata(compound_kind.item_count - 1, "not_talked")
	_copy_option_items(outcome_objective, compound_objective)
	_copy_option_items(outcome_item, compound_item)
	_copy_option_items(outcome_flag, compound_flag)
	compound_conditions.clear()
	var when: Variant = transition.get("when", {})
	var mode := "all"
	if when is Dictionary and _editable_compound(when):
		mode = str(when.get("kind", "all"))
		if mode == "not":
			compound_conditions.append(when.condition.duplicate(true))
		else:
			for value: Variant in when.get("conditions", []):
				compound_conditions.append(value.duplicate(true))
	elif when is Dictionary and _editable_predicate_kind(when) != "advanced":
		compound_conditions.append(when.duplicate(true))
	if compound_conditions.is_empty():
		var first := _default_compound_predicate()
		if not first.is_empty():
			compound_conditions.append(first)
	if compound_conditions.is_empty():
		_set_outcome_status(_t("campaign.error.compound_content"), true)
		return
	compound_refreshing = true
	_select_metadata(compound_mode, mode)
	compound_refreshing = false
	_refresh_compound_list(0)
	compound_layer.visible = true
	compound_select.grab_focus()


func _copy_option_items(from: OptionButton, to: OptionButton) -> void:
	to.clear()
	for index in from.item_count:
		to.add_item(from.get_item_text(index))
		to.set_item_metadata(to.item_count - 1, from.get_item_metadata(index))


func _close_compound() -> void:
	compound_layer.visible = false
	outcome_compound_button.grab_focus()


func _refresh_compound_list(preferred_index := -1) -> void:
	compound_refreshing = true
	var selected := preferred_index if preferred_index >= 0 else compound_select.selected
	compound_select.clear()
	for index in compound_conditions.size():
		compound_select.add_item(_t("campaign.compound.entry", {
			"number": index + 1,
			"requirement": _predicate_text(compound_conditions[index]),
		}))
	compound_select.select(clampi(selected, 0, maxi(0, compound_select.item_count - 1)))
	compound_remove_button.disabled = compound_conditions.size() <= 1
	compound_refreshing = false
	_refresh_compound_fields()


func _refresh_compound_fields(_index: int = 0) -> void:
	if compound_refreshing or compound_conditions.is_empty():
		return
	compound_refreshing = true
	var predicate := compound_conditions[clampi(compound_select.selected, 0, compound_conditions.size() - 1)]
	var kind := _editable_predicate_kind(predicate)
	_select_metadata(compound_kind, kind)
	_select_metadata(compound_objective, str(predicate.get("objectiveId", "")))
	_select_metadata(compound_item, str(predicate.get("itemId", "")))
	_select_metadata(compound_flag, str(predicate.get("flagId", "")))
	compound_quantity_label.text = str(maxi(1, int(predicate.get("quantity", 1))))
	compound_objective_column.visible = kind in ["victory", "defeat"]
	compound_item_column.visible = kind == "item"
	compound_flag_column.visible = kind in ["talked", "not_talked"]
	compound_quantity_row.visible = kind == "item"
	compound_refreshing = false


func _compound_predicate_from_controls() -> Dictionary:
	match _selected_metadata(compound_kind):
		"victory", "defeat":
			return {
				"kind": "objectiveResult",
				"objectiveId": _selected_metadata(compound_objective),
				"result": "defeat" if _selected_metadata(compound_kind) == "defeat" else "victory",
			}
		"item":
			return {
				"kind": "inventoryAtLeast",
				"itemId": _selected_metadata(compound_item),
				"quantity": maxi(1, int(compound_quantity_label.text)),
			}
		"talked", "not_talked":
			return {
				"kind": "worldFlagEquals",
				"flagId": _selected_metadata(compound_flag),
				"value": _selected_metadata(compound_kind) == "talked",
			}
	return {}


func _update_compound_condition(_index: int = 0) -> void:
	if compound_refreshing or compound_conditions.is_empty():
		return
	var selected := clampi(compound_select.selected, 0, compound_conditions.size() - 1)
	var predicate := _compound_predicate_from_controls()
	if predicate.is_empty():
		return
	compound_conditions[selected] = predicate
	_refresh_compound_list(selected)


func _adjust_compound_quantity(amount: int) -> void:
	compound_quantity_label.text = str(clampi(
		int(compound_quantity_label.text) + amount, 1, 9999
	))
	_update_compound_condition()


func _add_compound_condition() -> void:
	_update_compound_condition()
	var added := _default_compound_predicate()
	if added.is_empty():
		return
	compound_conditions.append(added)
	if _selected_metadata(compound_mode) == "not":
		_select_metadata(compound_mode, "all")
	_refresh_compound_list(compound_conditions.size() - 1)


func _remove_compound_condition() -> void:
	if compound_conditions.size() <= 1:
		return
	var selected := clampi(compound_select.selected, 0, compound_conditions.size() - 1)
	compound_conditions.remove_at(selected)
	_refresh_compound_list(mini(selected, compound_conditions.size() - 1))


func _change_compound_mode(_index: int = 0) -> void:
	if compound_refreshing:
		return
	if _selected_metadata(compound_mode) == "not" and compound_conditions.size() != 1:
		compound_refreshing = true
		_select_metadata(compound_mode, "all")
		compound_refreshing = false
		compound_status.text = _t("campaign.compound.not_limit")
		compound_status.add_theme_color_override("font_color", Color("ff7a83"))
	else:
		compound_status.text = _t("campaign.compound.help")
		compound_status.add_theme_color_override("font_color", Color("a9bdc9"))


func _apply_compound() -> void:
	_update_compound_condition()
	var mode := _selected_metadata(compound_mode)
	if compound_conditions.is_empty() or (mode == "not" and compound_conditions.size() != 1):
		compound_status.text = _t("campaign.error.compound_invalid")
		compound_status.add_theme_color_override("font_color", Color("ff7a83"))
		return
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition: Dictionary = transitions[transition_index].duplicate(true)
	transition.when = (
		{"kind": "not", "condition": compound_conditions[0].duplicate(true)}
		if mode == "not" else
		{"kind": mode, "conditions": compound_conditions.duplicate(true)}
	)
	transitions[transition_index] = transition
	node.transitions = transitions
	candidate.node = node
	compound_layer.visible = false
	if _accept_outcome_candidate(
		candidate, _t("campaign.status.requirements_changed"), str(transition.get("id", ""))
	):
		outcome_compound_button.grab_focus()


func _next_outcome_id(transitions: Array) -> String:
	var sequence := 1
	var used: Array[String] = []
	for value: Variant in transitions:
		if value is Dictionary:
			used.append(str(value.get("id", "")))
	while used.has("outcome_%d" % sequence):
		sequence += 1
	return "outcome_%d" % sequence


func _add_outcome() -> void:
	var candidate := _candidate_outcome_node()
	if candidate.is_empty():
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var transition_id := _next_outcome_id(transitions)
	var maximum_priority := -10
	var has_victory := false
	var has_defeat := false
	for value: Variant in transitions:
		if not value is Dictionary:
			continue
		maximum_priority = maxi(maximum_priority, int(value.get("priority", 0)))
		has_victory = has_victory or _outcome_condition_kind(value) == "victory"
		has_defeat = has_defeat or _outcome_condition_kind(value) == "defeat"
	var added := {
		"id": transition_id,
		"targetNodeId": _default_outcome_target(),
		"priority": maximum_priority + 10,
	}
	if outcome_objective.item_count > 0:
		added.when = {
			"kind": "objectiveResult",
			"objectiveId": str(outcome_objective.get_item_metadata(0)),
			"result": "victory" if not has_victory else ("defeat" if not has_defeat else "victory"),
		}
	transitions.append(added)
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, _t("campaign.status.outcome_added"), transition_id)


func _remove_outcome() -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	if transitions.size() <= 1:
		_set_outcome_status(_t("campaign.error.keep_route"), true)
		return
	transitions.remove_at(transition_index)
	for index in transitions.size():
		transitions[index].priority = index * 10
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, _t("campaign.status.outcome_removed"))


func _move_outcome(offset: int) -> void:
	var candidate := _candidate_outcome_node()
	var transition_index := _selected_outcome_index()
	if candidate.is_empty() or transition_index < 0:
		return
	var node: Dictionary = candidate.node
	var transitions: Array = node.get("transitions", []).duplicate(true)
	var target_index := transition_index + offset
	if target_index < 0 or target_index >= transitions.size():
		return
	var transition_id := str(transitions[transition_index].get("id", ""))
	var moved: Variant = transitions[transition_index]
	transitions[transition_index] = transitions[target_index]
	transitions[target_index] = moved
	for index in transitions.size():
		transitions[index].priority = index * 10
	node.transitions = transitions
	candidate.node = node
	_accept_outcome_candidate(candidate, _t("campaign.status.outcome_reordered"), transition_id)


func _accept_outcome_candidate(
	candidate: Dictionary, message: String, preferred_id := ""
) -> bool:
	candidate.nodes[candidate.node_index] = candidate.node
	candidate.flow.nodes = candidate.nodes
	candidate.campaigns[candidate.campaign_index].flow = candidate.flow
	candidate.project.campaigns = candidate.campaigns
	var portable: Dictionary = _integer_numbers(candidate.project)
	var candidate_source := JSON.stringify(portable, "  ", true) + "\n"
	var checked: Dictionary = compiler.compile_source_json(candidate_source)
	if not checked.ok:
		var detail := _t("campaign.error.project_incomplete")
		if not checked.diagnostics.is_empty():
			detail = str(checked.diagnostics[0].get("detail", checked.diagnostics[0].get("code", detail)))
		_set_outcome_status(_t("campaign.error.not_changed", {"detail": detail.to_upper()}), true)
		return false
	if candidate_source == source:
		return true
	undo_stack.append(source)
	if undo_stack.size() > HISTORY_LIMIT:
		undo_stack.pop_front()
	redo_stack.clear()
	source = candidate_source
	dirty = source != saved_source
	_read_source()
	_collect_route()
	_refresh_route()
	_update_actions()
	_refresh_outcome_layer(preferred_id)
	_set_outcome_status(_t("campaign.status.unsaved", {"change": message}), false)
	return true


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


func _set_outcome_status(message: String, failure: bool) -> void:
	outcome_status.text = message
	outcome_status.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _build_interface() -> void:
	var background := ColorRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.color = Color("07111b")
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	title_label = _label(_t("campaign.heading"), 24, Color("69d9ff"))
	title_label.position = Vector2(80, 42)
	title_label.size = Vector2(1760, 42)
	add_child(title_label)
	var subtitle := _label(
		_t("campaign.subtitle"), 36, Color("f2f7f9")
	)
	subtitle.position = Vector2(80, 84)
	subtitle.size = Vector2(1760, 54)
	add_child(subtitle)

	var toolbar := HBoxContainer.new()
	toolbar.position = Vector2(80, 148)
	toolbar.size = Vector2(1760, 62)
	toolbar.add_theme_constant_override("separation", 12)
	add_child(toolbar)
	save_button = _button(_t("campaign.save"), 170, 58)
	save_button.pressed.connect(_save)
	toolbar.add_child(save_button)
	undo_button = _button(_t("campaign.undo"), 150, 58)
	undo_button.pressed.connect(_undo)
	toolbar.add_child(undo_button)
	redo_button = _button(_t("campaign.redo"), 150, 58)
	redo_button.pressed.connect(_redo)
	toolbar.add_child(redo_button)
	var projects := _button(_t("campaign.projects"), 200, 58)
	projects.pressed.connect(_open_projects)
	toolbar.add_child(projects)
	var army := _button(_t("campaign.army"), 260, 58)
	army.pressed.connect(_open_army)
	toolbar.add_child(army)
	test_one_button = _button(_t("campaign.test.whole_one"), 210, 58)
	test_one_button.tooltip_text = _t("campaign.test.whole_one_tooltip")
	test_one_button.pressed.connect(_play_campaign.bind(1))
	toolbar.add_child(test_one_button)
	test_two_button = _button(_t("campaign.test.whole_two"), 220, 58)
	test_two_button.tooltip_text = _t("campaign.test.whole_two_tooltip")
	test_two_button.pressed.connect(_play_campaign.bind(2))
	toolbar.add_child(test_two_button)
	var hint := _label(
		_t("campaign.controls_hint"),
		15,
		Color("718896")
	)
	hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	hint.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	toolbar.add_child(hint)

	var panel := PanelContainer.new()
	panel.position = Vector2(80, 232)
	panel.size = Vector2(1760, 760)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 18, Color(0.20, 0.62, 0.78, 0.35))
	)
	add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var explanation := _label(
		_t("campaign.guidance"),
		18,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 38
	column.add_child(explanation)
	var campaign_row := HBoxContainer.new()
	campaign_row.add_theme_constant_override("separation", 16)
	column.add_child(campaign_row)
	var campaign_title := _label(_t("campaign.campaign_label"), 15, Color("69d9ff"))
	campaign_title.custom_minimum_size = Vector2(110, 54)
	campaign_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	campaign_row.add_child(campaign_title)
	campaign_select = OptionButton.new()
	campaign_select.custom_minimum_size = Vector2(620, 54)
	campaign_select.add_theme_font_size_override("font_size", 18)
	campaign_select.tooltip_text = _t("campaign.selector_tooltip")
	campaign_select.item_selected.connect(_change_campaign)
	campaign_row.add_child(campaign_select)
	var add_campaign := _button(_t("campaign.add"), 220, 54)
	add_campaign.tooltip_text = _t("campaign.add_tooltip")
	add_campaign.pressed.connect(_open_new_campaign)
	campaign_row.add_child(add_campaign)
	campaign_dependency_button = _button(_t("campaign.delete.open"), 280, 54)
	campaign_dependency_button.tooltip_text = _t("campaign.delete.open_tooltip")
	campaign_dependency_button.pressed.connect(
		func() -> void:
			_open_dependency_review(
				"campaign", str(campaign.get("id", "")), campaign_dependency_button
			)
	)
	campaign_row.add_child(campaign_dependency_button)
	var campaign_help := _label(
		_t("campaign.campaign_help"),
		15,
		Color("a9bdc9")
	)
	campaign_help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	campaign_help.custom_minimum_size = Vector2(0, 54)
	campaign_help.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	campaign_help.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	campaign_row.add_child(campaign_help)
	var player_row := HBoxContainer.new()
	player_row.add_theme_constant_override("separation", 16)
	column.add_child(player_row)
	var player_title := _label(_t("campaign.players.label"), 15, Color("69d9ff"))
	player_title.custom_minimum_size = Vector2(110, 54)
	player_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	player_row.add_child(player_title)
	player_mode_select = OptionButton.new()
	player_mode_select.custom_minimum_size = Vector2(390, 54)
	player_mode_select.add_theme_font_size_override("font_size", 18)
	for choice: Dictionary in [
		{"id": "one", "label": _t("campaign.players.one")},
		{"id": "two", "label": _t("campaign.players.two")},
		{"id": "choice", "label": _t("campaign.players.choice")},
	]:
		player_mode_select.add_item(choice.label)
		player_mode_select.set_item_metadata(player_mode_select.item_count - 1, choice.id)
	player_mode_select.item_selected.connect(_change_player_mode)
	player_row.add_child(player_mode_select)
	player_mode_help = _label("", 15, Color("a9bdc9"))
	player_mode_help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	player_mode_help.custom_minimum_size = Vector2(0, 54)
	player_mode_help.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	player_mode_help.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	player_row.add_child(player_mode_help)
	target_budget_label = _label(_t("campaign.targets.checking"), 16, Color("a9bdc9"))
	target_budget_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	target_budget_label.custom_minimum_size.y = 28
	column.add_child(target_budget_label)
	route_filter = LineEdit.new()
	route_filter.placeholder_text = _t("campaign.route.search")
	route_filter.custom_minimum_size = Vector2(0, 46)
	route_filter.add_theme_font_size_override("font_size", 17)
	route_filter.tooltip_text = _t("campaign.route.search_tooltip")
	route_filter.text_changed.connect(_filter_route)
	column.add_child(route_filter)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	route_list = VBoxContainer.new()
	route_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	route_list.add_theme_constant_override("separation", 10)
	scroll.add_child(route_list)
	var status_row := HBoxContainer.new()
	status_row.add_theme_constant_override("separation", 12)
	column.add_child(status_row)
	status_label = _label(_t("campaign.opening"), 17, Color("a9bdc9"))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.custom_minimum_size.y = 36
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_row.add_child(status_label)
	fix_button = _button(_t("campaign.diagnostic.fix.issue"), 280, 56)
	fix_button.size_flags_horizontal = Control.SIZE_SHRINK_END
	fix_button.visible = false
	fix_button.pressed.connect(_fix_diagnostic)
	status_row.add_child(fix_button)
	_build_outcome_layer()
	_build_chapter_order_layer()
	_build_new_campaign_layer()
	_build_dependency_layer()


func _build_new_campaign_layer() -> void:
	new_campaign_layer = Control.new()
	new_campaign_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	new_campaign_layer.visible = false
	new_campaign_layer.z_index = 45
	add_child(new_campaign_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	new_campaign_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(500, 250)
	panel.size = Vector2(920, 520)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	new_campaign_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("campaign.new.heading"), 18, Color("69d9ff")))
	column.add_child(_label(_t("campaign.new.question"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("campaign.new.help"),
		18,
		Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 82
	column.add_child(explanation)
	new_campaign_name = LineEdit.new()
	new_campaign_name.custom_minimum_size = Vector2(0, 62)
	new_campaign_name.add_theme_font_size_override("font_size", 22)
	new_campaign_name.text_submitted.connect(_create_new_campaign_from_text)
	column.add_child(new_campaign_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	new_campaign_create_button = _button(_t("campaign.new.create"), 400, 66)
	new_campaign_create_button.pressed.connect(_create_new_campaign)
	actions.add_child(new_campaign_create_button)
	var cancel := _button(_t("campaign.cancel"), 240, 66)
	cancel.pressed.connect(_close_new_campaign)
	actions.add_child(cancel)


func _build_dependency_layer() -> void:
	dependency_layer = Control.new()
	dependency_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dependency_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	dependency_layer.visible = false
	dependency_layer.z_index = 58
	add_child(dependency_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.005, 0.01, 0.016, 0.95)
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
	column.add_child(_label(_t("campaign.delete.safe"), 18, Color("69d9ff")))
	dependency_heading = _label(_t("campaign.delete.heading"), 29, Color("f2f7f9"))
	dependency_heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_heading.custom_minimum_size.y = 70
	column.add_child(dependency_heading)
	dependency_summary = _label("", 19, Color("a9bdc9"))
	dependency_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dependency_summary.custom_minimum_size.y = 90
	column.add_child(dependency_summary)
	var uses_panel := PanelContainer.new()
	uses_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	uses_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0c1d29"), 14, Color(0.35, 0.45, 0.72, 0.42))
	)
	column.add_child(uses_panel)
	dependency_uses = RichTextLabel.new()
	dependency_uses.custom_minimum_size = Vector2(1120, 390)
	dependency_uses.add_theme_font_size_override("normal_font_size", 18)
	dependency_uses.add_theme_color_override("default_color", Color("dce8ed"))
	uses_panel.add_child(dependency_uses)
	var note := _label(
		_t("campaign.delete.help"),
		16,
		Color("718896")
	)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.custom_minimum_size.y = 44
	column.add_child(note)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	dependency_confirm_button = _button(_t("campaign.delete.cannot"), 380, 64)
	dependency_confirm_button.pressed.connect(_confirm_dependency_delete)
	actions.add_child(dependency_confirm_button)
	dependency_close_button = _button(_t("campaign.delete.keep"), 260, 64)
	dependency_close_button.pressed.connect(_close_dependency_review)
	actions.add_child(dependency_close_button)


func _build_chapter_order_layer() -> void:
	chapter_order_layer = Control.new()
	chapter_order_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	chapter_order_layer.visible = false
	chapter_order_layer.z_index = 30
	add_child(chapter_order_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	chapter_order_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 210)
	panel.size = Vector2(1060, 660)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	chapter_order_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("campaign.order.heading"), 18, Color("69d9ff")))
	column.add_child(_label(_t("campaign.order.title"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("campaign.order.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 76
	column.add_child(explanation)
	column.add_child(_label(_t("campaign.order.chapter"), 15, Color("69d9ff")))
	chapter_order_select = OptionButton.new()
	chapter_order_select.custom_minimum_size = Vector2(960, 64)
	chapter_order_select.add_theme_font_size_override("font_size", 19)
	chapter_order_select.item_selected.connect(_refresh_chapter_order_actions)
	column.add_child(chapter_order_select)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	chapter_earlier_button = _button(_t("campaign.order.earlier"), 280, 66)
	chapter_earlier_button.pressed.connect(_move_chapter.bind(-1))
	actions.add_child(chapter_earlier_button)
	chapter_later_button = _button(_t("campaign.order.later"), 280, 66)
	chapter_later_button.pressed.connect(_move_chapter.bind(1))
	actions.add_child(chapter_later_button)
	var done := _button(_t("campaign.done"), 240, 66)
	done.pressed.connect(_close_chapter_order)
	actions.add_child(done)
	chapter_order_status = _label(_t("campaign.ready"), 17, Color("72e08f"))
	chapter_order_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	chapter_order_status.custom_minimum_size.y = 52
	column.add_child(chapter_order_status)


func _build_outcome_layer() -> void:
	outcome_layer = Control.new()
	outcome_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outcome_layer.visible = false
	outcome_layer.z_index = 30
	add_child(outcome_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.95)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	outcome_layer.add_child(shade)
	outcome_panel = PanelContainer.new()
	outcome_panel.position = Vector2(250, 70)
	outcome_panel.size = Vector2(1420, 940)
	outcome_panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	outcome_layer.add_child(outcome_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	outcome_panel.add_child(column)
	column.add_child(_label(_t("campaign.outcome.heading"), 18, Color("69d9ff")))
	outcome_title = _label(_t("campaign.outcome.default_title"), 31, Color("f2f7f9"))
	column.add_child(outcome_title)
	var explanation := _label(
		_t("campaign.outcome.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 48
	column.add_child(explanation)
	column.add_child(_label(_t("campaign.outcome.label"), 15, Color("69d9ff")))
	outcome_select = OptionButton.new()
	outcome_select.custom_minimum_size = Vector2(1320, 60)
	outcome_select.add_theme_font_size_override("font_size", 19)
	outcome_select.item_selected.connect(_refresh_selected_outcome)
	column.add_child(outcome_select)

	var route_row := HBoxContainer.new()
	route_row.add_theme_constant_override("separation", 18)
	column.add_child(route_row)
	var condition_column := VBoxContainer.new()
	condition_column.custom_minimum_size.x = 650
	condition_column.add_theme_constant_override("separation", 7)
	route_row.add_child(condition_column)
	condition_column.add_child(_label(_t("campaign.outcome.when"), 15, Color("69d9ff")))
	outcome_condition = OptionButton.new()
	outcome_condition.custom_minimum_size = Vector2(650, 58)
	outcome_condition.add_theme_font_size_override("font_size", 18)
	outcome_condition.item_selected.connect(_refresh_outcome_fields)
	condition_column.add_child(outcome_condition)
	var target_column := VBoxContainer.new()
	target_column.custom_minimum_size.x = 650
	target_column.add_theme_constant_override("separation", 7)
	route_row.add_child(target_column)
	target_column.add_child(_label(_t("campaign.outcome.go_to"), 15, Color("69d9ff")))
	outcome_target = OptionButton.new()
	outcome_target.custom_minimum_size = Vector2(650, 58)
	outcome_target.add_theme_font_size_override("font_size", 18)
	target_column.add_child(outcome_target)

	outcome_detail_row = HBoxContainer.new()
	outcome_detail_row.add_theme_constant_override("separation", 18)
	column.add_child(outcome_detail_row)
	outcome_objective_column = VBoxContainer.new()
	outcome_objective_column.custom_minimum_size.x = 650
	outcome_objective_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_objective_column)
	outcome_objective_column.add_child(_label(_t("campaign.outcome.goal"), 15, Color("69d9ff")))
	outcome_objective = OptionButton.new()
	outcome_objective.custom_minimum_size = Vector2(650, 58)
	outcome_objective.add_theme_font_size_override("font_size", 18)
	outcome_objective_column.add_child(outcome_objective)
	outcome_item_column = VBoxContainer.new()
	outcome_item_column.custom_minimum_size.x = 650
	outcome_item_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_item_column)
	outcome_item_column.add_child(_label(_t("campaign.outcome.item"), 15, Color("69d9ff")))
	outcome_item = OptionButton.new()
	outcome_item.custom_minimum_size = Vector2(650, 58)
	outcome_item.add_theme_font_size_override("font_size", 18)
	outcome_item_column.add_child(outcome_item)
	outcome_flag_column = VBoxContainer.new()
	outcome_flag_column.custom_minimum_size.x = 650
	outcome_flag_column.add_theme_constant_override("separation", 7)
	outcome_detail_row.add_child(outcome_flag_column)
	outcome_flag_column.add_child(_label(_t("campaign.outcome.character_talked"), 15, Color("69d9ff")))
	outcome_flag = OptionButton.new()
	outcome_flag.custom_minimum_size = Vector2(650, 58)
	outcome_flag.add_theme_font_size_override("font_size", 18)
	outcome_flag_column.add_child(outcome_flag)

	outcome_quantity_row = HBoxContainer.new()
	outcome_quantity_row.add_theme_constant_override("separation", 10)
	column.add_child(outcome_quantity_row)
	var quantity_title := _label(_t("campaign.outcome.item_amount"), 15, Color("69d9ff"))
	quantity_title.custom_minimum_size = Vector2(260, 52)
	quantity_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_quantity_row.add_child(quantity_title)
	var minus := _button("−", 90, 50)
	minus.pressed.connect(_adjust_outcome_quantity.bind(-1))
	outcome_quantity_row.add_child(minus)
	outcome_quantity_label = _label("1", 20, Color("f2f7f9"))
	outcome_quantity_label.custom_minimum_size = Vector2(150, 50)
	outcome_quantity_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_quantity_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_quantity_row.add_child(outcome_quantity_label)
	var plus := _button("+", 90, 50)
	plus.pressed.connect(_adjust_outcome_quantity.bind(1))
	outcome_quantity_row.add_child(plus)
	outcome_quantity_buttons = [minus, plus]
	outcome_compound_row = HBoxContainer.new()
	outcome_compound_row.add_theme_constant_override("separation", 16)
	column.add_child(outcome_compound_row)
	outcome_compound_summary = _label(_t("campaign.compound.add_requirements"), 17, Color("a9bdc9"))
	outcome_compound_summary.custom_minimum_size = Vector2(860, 58)
	outcome_compound_summary.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	outcome_compound_row.add_child(outcome_compound_summary)
	outcome_compound_button = _button(_t("campaign.compound.edit"), 420, 58)
	outcome_compound_button.pressed.connect(_open_compound)
	outcome_compound_row.add_child(outcome_compound_button)

	var edit_actions := HBoxContainer.new()
	edit_actions.add_theme_constant_override("separation", 12)
	column.add_child(edit_actions)
	outcome_apply_button = _button(_t("campaign.outcome.apply"), 300, 60)
	outcome_apply_button.pressed.connect(_apply_outcome)
	edit_actions.add_child(outcome_apply_button)
	add_route_button = _button(_t("campaign.outcome.add_route"), 300, 60)
	add_route_button.pressed.connect(_open_add_route)
	edit_actions.add_child(add_route_button)
	outcome_remove_button = _button(_t("campaign.outcome.remove"), 300, 60)
	outcome_remove_button.pressed.connect(_remove_outcome)
	edit_actions.add_child(outcome_remove_button)

	var order_actions := HBoxContainer.new()
	order_actions.add_theme_constant_override("separation", 12)
	column.add_child(order_actions)
	outcome_earlier_button = _button(_t("campaign.outcome.earlier"), 260, 56)
	outcome_earlier_button.pressed.connect(_move_outcome.bind(-1))
	order_actions.add_child(outcome_earlier_button)
	outcome_later_button = _button(_t("campaign.outcome.later"), 260, 56)
	outcome_later_button.pressed.connect(_move_outcome.bind(1))
	order_actions.add_child(outcome_later_button)
	var done := _button(_t("campaign.done"), 240, 56)
	done.pressed.connect(_close_outcomes)
	order_actions.add_child(done)
	outcome_status = _label(_t("campaign.ready"), 16, Color("72e08f"))
	outcome_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	outcome_status.custom_minimum_size.y = 44
	column.add_child(outcome_status)
	_build_compound_layer()
	_build_add_route_layer()
	_build_branch_layer()
	_build_ending_layer()


func _build_compound_layer() -> void:
	compound_layer = Control.new()
	compound_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	compound_layer.visible = false
	compound_layer.z_index = 45
	add_child(compound_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.97)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	compound_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(360, 90)
	panel.size = Vector2(1200, 900)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	compound_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 13)
	panel.add_child(column)
	column.add_child(_label(_t("campaign.compound.heading"), 18, Color("69d9ff")))
	column.add_child(_label(_t("campaign.compound.title"), 30, Color("f2f7f9")))
	var explanation := _label(
		_t("campaign.compound.explanation"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 52
	column.add_child(explanation)
	var top_row := HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 16)
	column.add_child(top_row)
	var mode_column := VBoxContainer.new()
	mode_column.custom_minimum_size.x = 380
	mode_column.add_child(_label(_t("campaign.compound.match"), 15, Color("69d9ff")))
	compound_mode = OptionButton.new()
	compound_mode.custom_minimum_size = Vector2(380, 60)
	for choice: Dictionary in [
		{"id": "all", "label": _t("campaign.compound.all")},
		{"id": "any", "label": _t("campaign.compound.any")},
		{"id": "not", "label": _t("campaign.compound.not")},
	]:
		compound_mode.add_item(choice.label)
		compound_mode.set_item_metadata(compound_mode.item_count - 1, choice.id)
	compound_mode.item_selected.connect(_change_compound_mode)
	mode_column.add_child(compound_mode)
	top_row.add_child(mode_column)
	var requirement_column := VBoxContainer.new()
	requirement_column.custom_minimum_size.x = 730
	requirement_column.add_child(_label(_t("campaign.compound.requirement"), 15, Color("69d9ff")))
	compound_select = OptionButton.new()
	compound_select.custom_minimum_size = Vector2(730, 60)
	compound_select.item_selected.connect(_refresh_compound_fields)
	requirement_column.add_child(compound_select)
	top_row.add_child(requirement_column)

	column.add_child(_label(_t("campaign.compound.this_requirement"), 15, Color("69d9ff")))
	compound_kind = OptionButton.new()
	compound_kind.custom_minimum_size = Vector2(1130, 58)
	compound_kind.item_selected.connect(_update_compound_condition)
	column.add_child(compound_kind)
	compound_detail_row = HBoxContainer.new()
	compound_detail_row.add_theme_constant_override("separation", 16)
	column.add_child(compound_detail_row)
	compound_objective_column = VBoxContainer.new()
	compound_objective_column.custom_minimum_size.x = 550
	compound_objective_column.add_child(_label(_t("campaign.outcome.goal"), 15, Color("69d9ff")))
	compound_objective = OptionButton.new()
	compound_objective.custom_minimum_size = Vector2(550, 58)
	compound_objective.item_selected.connect(_update_compound_condition)
	compound_objective_column.add_child(compound_objective)
	compound_detail_row.add_child(compound_objective_column)
	compound_item_column = VBoxContainer.new()
	compound_item_column.custom_minimum_size.x = 550
	compound_item_column.add_child(_label(_t("campaign.outcome.item"), 15, Color("69d9ff")))
	compound_item = OptionButton.new()
	compound_item.custom_minimum_size = Vector2(550, 58)
	compound_item.item_selected.connect(_update_compound_condition)
	compound_item_column.add_child(compound_item)
	compound_detail_row.add_child(compound_item_column)
	compound_flag_column = VBoxContainer.new()
	compound_flag_column.custom_minimum_size.x = 550
	compound_flag_column.add_child(_label(_t("campaign.compound.character"), 15, Color("69d9ff")))
	compound_flag = OptionButton.new()
	compound_flag.custom_minimum_size = Vector2(550, 58)
	compound_flag.item_selected.connect(_update_compound_condition)
	compound_flag_column.add_child(compound_flag)
	compound_detail_row.add_child(compound_flag_column)

	compound_quantity_row = HBoxContainer.new()
	compound_quantity_row.add_theme_constant_override("separation", 12)
	column.add_child(compound_quantity_row)
	var amount_title := _label(_t("campaign.outcome.item_amount"), 15, Color("69d9ff"))
	amount_title.custom_minimum_size = Vector2(240, 52)
	amount_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	compound_quantity_row.add_child(amount_title)
	var amount_minus := _button("−", 90, 50)
	amount_minus.pressed.connect(_adjust_compound_quantity.bind(-1))
	compound_quantity_row.add_child(amount_minus)
	compound_quantity_label = _label("1", 20, Color("f2f7f9"))
	compound_quantity_label.custom_minimum_size = Vector2(140, 50)
	compound_quantity_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	compound_quantity_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	compound_quantity_row.add_child(compound_quantity_label)
	var amount_plus := _button("+", 90, 50)
	amount_plus.pressed.connect(_adjust_compound_quantity.bind(1))
	compound_quantity_row.add_child(amount_plus)

	var list_actions := HBoxContainer.new()
	list_actions.add_theme_constant_override("separation", 14)
	column.add_child(list_actions)
	var add := _button(_t("campaign.compound.add"), 350, 60)
	add.pressed.connect(_add_compound_condition)
	list_actions.add_child(add)
	compound_remove_button = _button(_t("campaign.compound.remove"), 350, 60)
	compound_remove_button.pressed.connect(_remove_compound_condition)
	list_actions.add_child(compound_remove_button)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var apply := _button(_t("campaign.compound.apply"), 430, 64)
	apply.pressed.connect(_apply_compound)
	actions.add_child(apply)
	var cancel := _button(_t("campaign.cancel"), 280, 64)
	cancel.pressed.connect(_close_compound)
	actions.add_child(cancel)
	compound_status = _label(
		_t("campaign.compound.help"),
		16, Color("a9bdc9")
	)
	compound_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	compound_status.custom_minimum_size.y = 44
	column.add_child(compound_status)


func _build_add_route_layer() -> void:
	add_route_layer = Control.new()
	add_route_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_route_layer.visible = false
	add_route_layer.z_index = 40
	add_child(add_route_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	add_route_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 170)
	panel.size = Vector2(1060, 740)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	add_route_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	panel.add_child(column)
	column.add_child(_label(_t("campaign.add_route.heading"), 18, Color("69d9ff")))
	column.add_child(_label(_t("campaign.add_route.title"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("campaign.add_route.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 62
	column.add_child(explanation)
	add_existing_button = _button(
		_t("campaign.add_route.existing"), 960, 96
	)
	add_existing_button.pressed.connect(_choose_existing_route)
	column.add_child(add_existing_button)
	add_branch_button = _button(
		_t("campaign.add_route.branch_available"),
		960, 96
	)
	add_branch_button.pressed.connect(_choose_new_branch)
	column.add_child(add_branch_button)
	var ending := _button(
		_t("campaign.add_route.ending"), 960, 96
	)
	ending.pressed.connect(_choose_new_ending)
	column.add_child(ending)
	var cancel := _button(_t("campaign.cancel"), 260, 58)
	cancel.pressed.connect(_close_add_route)
	column.add_child(cancel)


func _build_branch_layer() -> void:
	branch_layer = Control.new()
	branch_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	branch_layer.visible = false
	branch_layer.z_index = 50
	add_child(branch_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	branch_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 240)
	panel.size = Vector2(1060, 600)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	branch_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	column.add_child(_label(_t("campaign.branch.heading"), 18, Color("69d9ff")))
	column.add_child(_label(_t("campaign.branch.title"), 32, Color("f2f7f9")))
	var explanation := _label(
		_t("campaign.branch.help"),
		18, Color("a9bdc9")
	)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.custom_minimum_size.y = 92
	column.add_child(explanation)
	column.add_child(_label(_t("campaign.branch.name"), 15, Color("69d9ff")))
	branch_name = LineEdit.new()
	branch_name.max_length = 140
	branch_name.custom_minimum_size = Vector2(960, 64)
	branch_name.add_theme_font_size_override("font_size", 22)
	branch_name.text_submitted.connect(_create_branch_from_text)
	column.add_child(branch_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	var create := _button(_t("campaign.branch.create"), 360, 66)
	create.pressed.connect(_create_branch)
	actions.add_child(create)
	var cancel := _button(_t("campaign.cancel"), 240, 66)
	cancel.pressed.connect(_close_new_branch)
	actions.add_child(cancel)


func _build_ending_layer() -> void:
	ending_layer = Control.new()
	ending_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	ending_layer.visible = false
	ending_layer.z_index = 40
	add_child(ending_layer)
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.003, 0.008, 0.014, 0.96)
	shade.mouse_filter = Control.MOUSE_FILTER_STOP
	ending_layer.add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(430, 270)
	panel.size = Vector2(1060, 540)
	panel.add_theme_stylebox_override(
		"panel", _panel(Color("0a1823"), 24, Color("69d9ff"))
	)
	ending_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	ending_kicker = _label(_t("campaign.ending.new"), 18, Color("69d9ff"))
	column.add_child(ending_kicker)
	ending_question = _label(_t("campaign.ending.new_question"), 32, Color("f2f7f9"))
	column.add_child(ending_question)
	ending_explanation = _label(
		_t("campaign.ending.new_help"),
		18, Color("a9bdc9")
	)
	ending_explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ending_explanation.custom_minimum_size.y = 82
	column.add_child(ending_explanation)
	column.add_child(_label(_t("campaign.ending.name"), 15, Color("69d9ff")))
	ending_name = LineEdit.new()
	ending_name.max_length = 160
	ending_name.custom_minimum_size = Vector2(960, 64)
	ending_name.add_theme_font_size_override("font_size", 22)
	ending_name.text_submitted.connect(_create_ending_from_text)
	column.add_child(ending_name)
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 14)
	column.add_child(actions)
	ending_create_button = _button(_t("campaign.ending.create_use"), 320, 66)
	ending_create_button.pressed.connect(_create_ending)
	actions.add_child(ending_create_button)
	var cancel := _button(_t("campaign.cancel"), 240, 66)
	cancel.pressed.connect(_close_new_ending)
	actions.add_child(cancel)


func _section_label(value: String) -> Label:
	var label := _label(value, 17, Color("69d9ff"))
	label.custom_minimum_size.y = 32
	return label


func _set_status(message: String, failure: bool) -> void:
	pending_diagnostic = {}
	if fix_button != null:
		fix_button.visible = false
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color", Color("ff7a83") if failure else Color("72e08f")
	)


func _t(message_id: String, values: Dictionary = {}) -> String:
	return Localization.text(message_id, values)


func _label(value: String, size: int, colour: Color) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", colour)
	return label


func _button(value: String, width: int, height: int = 88) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(width, height)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.add_theme_font_size_override("font_size", 20)
	button.add_theme_color_override("font_color", Color("dcebf0"))
	button.add_theme_color_override("font_hover_color", Color("071018"))
	button.add_theme_color_override("font_pressed_color", Color("071018"))
	button.add_theme_color_override("font_focus_color", Color("071018"))
	button.add_theme_stylebox_override(
		"normal", _panel(Color("102633"), 12, Color(0.25, 0.62, 0.74, 0.34))
	)
	var active := _panel(Color("69d9ff"), 12, Color("b9f2ff"))
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
	style.content_margin_left = 24
	style.content_margin_right = 24
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	return style
