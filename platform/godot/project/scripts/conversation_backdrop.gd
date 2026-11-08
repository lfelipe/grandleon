# SPDX-License-Identifier: MIT
class_name GrandleonConversationBackdrop
extends Control

# Draws a resolved layer pack behind the existing modal dialogue panel. The
# lower scrim deliberately encloses that panel's portrait and text rectangle;
# art may add detail around a conversation, but it never competes with words.

const SAFE_RECT := Rect2(140, 640, 1640, 380)

var _selection: Dictionary = {}
var _layer_nodes: Array[TextureRect] = []
var _tone: ColorRect
var _safe_scrim: ColorRect


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false


func present(selection: Dictionary) -> bool:
	if (
		visible
		and String(selection.get("mode", "fallback")) == "layers"
		and selection.get("layer_paths", []) == _selection.get("layer_paths", [])
		and int(selection.get("variant", -1)) == int(_selection.get("variant", -2))
		and String(selection.get("pack_id", "")) == String(_selection.get("pack_id", ""))
	):
		_selection = selection.duplicate(true)
		return true
	clear()
	if String(selection.get("mode", "fallback")) != "layers":
		return false
	var paths_value: Variant = selection.get("layer_paths", [])
	if not paths_value is Array or paths_value.is_empty():
		return false
	var textures: Array[Texture2D] = []
	for value: Variant in paths_value:
		var resource: Resource = load(String(value))
		if not resource is Texture2D:
			clear()
			return false
		textures.append(resource as Texture2D)
	for texture: Texture2D in textures:
		var layer := TextureRect.new()
		layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		layer.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		layer.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		layer.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
		layer.texture = texture
		add_child(layer)
		_layer_nodes.append(layer)
	_tone = ColorRect.new()
	_tone.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_tone.color = Color(0.004, 0.008, 0.014, 0.12)
	_tone.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_tone)
	_safe_scrim = ColorRect.new()
	_safe_scrim.position = SAFE_RECT.position
	_safe_scrim.size = SAFE_RECT.size
	_safe_scrim.color = Color(0.004, 0.009, 0.017, 0.44)
	_safe_scrim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_safe_scrim)
	_selection = selection.duplicate(true)
	visible = true
	return true


func clear() -> void:
	for child: Node in get_children():
		remove_child(child)
		child.free()
	_layer_nodes.clear()
	_tone = null
	_safe_scrim = null
	_selection.clear()
	visible = false


func presentation_state() -> Dictionary:
	return {
		"visible": visible,
		"layer_count": _layer_nodes.size(),
		"safe_rect": SAFE_RECT,
		"has_overview_tone": _tone != null,
		"has_text_safe_scrim": _safe_scrim != null,
		"selection": _selection.duplicate(true),
	}
