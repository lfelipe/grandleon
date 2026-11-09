# SPDX-License-Identifier: MIT
extends Node

const PLAYER_COUNT := 10
const MUSIC_THEMES := ["cedar_overture", "banners_afar", "willow_village",
	"stone_and_shadow", "drums_at_the_gate", "first_light"]

var _streams: Dictionary = {}
var _players: Array[AudioStreamPlayer] = []
var _next_player := 0
var _effects_level := 1.0
var _music_level := 0.45
var _music_player: AudioStreamPlayer
var _music_theme := ""
var _bound_buttons: Dictionary = {}


func _ready() -> void:
	_build_streams()
	_music_player = AudioStreamPlayer.new()
	_music_player.bus = &"Master"
	add_child(_music_player)
	_music_player.finished.connect(func():
		if not _music_theme.is_empty():
			_music_player.play()
	)
	for unused in PLAYER_COUNT:
		var player := AudioStreamPlayer.new()
		player.bus = &"Master"
		add_child(player)
		_players.append(player)
	get_tree().node_added.connect(_on_node_added)
	call_deferred("_bind_existing_buttons")


func set_effects_level(value: float) -> void:
	_effects_level = clampf(value, 0.0, 1.0)
	for player: AudioStreamPlayer in _players:
		player.volume_db = linear_to_db(maxf(_effects_level, 0.00001))
	if is_zero_approx(_effects_level):
		for player: AudioStreamPlayer in _players:
			player.stop()


func _exit_tree() -> void:
	for player: AudioStreamPlayer in _players:
		player.stop()
		player.stream = null
	if is_instance_valid(_music_player):
		_music_player.stop()
		_music_player.stream = null
	_streams.clear()


func effects_level() -> float:
	return _effects_level


func set_music_level(value: float) -> void:
	_music_level = clampf(value, 0.0, 1.0)
	if is_instance_valid(_music_player):
		_music_player.volume_db = linear_to_db(maxf(_music_level, 0.00001))
		_music_player.stream_paused = is_zero_approx(_music_level)


func music_level() -> float:
	return _music_level


func play_music(theme := "cedar_overture") -> bool:
	if theme not in MUSIC_THEMES or not is_instance_valid(_music_player):
		return false
	if theme == _music_theme:
		return true
	var stream = load("res://audio/music/%s.ogg" % theme)
	if not stream is AudioStream:
		return false
	_music_theme = theme
	_music_player.stream = stream
	_music_player.volume_db = linear_to_db(maxf(_music_level, 0.00001))
	_music_player.play()
	_music_player.stream_paused = is_zero_approx(_music_level)
	return true


func level_name() -> String:
	if is_zero_approx(_effects_level):
		return "OFF"
	if _effects_level < 0.75:
		return "LOW"
	return "FULL"


func play_cue(cue: String) -> bool:
	if is_zero_approx(_effects_level) or not _streams.has(cue) or _players.is_empty():
		return false
	var player := _players[_next_player]
	_next_player = (_next_player + 1) % _players.size()
	player.stop()
	player.stream = _streams[cue]
	player.volume_db = linear_to_db(_effects_level)
	player.play()
	return true


func present_events(events: Array, action: String, content_id := 0,
	definitions: Dictionary = {}, snapshot: Dictionary = {}) -> void:
	for cue: String in cues_for_events(events, action, content_id, definitions, snapshot):
		play_cue(cue)


func cues_for_events(events: Array, action: String, content_id := 0,
	definitions: Dictionary = {}, snapshot: Dictionary = {}) -> Array[String]:
	var cues: Dictionary = {}
	var initial_actor := 0
	for event: Dictionary in events:
		if String(event.get("kind", "")) in ["unit_damaged", "attack_missed", "unit_restored"]:
			initial_actor = int(event.get("related_unit_id", 0))
			break
	for event: Dictionary in events:
		match String(event.get("kind", "")):
			"unit_moved":
				cues["move"] = true
			"unit_damaged":
				cues[_combat_cue(event, action, content_id, definitions, snapshot, initial_actor)] = true
				cues["impact"] = true
			"attack_missed":
				cues[_combat_cue(event, action, content_id, definitions, snapshot, initial_actor)] = true
				cues["miss"] = true
			"unit_restored":
				cues["healing" if not definitions.is_empty() else "ability"] = true
				cues["restore"] = true
			"item_used":
				cues["item"] = true
			"unit_talked":
				cues["talk"] = true
			"unit_defeated":
				cues["defeat"] = true
			"unit_endured":
				cues["endure"] = true
	var result: Array[String] = []
	for cue: String in cues:
		result.append(cue)
	result.sort()
	return result


func present_combat_beat(beat: Dictionary, action: String, content_id: int,
	impact: bool, definitions: Dictionary) -> void:
	if impact:
		play_cue("restore" if bool(beat.get("restored", false)) else
			"impact" if bool(beat.get("landed", false)) else "miss")
		if bool(beat.get("defeated", false)):
			play_cue("defeat")
		elif bool(beat.get("endured", false)):
			play_cue("endure")
		return
	if bool(beat.get("restored", false)):
		play_cue("healing")
		return
	var actor: Dictionary = beat.get("actor", {})
	var counter := bool(beat.get("counter", false))
	play_cue(_combat_cue(beat.get("event", {}), "attack" if counter else action,
		0 if counter else content_id, definitions, {"units": [actor]}, int(actor.get("id", 0))))


func _combat_cue(event: Dictionary, action: String, content_id: int,
	definitions: Dictionary, snapshot: Dictionary, initial_actor: int) -> String:
	# Content identities and rules drive this, never translated display names.
	var actor_id := int(event.get("related_unit_id", 0))
	if action == "ability" and actor_id == initial_actor:
		for ability: Dictionary in definitions.get("abilities", []):
			if int(ability.get("id", 0)) == content_id:
				if String(ability.get("kind", "")) == "restore":
					return "healing"
				return "arcane" if String(ability.get("damage_type", "physical")) == "magical" else "heavy"
		return "ability"
	var weapon_id := content_id if action == "attack" and actor_id == initial_actor else 0
	if weapon_id == 0:
		for unit: Dictionary in snapshot.get("units", []):
			if int(unit.get("id", 0)) == actor_id:
				var weapons: Array = unit.get("weapons", [])
				if not weapons.is_empty():
					weapon_id = int(weapons[0])
				break
	for weapon: Dictionary in definitions.get("weapons", []):
		if int(weapon.get("id", 0)) == weapon_id:
			return "bow" if int(weapon.get("minimum_reach", 1)) > 1 else "blade"
	return "attack"


func available_cues() -> Array[String]:
	var result: Array[String] = []
	for cue: String in _streams:
		result.append(cue)
	result.sort()
	return result


func cue_length_msec(cue: String) -> int:
	if not _streams.has(cue):
		return 0
	return roundi((_streams[cue] as AudioStreamWAV).get_length() * 1000.0)


func _on_node_added(node: Node) -> void:
	if node is BaseButton:
		call_deferred("_bind_button", node)


func _bind_existing_buttons() -> void:
	_bind_buttons_below(get_tree().root)


func _bind_buttons_below(node: Node) -> void:
	if node is BaseButton:
		_bind_button(node)
	for child: Node in node.get_children():
		_bind_buttons_below(child)


func _bind_button(button: BaseButton) -> void:
	var id := button.get_instance_id()
	if _bound_buttons.has(id) or not is_instance_valid(button):
		return
	_bound_buttons[id] = true
	button.focus_entered.connect(play_cue.bind("focus"))
	button.mouse_entered.connect(_play_button_hover.bind(button))
	button.pressed.connect(play_cue.bind("confirm"))
	button.tree_exiting.connect(_forget_button.bind(id))


func _play_button_hover(button: BaseButton) -> void:
	if not button.disabled and not button.has_focus():
		play_cue("focus")


func _forget_button(id: int) -> void:
	_bound_buttons.erase(id)


func _build_streams() -> void:
	for cue: String in ["focus", "confirm", "cancel", "refuse", "dialogue",
		"move", "attack", "ability", "impact", "miss", "restore", "item",
		"talk", "defeat", "endure", "victory", "blade", "bow", "heavy", "arcane", "healing"]:
		_streams[cue] = load("res://audio/sfx/%s.wav" % cue)
