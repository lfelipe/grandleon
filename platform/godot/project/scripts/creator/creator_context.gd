# SPDX-License-Identifier: MIT
extends Node

var project_id := ""
var campaign_id := ""
var map_id := ""
var encounter_id := ""
var dialogue_id := ""
var map_action := ""
var diagnostic_focus: Dictionary = {}


func open_project(value: String) -> void:
	project_id = value
	campaign_id = ""
	map_id = ""
	encounter_id = ""
	dialogue_id = ""
	map_action = ""
	diagnostic_focus = {}


func choose_campaign(value: String) -> void:
	campaign_id = value


func choose_map(value: String, encounter := "") -> void:
	map_id = value
	encounter_id = encounter


func choose_dialogue(value: String) -> void:
	dialogue_id = value


func request_map_action(value: String) -> void:
	map_action = value


func take_map_action() -> String:
	var value := map_action
	map_action = ""
	return value


func set_diagnostic_focus(value: Dictionary) -> void:
	diagnostic_focus = value.duplicate(true)


func take_diagnostic_focus() -> Dictionary:
	var value := diagnostic_focus
	diagnostic_focus = {}
	return value


func clear() -> void:
	project_id = ""
	campaign_id = ""
	map_id = ""
	encounter_id = ""
	dialogue_id = ""
	map_action = ""
	diagnostic_focus = {}
