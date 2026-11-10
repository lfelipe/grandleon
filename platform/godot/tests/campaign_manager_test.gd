# SPDX-License-Identifier: MIT
extends SceneTree

const CampaignManager = preload("res://scripts/campaign_manager.gd")

var failures := 0
var requested_member := 0
var requested_name := ""


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var pc_audio := root.get_node_or_null("PCAudio")
	if pc_audio != null:
		pc_audio.set_effects_level(0.0)
	var manager = CampaignManager.new()
	root.add_child(manager)
	current_scene = manager
	await process_frame
	manager.player_name_requested.connect(_capture_name)
	manager.present(_company(false, "Aren", "Aren"))
	manager._select_member(1, true)

	_expect("eligible character has a name action", _has_name_action(manager), true)
	manager._open_name_entry()
	_expect("name entry opens", manager._name_layer.visible, true)
	_expect("localized authored default is shown", manager._name_input.text, "Aren")
	_expect(
		"authored default remains distinct in the explanation",
		manager._name_default.text.contains("Aren"),
		true
	)
	_expect("default reset is hidden before an override", manager._name_use_default.visible, false)

	manager._name_input.text = "   "
	manager._validate_player_name(manager._name_input.text)
	_expect("blank name is blocked", manager._name_save.disabled, true)
	manager._name_input.text = "界".repeat(22)
	manager._validate_player_name(manager._name_input.text)
	_expect("UTF-8 byte limit is enforced", manager._name_save.disabled, true)
	manager._name_input.text = "A".repeat(64)
	manager._validate_player_name(manager._name_input.text)
	_expect("the full portable ASCII limit is accepted", manager._name_save.disabled, false)
	manager._name_input.text = "bryn"
	manager._validate_player_name(manager._name_input.text)
	_expect("a duplicate company name remains an allowed choice", manager._name_save.disabled, false)
	_expect(
		"a duplicate company name is clearly called out",
		manager._name_error.text.contains("already uses"),
		true
	)
	manager._name_input.text = "Luzia"
	manager._submit_player_name()
	_expect("valid name closes the modal", manager._name_layer.visible, false)
	_expect("valid name targets the selected member", requested_member, 1)
	_expect("valid name is trimmed and submitted", requested_name, "Luzia")

	manager.present(_company(true, "Luzia", "Aren"))
	manager._select_member(1, true)
	manager._open_name_entry()
	_expect("saved literal is the effective edit value", manager._name_input.text, "Luzia")
	_expect("saved literal can return to authored default", manager._name_use_default.visible, true)
	requested_member = 0
	requested_name = ""
	manager._use_default_name()
	_expect("default reset sends the source authored name", requested_name, "Aren Source")

	manager.present(_company(false, "Aren", "Aren"))
	manager._select_member(1, true)
	manager._open_name_entry()
	requested_member = 0
	requested_name = ""
	manager._submit_player_name()
	_expect("saving an unchanged localized default is a no-op", requested_member, 0)
	_expect("the no-op closes the entry", manager._name_layer.visible, false)

	manager._select_member(2, true)
	_expect("fixed character has no name action", _has_name_action(manager), false)
	manager._open_name_entry()
	_expect("fixed character cannot open name entry", manager._name_layer.visible, false)

	var localization := root.get_node("Localization")
	localization.call("select_language", "pt-BR", false)
	var portuguese_manager = CampaignManager.new()
	root.add_child(portuguese_manager)
	await process_frame
	portuguese_manager.present(_company(false, "Aren", "Aren"))
	portuguese_manager._select_member(1, true)
	_expect(
		"company preparation is localized in Portuguese",
		portuguese_manager._heading.text,
		"Prepare-se para a próxima batalha"
	)
	_expect(
		"company member status is localized in Portuguese",
		portuguese_manager._member_status.text.contains("NÍVEL 1"),
		true
	)
	_expect(
		"company name action is localized in Portuguese",
		_has_button_text(portuguese_manager, "ESCOLHER NOME DO PERSONAGEM"),
		true
	)
	_expect(
		"company battle action is localized in Portuguese",
		portuguese_manager._proceed_button.text.contains("INICIAR BATALHA"),
		true
	)
	portuguese_manager.queue_free()
	localization.call("select_language", "en", false)

	if failures == 0:
		print("campaign manager name entry smoke passed")
	current_scene = null
	manager.queue_free()
	await process_frame
	await process_frame
	quit(1 if failures > 0 else 0)


func _company(has_override: bool, name: String, localized_default: String) -> Dictionary:
	return {
		"encounter_id": 11,
		"fielded_count": 2,
		"capacity": 2,
		"refused": "none",
		"store": [],
		"stages": [],
		"roster": [
			{
				"member_id": 1,
				"name": name,
				"default_name": localized_default,
				"authored_name": "Aren Source",
				"player_nameable": true,
				"has_player_name": has_override,
				"availability": "available",
				"level": 1,
				"placeable": true,
				"fielded": true,
				"carried": [],
			},
			{
				"member_id": 2,
				"name": "Bryn",
				"default_name": "Bryn",
				"authored_name": "Bryn",
				"player_nameable": false,
				"has_player_name": false,
				"availability": "available",
				"level": 1,
				"placeable": true,
				"fielded": true,
				"carried": [],
			},
		],
	}


func _has_name_action(manager: Control) -> bool:
	for button: Button in manager._action_buttons:
		if button.text == "CHOOSE CHARACTER NAME":
			return true
	return false


func _has_button_text(manager: Control, expected: String) -> bool:
	for button: Button in manager._action_buttons:
		if button.text == expected:
			return true
	return false


func _capture_name(member_id: int, name: String) -> void:
	requested_member = member_id
	requested_name = name


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
