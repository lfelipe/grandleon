# SPDX-License-Identifier: MIT
extends "res://scripts/creator/campaign_editor.gd"
func _ready() -> void:
	pass
func _open_projects() -> void:
	get_parent().remove_child(self)
