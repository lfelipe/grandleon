# SPDX-License-Identifier: MIT
# Observe the real home launch decision without changing scenes during UI checks.
extends "res://scripts/home_menu.gd"
var observed_launch: Dictionary = {}

func _stage_package(bytes: PackedByteArray, campaign: String, preview: bool, players: int, scope := "") -> void:
	observed_launch = {"package": bytes, "campaign": campaign, "preview": preview, "players": players, "save_scope": scope}
