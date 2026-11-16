# SPDX-License-Identifier: MIT
# Actual direct-battle builder + native in-memory campaign, with legal commands.
extends SceneTree
var checks := 0
var failures := 0
func _init() -> void: call_deferred("run")
func check(value: bool, label: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("personal preview: " + label)
func run() -> void:
	var path: String = get_script().resource_path.get_base_dir().path_join("../../playstation/mipsync/tests/fixtures/personal-character-campaign.json")
	var source: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var campaign: Dictionary = source.campaigns[0]
	# A real earlier-story recruit must be available in the later direct test.
	var recruit: Dictionary = campaign.roster.pop_back()
	campaign.flow.nodes[0].recruits = [recruit]
	var before := JSON.stringify(source)
	var builder = load("res://scripts/creator/campaign_editor.gd").new()
	builder.project = source
	builder.campaign = campaign
	var preview: Dictionary = builder._battle_preview_source("required_loss")
	builder.free()
	check(preview.get("ok", false), "direct-battle source is built")
	check(JSON.stringify(source) == before, "preview never mutates authored project")
	var disposable: Dictionary = JSON.parse_string(preview.source)
	check(disposable.campaigns[0].roster.size() == 4, "earlier recruits become available in disposable roster")
	check(disposable.campaigns[0].roster[3].defeatPolicy == "loseBattle", "recruit personal rules survive the preview handoff")
	check(not disposable.campaigns[0].flow.nodes[0].has("recruits"), "disposable source does not duplicate recruit identity")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(preview.source)
	check(compiled.get("ok", false), "real direct preview package compiles")
	if not compiled.get("ok", false):
		quit(1)
		return
	for players in [1, 2]:
		await play(compiled.package, players, true)
		await play(compiled.package, players, false)
	print("PERSONAL_RULES_PREVIEW_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)
func play(bytes: PackedByteArray, players: int, remove_sword: bool) -> void:
	var session = ClassDB.instantiate("GrandleonSession")
	check(session.start_package(bytes, "personal_rules", players, "", "en"), "fresh no-disk preview starts")
	var manager = load("res://scripts/campaign_manager.gd").new()
	root.add_child(manager)
	manager.intent_requested.connect(func(kind: String, member_id: int, item_id: int, stage_id: int):
		check(kind == "take_weapon", "real company button sends an unambiguous weapon intent")
		check(session.submit_management(kind, member_id, item_id, stage_id), "real company button reaches native management")
	)
	var snapshot := {}
	var sword := 0
	var changed := false
	var began := false
	var ended := false
	var pending := false
	var request := 100
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline and not ended:
		await process_frame
		for event: Dictionary in session.poll():
			match str(event.type):
				"campaign_begun":
					check(not event.resumed, "each preview creates a fresh company")
				"management":
					var roster: Array = event.company.roster
					check(roster.size() == 4, "preview uses the actual portable roster")
					var scholar: Dictionary = roster[0]
					if not changed:
						check(scholar.carried.size() == 1, "personal sword is owned on every fresh run")
						sword = int(scholar.carried[0].id)
						changed = true
						if remove_sword:
							check(scholar.carried[0].get("category", "") == "weapon", "native company publishes the real carried category")
							check(scholar.carried[0].name == "Personal Sword", "weapon names use their own localized registry")
							manager.present(event.company)
							manager._select_member(int(scholar.member_id), true)
							var clicked := false
							for button: Button in manager._action_buttons:
								if button.text.contains("Personal Sword"):
									button.pressed.emit()
									clicked = true
									break
							check(clicked, "normal Return weapon action is reachable")
							continue
					check(session.submit_management("proceed"), "normal company page starts battle")
				"battle_begins":
					began = true
					snapshot = event.snapshot
					check(snapshot.units.size() == 5, "direct preview includes the real later recruit and fresh original roster")
					var scholar := member(snapshot, 1)
					var rhea := member(snapshot, 4)
					check(not scholar.is_empty() and not rhea.is_empty(), "named heroes are bound to persistent identities")
					if scholar.is_empty() or rhea.is_empty():
						ended = true
						continue
					check(scholar.weapons == ([] if remove_sword else [sword]), "preview joins personal inventory instead of Mage role focus")
					check(int(rhea.player) == players, "preview preserves actual 1P/2P ownership")
					check(not session.has_progress() and session.campaign_checkpoint_bytes().is_empty(), "preview does not expose installed-game save progress")
					if remove_sword: ended = true
				"snapshot":
					snapshot = event.snapshot
					if int(event.get("request_id", 0)) == request: pending = false
				"battle_ended":
					check(event.snapshot.outcome == "second_side_won", "required hero defeat ends actual preview battle")
					check(int(member(event.snapshot, 4).health) == 0, "required hero was defeated through actual combat")
					check(int(member(event.snapshot, 1).health) > 0 and int(member(event.snapshot, 3).health) > 0, "required loss applies while other allies survive")
					ended = true
				"session_failed":
					check(false, "native preview failed: " + str(event.get("message", "")))
					ended = true
		if began and not ended and not pending and snapshot.get("active_side", "") == "first":
			for unit: Dictionary in snapshot.units:
				if unit.side == "first" and unit.on_board and not unit.has_acted:
					request += 1
					pending = true
					session.submit_intent("wait", int(unit.id), 0, 0, 0, 0, request)
					break
	check(began and ended, "real native preview reaches expected outcome before timeout")
	session.stop()
	manager.queue_free()
	await process_frame
func member(snapshot: Dictionary, identity: int) -> Dictionary:
	for unit: Dictionary in snapshot.get("units", []):
		if int(unit.get("member_id", 0)) == identity: return unit
	return {}
