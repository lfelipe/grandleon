# SPDX-License-Identifier: MIT
# Shared normal-input campaign play for native integration tests.
extends SceneTree

func expect(condition: bool, message: String) -> bool:
	if not condition:
		push_error(message)
		quit(1)
	return condition

func _play_to_checkpoint(session) -> Dictionary:
	var deadline := Time.get_ticks_msec() + 30000
	var snapshot: Dictionary = {}
	var abilities := {}
	var waiting := false
	var commands := 0
	var moves := 0
	var won := false
	while Time.get_ticks_msec() < deadline and commands < 300:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "management":
				if session.checkpoint_save_ready():
					if not expect(won, "Checkpoint must follow an ordinary first-battle victory"):
						return {}
					if not expect(session.save_checkpoint(1), "Normal between-battle checkpoint must save"):
						return {}
					return {"ok": true, "commands": commands, "moves": moves}
				if not expect(session.submit_management("proceed"), "Normal company Proceed must start first battle"):
					return {}
			elif event.type == "definitions":
				for ability: Dictionary in event.abilities:
					abilities[ability.id] = ability
			elif event.type in ["battle_begins", "snapshot"]:
				snapshot = event.snapshot
				waiting = false
			elif event.type == "deployment_begins":
				session.submit_intent("begin")
			elif event.type == "battle_ended":
				won = event.snapshot.outcome == "first_side_won"
				if not expect(won, "Ordinary campaign commands must win the tutorial battle"):
					return {}
				snapshot = {}
			elif event.type in ["session_failed", "refused"]:
				expect(false, "Ordinary campaign command failed: " + JSON.stringify(event))
				return {}
		if won and session.checkpoint_save_ready():
			if not expect(session.save_checkpoint(1), "Victory checkpoint must save between battles"):
				return {}
			return {"ok": true, "commands": commands, "moves": moves}
		if not snapshot.is_empty() and not waiting and snapshot.active_side == "first" and snapshot.outcome == "ongoing":
			var intent := _ordinary_intent(session, snapshot, abilities)
			if not expect(not intent.is_empty(), "An authored player unit must have an ordinary action"):
				return {}
			commands += 1
			if intent.kind == "move": moves += 1
			print("campaign play command ", commands, " ", intent)
			session.submit_intent(intent.kind, intent.actor, intent.get("target", 0), intent.get("x", 0), intent.get("y", 0), intent.get("content", 0), commands)
			waiting = true
	expect(false, "Campaign did not reach a genuine victory checkpoint within the command/time budget")
	return {}

func _ordinary_intent(session, snapshot: Dictionary, abilities: Dictionary) -> Dictionary:
	var allies: Array[Dictionary] = []
	var enemies: Array[Dictionary] = []
	for unit: Dictionary in snapshot.units:
		if not unit.on_board: continue
		if unit.side == "first": allies.append(unit)
		else: enemies.append(unit)
	for actor: Dictionary in allies:
		if actor.spent_action_points >= actor.action_points: continue
		for id: int in actor.abilities:
			var ability: Dictionary = abilities.get(id, {})
			if ability.get("kind", "") != "restore": continue
			var tiles: Array = session.query_tiles("ability", actor.id, id)
			for ally: Dictionary in allies:
				if ally.maximum_health - ally.health < 4: continue
				for tile: Dictionary in tiles:
					if tile.x == ally.x and tile.y == ally.y:
						return {"kind": "ability", "actor": actor.id, "target": ally.id, "content": id}
	for actor: Dictionary in allies:
		if actor.spent_action_points >= actor.action_points: continue
		for enemy: Dictionary in enemies:
			var forecast: Dictionary = session.forecast_attack(actor.id, enemy.id)
			if forecast.get("available", false) and forecast.get("damage", 0) > 0:
				return {"kind": "attack", "actor": actor.id, "target": enemy.id, "content": actor.weapons[0] if not actor.weapons.is_empty() else 0}
	for actor: Dictionary in allies:
		if actor.spent_action_points >= actor.action_points: continue
		var best_distance := 100000
		for enemy: Dictionary in enemies:
			best_distance = mini(best_distance, absi(actor.x - enemy.x) + absi(actor.y - enemy.y))
		var best: Dictionary = {}
		for tile: Dictionary in session.query_tiles("move", actor.id):
			var distance := 100000
			for enemy: Dictionary in enemies:
				distance = mini(distance, absi(tile.x - enemy.x) + absi(tile.y - enemy.y))
			if distance < best_distance:
				best_distance = distance
				best = tile
		if not best.is_empty():
			return {"kind": "move", "actor": actor.id, "x": best.x, "y": best.y}
	for actor: Dictionary in allies:
		if actor.spent_action_points < actor.action_points:
			return {"kind": "wait", "actor": actor.id}
	return {}

