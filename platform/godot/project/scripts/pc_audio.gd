# SPDX-License-Identifier: MIT
extends Node

const SAMPLE_RATE := 44100
const PLAYER_COUNT := 10
const VALID_LEVELS := [0.0, 0.45, 1.0]

var _streams: Dictionary = {}
var _players: Array[AudioStreamPlayer] = []
var _next_player := 0
var _effects_level := 1.0
var _bound_buttons: Dictionary = {}


func _ready() -> void:
	_build_streams()
	for unused in PLAYER_COUNT:
		var player := AudioStreamPlayer.new()
		player.bus = &"Master"
		add_child(player)
		_players.append(player)
	get_tree().node_added.connect(_on_node_added)
	call_deferred("_bind_existing_buttons")


func set_effects_level(value: float) -> void:
	_effects_level = _nearest_level(value)
	if is_zero_approx(_effects_level):
		for player: AudioStreamPlayer in _players:
			player.stop()


func effects_level() -> float:
	return _effects_level


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


func present_events(events: Array, action: String) -> void:
	for cue: String in cues_for_events(events, action):
		play_cue(cue)


func cues_for_events(events: Array, action: String) -> Array[String]:
	var cues: Dictionary = {}
	for event: Dictionary in events:
		match String(event.get("kind", "")):
			"unit_moved":
				cues["move"] = true
			"unit_damaged":
				cues["ability" if action == "ability" else "attack"] = true
				cues["impact"] = true
			"attack_missed":
				cues["ability" if action == "ability" else "attack"] = true
				cues["miss"] = true
			"unit_restored":
				cues["ability"] = true
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


func available_cues() -> Array[String]:
	var result: Array[String] = []
	for cue: String in _streams:
		result.append(cue)
	result.sort()
	return result


func cue_length_msec(cue: String) -> int:
	if not _streams.has(cue):
		return 0
	return roundi(float((_streams[cue] as AudioStreamWAV).data.size()) * 500.0 / SAMPLE_RATE)


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


func _nearest_level(value: float) -> float:
	var result: float = VALID_LEVELS[0]
	var distance := absf(value - result)
	for level: float in VALID_LEVELS:
		var candidate := absf(value - level)
		if candidate < distance:
			distance = candidate
			result = level
	return result


func _build_streams() -> void:
	_streams = {
		"focus": _synth("focus", 0.045),
		"confirm": _synth("confirm", 0.115),
		"cancel": _synth("cancel", 0.105),
		"refuse": _synth("refuse", 0.18),
		"dialogue": _synth("dialogue", 0.055),
		"move": _synth("move", 0.11),
		"attack": _synth("attack", 0.19),
		"ability": _synth("ability", 0.38),
		"impact": _synth("impact", 0.16),
		"miss": _synth("miss", 0.21),
		"restore": _synth("restore", 0.34),
		"item": _synth("item", 0.22),
		"talk": _synth("talk", 0.14),
		"defeat": _synth("defeat", 0.48),
		"endure": _synth("endure", 0.26),
	}


func _synth(cue: String, duration: float) -> AudioStreamWAV:
	var count := maxi(1, roundi(duration * SAMPLE_RATE))
	var bytes := PackedByteArray()
	bytes.resize(count * 2)
	var noise_state := 0x42d00d
	for index in count:
		var t := float(index) / SAMPLE_RATE
		var progress := t / duration
		noise_state = int((noise_state * 1103515245 + 12345) & 0x7fffffff)
		var noise := float(noise_state % 65536) / 32767.5 - 1.0
		var sample := _sample(cue, t, progress, noise)
		var envelope := _envelope(progress)
		var value := clampi(roundi(sample * envelope * 24500.0), -32767, 32767)
		bytes[index * 2] = value & 0xff
		bytes[index * 2 + 1] = (value >> 8) & 0xff
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = SAMPLE_RATE
	stream.stereo = false
	stream.data = bytes
	return stream


func _sample(cue: String, t: float, progress: float, noise: float) -> float:
	var tau := TAU
	match cue:
		"focus":
			return sin(tau * (1120.0 + 540.0 * progress) * t) * 0.30
		"confirm":
			var note := 660.0 if progress < 0.48 else 990.0
			return sin(tau * note * t) * 0.34 + sin(tau * note * 2.0 * t) * 0.08
		"cancel":
			return sin(tau * (740.0 - 330.0 * progress) * t) * 0.34
		"refuse":
			return sin(tau * (180.0 - 35.0 * progress) * t) * 0.34 + noise * 0.10
		"dialogue":
			return sin(tau * 520.0 * t) * 0.21 + sin(tau * 780.0 * t) * 0.08
		"move":
			return sin(tau * (105.0 - 30.0 * progress) * t) * 0.24 + noise * 0.13
		"attack":
			return noise * (0.48 - 0.25 * progress) + sin(tau * (210.0 - 90.0 * progress) * t) * 0.25
		"ability":
			var sweep := 260.0 + 780.0 * progress
			return sin(tau * sweep * t) * 0.26 + sin(tau * sweep * 1.5 * t) * 0.13
		"impact":
			return sin(tau * (115.0 - 55.0 * progress) * t) * 0.43 + noise * 0.22
		"miss":
			return noise * 0.19 + sin(tau * (920.0 - 510.0 * progress) * t) * 0.19
		"restore":
			var heal_note: float = [392.0, 523.25, 659.25][mini(2, floori(progress * 3.0))]
			return sin(tau * heal_note * t) * 0.29 + sin(tau * heal_note * 2.0 * t) * 0.07
		"item":
			return sin(tau * (420.0 + 260.0 * progress) * t) * 0.27 + noise * 0.06
		"talk":
			return sin(tau * (330.0 + 80.0 * sin(tau * 9.0 * t)) * t) * 0.22
		"defeat":
			var fall := 330.0 - 210.0 * progress
			return sin(tau * fall * t) * 0.34 + sin(tau * fall * 0.5 * t) * 0.18
		"endure":
			return sin(tau * (165.0 + 35.0 * progress) * t) * 0.34 + noise * 0.08
	return 0.0


func _envelope(progress: float) -> float:
	var attack := smoothstep(0.0, 0.06, progress)
	var release := 1.0 - smoothstep(0.58, 1.0, progress)
	return attack * release
