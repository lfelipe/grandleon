# SPDX-License-Identifier: MIT
extends Node

var package_bytes := PackedByteArray()
var campaign_key := ""
var draft_preview := false


func stage_package(bytes: PackedByteArray, campaign: String, preview := false) -> void:
	package_bytes = bytes.duplicate()
	campaign_key = campaign
	draft_preview = preview


func take_package() -> Dictionary:
	if package_bytes.is_empty() or campaign_key.is_empty():
		return {}
	var staged := {
		"package": package_bytes,
		"campaign": campaign_key,
		"draft_preview": draft_preview,
	}
	clear()
	return staged


func clear() -> void:
	package_bytes = PackedByteArray()
	campaign_key = ""
	draft_preview = false
