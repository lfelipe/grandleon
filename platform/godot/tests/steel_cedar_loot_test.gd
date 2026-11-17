# SPDX-License-Identifier: MIT
# The real demo, unchanged maps/stats, ordinary commands, and native checkpoints.
extends "native_campaign_play.gd"
var checks := 0
var game: RefCounted
var state: Dictionary = {}
var abilities := {}
var definitions := {}
var waiting := false
var sequence := 0
var proceed := true
var battle_count := 0
var ended := false
var gold := 0
var healed := 0
var management: Dictionary = {}
var used_members := {}
var tonic := 0
var sword := 0
var bell := 0

func _init() -> void: call_deferred("run")
func expect(value: bool, message: String) -> bool:
	checks += 1
	if not value:
		push_error("demo loot: " + message)
		quit(1)
		assert(value, message)
	return value
func owned(actor: Dictionary, identity: int) -> int:
	for stack: Dictionary in actor.get("carried_inventory", []):
		if int(stack.id) == identity: return int(stack.count)
	return 0
func named(name: String) -> Dictionary:
	for actor: Dictionary in state.get("units", []):
		if actor.name == name: return actor
	return {}
func pump() -> void:
	for event: Dictionary in game.poll():
		match event.type:
			"definitions":
				for value: Dictionary in event.abilities: abilities[value.id] = value
				for value: Dictionary in event.weapons:
					definitions[value.id] = value
					if value.name == "March Sword": sword = int(value.id)
					if value.name == "Pilgrim Bell": bell = int(value.id)
				for value: Dictionary in event.items:
					if value.name == "Field Tonic": tonic = int(value.id)
			"battle_begins":
				battle_count += 1
				state = event.snapshot
				waiting = false
			"snapshot":
				state = event.snapshot
				waiting = false
			"deployment_begins": game.submit_intent("begin")
			"management":
				management = event.company
				if proceed: game.submit_management("proceed")
			"battle_ended":
				expect(event.snapshot.outcome == "first_side_won", "ordinary demo play wins the opening battle")
				ended = true
			"presentation":
				for value: Dictionary in event.events:
					if value.kind == "gold_gained": gold += int(value.amount)
					if value.kind == "unit_restored" and int(event.get("action_content_id", 0)) == tonic: healed += int(value.amount)
			"refused", "session_failed": expect(false, "ordinary command failed: " + str(event))
func until(condition: Callable) -> void:
	var deadline := Time.get_ticks_msec() + 20000
	while not condition.call() and Time.get_ticks_msec() < deadline:
		pump()
		await process_frame
	pump()
	expect(condition.call(), "native worker reaches expected state before timeout")
func send(intent: Dictionary) -> void:
	sequence += 1
	game.submit_intent(intent.kind, intent.get("actor", 0), intent.get("target", 0), intent.get("x", 0), intent.get("y", 0), intent.get("content", 0), sequence, intent.get("ground", 0))
	waiting = true
	await until(func(): return not waiting or ended)
func verify_packs(tomas_member: int) -> void:
	var heroes := 0
	for actor: Dictionary in state.units:
		if actor.side != "first": continue
		heroes += 1
		var consumed := used_members.has(int(actor.member_id))
		expect(owned(actor, tonic) == (0 if consumed else 1), "tonic is founded once; used copies do not return next stage or after reload")
		if int(actor.member_id) == tomas_member:
			expect(owned(actor, sword) == 1 and owned(actor, bell) == 1, "Tomas retains both physical weapons across stage/save boundaries")
			expect(sword in actor.weapons and bell in actor.weapons, "both personal affinities still permit the acquired sword and original bell")
	expect(heroes >= 2, "real company remains present on the next board")
func play(bytes: PackedByteArray, players: int) -> void:
	state = {}; abilities = {}; definitions = {}; waiting = false; sequence = 0
	proceed = true; battle_count = 0; ended = false; gold = 0; healed = 0; management = {}; used_members = {}
	game = ClassDB.instantiate("GrandleonSession")
	var scope := ProjectSettings.globalize_path("user://demo-loot-%d-%d" % [players, Time.get_ticks_usec()])
	expect(game.start_package(bytes, "steel_and_cedar", players, scope, "en"), "%dP actual demo starts" % players)
	await until(func(): return battle_count == 1 and not state.is_empty())
	var count := 0
	for actor: Dictionary in state.units:
		if actor.side != "first": continue
		count += 1
		expect(actor.carry_capacity == 5 and actor.carried_inventory.size() == 2 and owned(actor, tonic) == 1, "each deployed hero begins with one weapon and one tonic in five combined slots")
	expect(count == players * 2, "1P/2P retains the authored distinct company")
	var tomas_member := int(named("Tomas Reed").member_id)
	expect(not named("March Sergeant").is_empty(), "existing orchard opponent is the reusable guaranteed-drop officer")
	while state.outcome == "ongoing" and sequence < 250:
		await until(func(): return state.active_side == "first" or state.outcome != "ongoing")
		if state.outcome != "ongoing": break
		var intent := {}
		for actor: Dictionary in state.units:
			if actor.side == "first" and actor.on_board and not actor.has_acted and actor.maximum_health - actor.health >= 2 and owned(actor, tonic) > 0:
				intent = {"kind":"item", "actor":actor.id, "target":actor.id, "content":tonic}
				used_members[int(actor.member_id)] = true
				break
		if intent.is_empty():
			intent = _ordinary_intent(game, state, abilities)
			# Keep the healer behind Rowan rather than sending him to the
			# nearest foe's adjacent tile. Attacks and healing remain ordinary.
			if intent.get("kind") == "move" and int(intent.actor) == int(named("Tomas Reed").id):
				intent = {"kind":"wait", "actor":intent.actor}
		expect(not intent.is_empty(), "normal demo pilot finds a legal combat action")
		var expected_health := -1
		if intent.get("kind") == "item":
			for actor: Dictionary in state.units:
				if int(actor.id) == int(intent.actor): expected_health = mini(int(actor.maximum_health), int(actor.health) + 8)
		await send(intent)
		if expected_health >= 0:
			for actor: Dictionary in state.units:
				if int(actor.id) == int(intent.actor): expect(int(actor.health) == expected_health, "tonic restores eight HP capped at the hero's maximum")
	expect(state.get("collecting_loot", false) and not ended, "victory waits for spatial collection and explicit Continue")
	expect(not used_members.is_empty() and healed >= 2, "real wounded heroes use the eight-health tonic during combat")
	expect(gold == (6 if players == 1 else 8), "authored common foes produce exactly two gold each")
	var found := {}
	for pile: Dictionary in state.ground_items:
		if int(pile.id) == sword: found = pile; break
	expect(not found.is_empty(), "first orchard always leaves a physical March Sword")
	# Another hero may finish combat on the dropped sword. Move them normally
	# before Tomas approaches; collection never bypasses tile occupancy.
	for actor: Dictionary in state.units.duplicate(true):
		if actor.on_board and actor.side == "first" and actor.name != "Tomas Reed" and int(actor.x) == int(found.x) and int(actor.y) == int(found.y):
			var free: Array = game.query_tiles("move", int(actor.id))
			expect(not free.is_empty(), "ally standing on loot can make room normally")
			await send({"kind":"move", "actor":actor.id, "x":free[0].x, "y":free[0].y})
	for step in 20:
		var tomas := named("Tomas Reed")
		if int(tomas.x) == int(found.x) and int(tomas.y) == int(found.y): break
		var best := {}
		var distance := absi(tomas.x-found.x) + absi(tomas.y-found.y)
		for tile: Dictionary in game.query_tiles("move", int(tomas.id)):
			var next := absi(tile.x-found.x) + absi(tile.y-found.y)
			if next < distance: best = tile; distance = next
		expect(not best.is_empty(), "Tomas has a normal terrain route to the dropped sword")
		await send({"kind":"move", "actor":tomas.id, "x":best.x, "y":best.y})
	await send({"kind":"pick_up", "actor":named("Tomas Reed").id, "ground":found.ground_item_id})
	expect(owned(named("Tomas Reed"), sword) == 1 and owned(named("Tomas Reed"), bell) == 1, "spatial pickup adds the sword without replacing Tomas's bell")
	expect(sword in named("Tomas Reed").weapons, "the acquired blade is usable by the unchanged Healer role through personal affinities")
	proceed = false
	await send({"kind":"finish_collection"})
	await until(func(): return ended and game.checkpoint_save_ready())
	await until(func(): return int(management.get("gold", 0)) == gold)
	expect(management.store.is_empty(), "equipment never teleports into shared supplies")
	expect(game.save_checkpoint(1), "normal between-battle save succeeds after collection")
	proceed = true
	expect(game.submit_management("proceed"), "normal company Continue enters Deepwood")
	await until(func(): return battle_count == 2)
	verify_packs(tomas_member)
	game.stop()
	game = ClassDB.instantiate("GrandleonSession")
	state = {}; battle_count = 0; ended = false
	expect(game.start_package(bytes, "steel_and_cedar", players, scope, "en"), "fresh native worker reopens saved demo")
	await until(func(): return battle_count == 1)
	verify_packs(tomas_member)
	expect(int(management.get("gold", 0)) == gold, "earned gold survives the real checkpoint reload")
	game.stop()
	print("STEEL_CEDAR_LOOT_MODE_PASS players=", players, " commands=", sequence)
func run() -> void:
	var source_path := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--source="): source_path = argument.trim_prefix("--source=")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var compiled: Dictionary = compiler.compile_source_json(FileAccess.get_file_as_string(source_path))
	expect(compiled.get("ok", false), "real authored demo compiles: " + str(compiled.get("diagnostics", [])))
	for players in [1, 2]: await play(compiled.package, players)
	print("STEEL_CEDAR_LOOT_PASS checks=", checks)
	root.get_node("PCAudio").request_quit(0)
