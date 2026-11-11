# SPDX-License-Identifier: MIT
# Keep desktop layouts intact. Android forms gain readable type and bounded,
# focus-following scroll areas rather than shrinking an entire form to fit.
extends Node
const MobileLayout = preload("res://scripts/mobile_layout.gd")
var screen: Control
var pending := false

static func metrics(units_per_dp: float, canvas_scale := 1.0) -> Dictionary:
	var unit := maxf(0.5, units_per_dp) / maxf(0.1, canvas_scale)
	return {"touch": ceilf(48 * unit), "font": roundi(16 * unit), "body": roundi(14 * unit), "unit": unit}

static func install(control: Control) -> void:
	if not OS.has_feature("android"): return
	var adapter := control.get_node_or_null("MobileCreatorLayout")
	if adapter == null:
		adapter = load("res://scripts/creator/mobile_creator_layout.gd").new()
		adapter.name = "MobileCreatorLayout"
		adapter.screen = control
		control.add_child(adapter)
	adapter.schedule()

func _ready() -> void:
	get_tree().node_added.connect(_node_added)

func _node_added(node: Node) -> void:
	if screen != null and screen.is_ancestor_of(node): schedule()

func schedule() -> void:
	if pending: return
	pending = true
	call_deferred("_apply")

func _apply() -> void:
	pending = false
	if not is_instance_valid(screen): return
	apply_tree(screen, metrics(MobileLayout.units_per_dp(screen.get_viewport()), screen.scale.x))

static func apply_tree(node: Node, sizes: Dictionary) -> void:
	for child: Node in node.get_children(): apply_tree(child, sizes)
	if node is Control and node.get_meta("mobile_tile", false): return
	if node is Button or node is LineEdit or node is TextEdit or node is SpinBox:
		node.add_theme_font_size_override("font_size", maxi(int(_original(node, "font", node.get_theme_font_size("font_size"))), sizes.font))
		node.custom_minimum_size.y = maxf(float(_original(node, "min_y", node.custom_minimum_size.y)), sizes.touch)
		if node is BaseButton:
			node.custom_minimum_size.x = maxf(float(_original(node, "min_x", node.custom_minimum_size.x)), sizes.touch)
		if node is OptionButton:
			var popup: PopupMenu = node.get_popup()
			popup.add_theme_font_size_override("font_size", sizes.font)
			popup.add_theme_constant_override("v_separation", roundi(32 * sizes.unit))
	elif node is Label or node is RichTextLabel:
		node.add_theme_font_size_override("font_size" if node is Label else "normal_font_size", maxi(int(_original(node, "font", node.get_theme_font_size("font_size" if node is Label else "normal_font_size"))), sizes.body))
		if node is RichTextLabel:
			for variant: String in ["bold_font_size", "italics_font_size", "bold_italics_font_size", "mono_font_size"]:
				node.add_theme_font_size_override(variant, maxi(int(_original(node, variant, node.get_theme_font_size(variant))), sizes.body))
	elif node is HSlider or node is VSlider:
		node.custom_minimum_size.y = maxf(float(_original(node, "min_y", node.custom_minimum_size.y)), sizes.touch)
	if node is ScrollContainer:
		node.follow_focus = true
	if node is PanelContainer and not node.get_parent() is Container and node.get_child_count() == 1:
		var contents := node.get_child(0)
		if contents is Container and not contents is ScrollContainer:
			var scroll := ScrollContainer.new()
			scroll.follow_focus = true
			node.remove_child(contents)
			node.add_child(scroll)
			scroll.add_child(contents)
			contents.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			contents.size_flags_vertical = Control.SIZE_EXPAND_FILL


static func _original(node: Node, key: String, value: Variant) -> Variant:
	var stored: Dictionary = node.get_meta("mobile_original_metrics", {})
	if not stored.has(key):
		stored[key] = value
		node.set_meta("mobile_original_metrics", stored)
	return stored[key]
