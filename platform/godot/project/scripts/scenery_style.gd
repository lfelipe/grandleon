# SPDX-License-Identifier: MIT
extends RefCounted

static var _document: Dictionary = {}

static func document() -> Dictionary:
	if _document.is_empty():
		_document = JSON.parse_string(FileAccess.get_file_as_string("res://content/scenery_style.json"))
	return _document

static func house_at(anchor: Vector2i) -> Dictionary:
	var choices: Array = document().houses.placements
	# Mix higher bits so four-cell-spaced cottages do not all repeat one house.
	var seed_value := anchor.x * 17 + anchor.y * 31
	return choices[posmod(seed_value ^ (seed_value >> 3), choices.size())]
