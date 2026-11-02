# SPDX-License-Identifier: MIT
extends Node

var package_bytes := PackedByteArray()
var campaign_key := ""


func stage_package(bytes: PackedByteArray, campaign: String) -> void:
	package_bytes = bytes.duplicate()
	campaign_key = campaign


func take_package() -> Dictionary:
	if package_bytes.is_empty() or campaign_key.is_empty():
		return {}
	var staged := {"package": package_bytes, "campaign": campaign_key}
	clear()
	return staged


func clear() -> void:
	package_bytes = PackedByteArray()
	campaign_key = ""
