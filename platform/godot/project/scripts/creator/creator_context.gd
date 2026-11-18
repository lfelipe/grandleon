# SPDX-License-Identifier: MIT
extends Node

var project_id := ""
var campaign_id := ""
var map_id := ""
var encounter_id := ""
var dialogue_id := ""
var story_target: Dictionary = {}
var story_action := ""
var story_return_scene := ""
var member_id := ""
var army_action := ""
var guided_workflow := false
var hub_action := ""
var map_action := ""
var map_preview_players := 1
var diagnostic_focus: Dictionary = {}


func open_project(value: String) -> void:
	project_id = value
	campaign_id = ""
	map_id = ""
	encounter_id = ""
	dialogue_id = ""
	story_target = {}
	story_action = ""
	story_return_scene = ""
	member_id = ""
	army_action = ""
	guided_workflow = false
	hub_action = ""
	map_action = ""
	map_preview_players = 1
	diagnostic_focus = {}


func choose_campaign(value: String) -> void:
	campaign_id = value


func choose_map(value: String, encounter := "") -> void:
	map_id = value
	encounter_id = encounter


func choose_dialogue(value: String) -> void:
	dialogue_id = value
	story_target = {}
	story_action = ""
	story_return_scene = ""


func request_story(
	campaign: String, node: String, dialogue := "", create := false, return_scene := ""
) -> void:
	choose_dialogue(dialogue)
	story_target = {"campaign_id": campaign, "node_id": node}
	story_action = "create" if create else "edit"
	story_return_scene = return_scene


func take_story_action() -> String:
	var value := story_action
	story_action = ""
	return value


func take_army_action() -> String:
	var value := army_action
	army_action = ""
	return value


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
	story_target = {}
	story_action = ""
	story_return_scene = ""
	member_id = ""
	army_action = ""
	guided_workflow = false
	hub_action = ""
	map_action = ""
	map_preview_players = 1
	diagnostic_focus = {}
