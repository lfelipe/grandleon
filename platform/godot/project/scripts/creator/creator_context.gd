# SPDX-License-Identifier: MIT
extends Node

var project_id := ""


func open_project(value: String) -> void:
	project_id = value


func clear() -> void:
	project_id = ""
