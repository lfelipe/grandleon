# SPDX-License-Identifier: MIT
extends Node

signal changed

const CHOICES := ["auto", "low", "medium", "high"]
const SETTINGS_PATH := "user://grandleon/render-quality.cfg"
var choice := "auto"
var recommended := "high"
var settings_path := SETTINGS_PATH


func _ready() -> void:
	recommended = recommend(OS.has_feature("mobile"), RenderingServer.get_video_adapter_name(), int(OS.get_memory_info().get("physical", 0)))
	var config := ConfigFile.new()
	if config.load(settings_path) == OK:
		var saved := str(config.get_value("graphics", "quality", "auto"))
		if saved in CHOICES: choice = saved


static func recommend(mobile: bool, adapter: String, memory_bytes: int) -> String:
	if not mobile: return "high"
	var gpu := adapter.to_lower()
	for entry: String in ["adreno (tm) 610", "adreno (tm) 612", "adreno (tm) 505", "adreno (tm) 506", "mali-g52", "powervr ge"]:
		if gpu.contains(entry): return "low"
	if memory_bytes > 0 and memory_bytes <= 4 * 1024 * 1024 * 1024: return "low"
	if memory_bytes >= 8 * 1024 * 1024 * 1024:
		for entry: String in ["adreno (tm) 740", "adreno (tm) 750", "adreno (tm) 830"]:
			if gpu.contains(entry): return "high"
	return "medium"


static func profile(level: String) -> Dictionary:
	match level:
		"low": return {"scale": 0.6, "shadows": false, "shadow_distance": 40.0}
		"medium": return {"scale": 0.8, "shadows": true, "shadow_distance": 40.0}
		_: return {"scale": 1.0, "shadows": true, "shadow_distance": 100.0}


func effective() -> String:
	if choice != "auto": return choice
	return recommended


func select(value: String) -> bool:
	if value not in CHOICES: return false
	var config := ConfigFile.new()
	config.set_value("graphics", "quality", value)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(settings_path.get_base_dir()))
	if config.save(settings_path) != OK: return false
	choice = value
	changed.emit()
	return true


func make_selector(height: float) -> OptionButton:
	var selector: OptionButton = load("res://scripts/render_quality_selector.gd").new()
	selector.custom_minimum_size.y = height
	selector.add_theme_font_size_override("font_size", 24)
	selector.get_popup().add_theme_font_size_override("font_size", 24)
	return selector
