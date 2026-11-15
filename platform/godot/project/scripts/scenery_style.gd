# SPDX-License-Identifier: MIT
extends RefCounted

static var _document: Dictionary = {}

static func document() -> Dictionary:
	if _document.is_empty():
		_document = JSON.parse_string(FileAccess.get_file_as_string("res://content/scenery_style.json"))
	return _document

static func house_at(anchor: Vector2i, family := "medieval") -> Dictionary:
	var choices: Array = document().houses.placements
	# Mix higher bits so four-cell-spaced cottages do not all repeat one house.
	var seed_value := anchor.x * 17 + anchor.y * 31
	var choice: Dictionary = choices[posmod(seed_value ^ (seed_value >> 3), choices.size())].duplicate()
	var drawings: Array = document().houses.families.get(family, document().houses.families.medieval)
	choice.drawing = drawings[int(choice.drawing) % drawings.size()]
	return choice

static func is_house_kind(kind: String) -> bool:
	return kind == "village_house" or (kind.ends_with("_house") and document().houses.families.has(kind.trim_suffix("_house")))
