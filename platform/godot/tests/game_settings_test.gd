# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	var created: Dictionary = library.create_project("Rules Road", template)
	_expect("settings fixture is created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var localization: Node = root.get_node("Localization")
	var prior_language := str(localization.call("current_language"))
	localization.call("select_language", "pt-BR", false)
	root.get_node("CreatorContext").open_project(str(created.id))
	var packed: PackedScene = load("res://game_settings.tscn")
	var settings = packed.instantiate()
	root.add_child(settings)
	current_scene = settings
	await process_frame
	await process_frame

	_expect("project opens in Game Settings", settings.project_id, str(created.id))
	_expect(
		"Game Settings uses the selected editor language",
		settings.title_label.text,
		"RULES ROAD  /  CONFIGURAÇÕES DO JOGO"
	)
	_expect(
		"Portuguese settings heading is visible",
		_tree_has_text(settings, "Regras que os jogadores devem conhecer antes de começar"),
		true
	)
	_expect(
		"Portuguese turn-order choice is visible",
		settings.turn_order_select.get_item_text(1),
		"LADO INTEIRO DE UMA VEZ"
	)
	_expect(
		"Portuguese help copy is visible",
		settings.turn_order_help.text,
		"Um lado move sua companhia em qualquer ordem; depois, o outro lado age."
	)
	_expect(
		"long localized help remains wrapped",
		settings.turn_order_help.autowrap_mode,
		TextServer.AUTOWRAP_WORD_SMART
	)
	_expect(
		"Portuguese ready status is visible",
		settings.status_label.text,
		"PRONTO  /  AS ALTERAÇÕES VALEM PARA TODAS AS EXPORTAÇÕES"
	)
	_expect("localized settings preserve initial focus", settings.turn_order_select.has_focus(), true)
	_expect(
		"starter default turn order is visible",
		settings._selected_metadata(settings.turn_order_select),
		"sideBlocks"
	)
	_expect(
		"starter recovery rule is visible",
		settings._selected_metadata(settings.character_loss_select),
		"recoverable"
	)
	_expect("starter begins with ordinary damage", settings.testing_toggle.button_pressed, false)
	_expect("starter recommends battle close-ups", settings.battle_closeups_toggle.button_pressed, true)
	_expect("starter content revision is visible", settings.revision_label.text, "0.1.0")
	_expect("starter battle has an override row", settings.battle_list.get_child_count(), 1)

	settings._select_metadata(settings.turn_order_select, "alternating")
	settings._change_default_turn_order(settings.turn_order_select.selected)
	_expect(
		"default turn order changes",
		str(_parse(settings.source).defaultTurnOrder),
		"alternating"
	)
	settings._select_metadata(settings.character_loss_select, "permanent")
	settings._change_character_loss(settings.character_loss_select.selected)
	_expect(
		"character loss changes",
		str(_parse(settings.source).characterLoss),
		"permanent"
	)
	settings._toggle_invulnerable(true)
	_expect(
		"invulnerable playtesting is explicit source data",
		bool(_parse(settings.source).invulnerableForTesting),
		true
	)
	settings._toggle_battle_closeups(false)
	_expect(
		"disabled close-up default is portable source data",
		bool(_parse(settings.source).battleCloseups),
		false
	)
	settings._toggle_battle_closeups(true)
	_expect(
		"historical close-up default restores omission",
		_parse(settings.source).has("battleCloseups"),
		false
	)
	settings._toggle_battle_closeups(false)
	_expect("close-up control reflects authored default", settings.battle_closeups_toggle.button_pressed, false)
	settings._bump_revision("patch")
	_expect("revision bump is controlled", str(_parse(settings.source).contentRevision), "0.1.1")

	var battle_row: HBoxContainer = settings.battle_list.get_child(0)
	var battle_select: OptionButton = battle_row.get_child(1)
	settings._change_battle_turn_order(0, battle_select, 0, 1)
	_expect(
		"battle can inherit the game default",
		_parse(settings.source).campaigns[0].flow.nodes[1].has("turnOrder"),
		false
	)
	settings._undo()
	_expect(
		"battle override change is undoable",
		str(_parse(settings.source).campaigns[0].flow.nodes[1].turnOrder),
		"sideBlocks"
	)
	settings._redo()
	_expect(
		"battle override change is redoable",
		_parse(settings.source).campaigns[0].flow.nodes[1].has("turnOrder"),
		false
	)
	_expect("settings are dirty before save", settings.dirty, true)
	_expect("settings save succeeds", settings._save(), true)
	_expect("settings are clean after save", settings.dirty, false)
	var stored: Dictionary = library.load_source(str(created.id))
	_expect("saved settings reload", stored.ok, true)
	if stored.ok:
		_expect("stored turn order matches", str(stored.project.defaultTurnOrder), "alternating")
		_expect("stored character loss matches", str(stored.project.characterLoss), "permanent")
		_expect("stored testing aid matches", bool(stored.project.invulnerableForTesting), true)
		_expect("stored close-up default matches", bool(stored.project.battleCloseups), false)
		_expect("stored revision matches", str(stored.project.contentRevision), "0.1.1")
		_expect(
			"stored battle inherits the default",
			stored.project.campaigns[0].flow.nodes[1].has("turnOrder"),
			false
		)

	settings.queue_free()
	localization.call("select_language", prior_language, false)
	await process_frame
	await process_frame
	if failures == 0:
		print("game settings smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _tree_has_text(node: Node, expected: String) -> bool:
	if node is Label and node.text == expected:
		return true
	if node is BaseButton and node.text == expected:
		return true
	for child: Node in node.get_children():
		if _tree_has_text(child, expected):
			return true
	return false


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
