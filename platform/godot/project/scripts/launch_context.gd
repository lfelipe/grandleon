# SPDX-License-Identifier: MIT
extends Node

var package_bytes := PackedByteArray()
var campaign_key := ""
var player_count := 2
var draft_preview := false
var preview_return_scene := ""
var preview_label := ""
var game_language := ""


func stage_package(
	bytes: PackedByteArray,
	campaign: String,
	preview := false,
	players := 2,
	return_scene := "",
	label := "",
	language := ""
) -> void:
	package_bytes = bytes.duplicate()
	campaign_key = campaign
	player_count = clampi(players, 1, 2)
	draft_preview = preview
	preview_return_scene = return_scene if preview else ""
	preview_label = label if preview else ""
	game_language = language


func take_package() -> Dictionary:
	if package_bytes.is_empty() or campaign_key.is_empty():
		return {}
	var staged := {
		"package": package_bytes,
		"campaign": campaign_key,
		"players": player_count,
		"draft_preview": draft_preview,
		"preview_return_scene": preview_return_scene,
		"preview_label": preview_label,
		"language": game_language,
	}
	clear()
	return staged


func clear() -> void:
	package_bytes = PackedByteArray()
	campaign_key = ""
	player_count = 2
	draft_preview = false
	preview_return_scene = ""
	preview_label = ""
	game_language = ""
