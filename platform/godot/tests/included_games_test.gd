# SPDX-License-Identifier: MIT
extends SceneTree

const LaunchContextScript = preload("res://scripts/launch_context.gd")

var failures := 0


func _init() -> void:
	var source := FileAccess.get_file_as_string("res://content/included_games.json")
	var parsed := JSON.new()
	_expect("included-game manifest parses", parsed.parse(source), OK)
	var games: Array = parsed.data.get("games", []) if parsed.data is Dictionary else []
	_expect("manifest has solo and co-op campaigns", games.size(), 2)
	if games.size() == 2:
		_expect("solo campaign is Tarnholt", games[0].campaign, "tarnholt_line")
		_expect("solo campaign requests one player", str(games[0].playerMode), "one")
		_expect("co-op campaign is Emberbound", games[1].campaign, "emberbound")
		_expect("co-op campaign lets players choose", str(games[1].playerMode), "choice")
		for game: Dictionary in games:
			_expect(
				"compiled example exists: %s" % game.title,
				FileAccess.file_exists(str(game.path)),
				true
			)

	var context = LaunchContextScript.new()
	context.stage_package(
		PackedByteArray([1, 2, 3]), "tarnholt_line", true, 1,
		"res://campaign_editor.tscn", "Cedar Watch"
	)
	var staged: Dictionary = context.take_package()
	_expect("staged campaign retained", staged.campaign, "tarnholt_line")
	_expect("staged one-player count retained", int(staged.players), 1)
	_expect("staged package retained", staged.package, PackedByteArray([1, 2, 3]))
	_expect("staged editor return retained", staged.preview_return_scene, "res://campaign_editor.tscn")
	_expect("staged battle label retained", staged.preview_label, "Cedar Watch")
	_expect("taking a package clears it", context.take_package().is_empty(), true)
	context.stage_package(PackedByteArray([4]), "emberbound", false, 99)
	var ordinary: Dictionary = context.take_package()
	_expect("staged count is bounded", int(ordinary.players), 2)
	_expect("ordinary launch has no editor return", ordinary.preview_return_scene, "")
	context.free()

	if failures == 0:
		print("included campaign manifest smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
