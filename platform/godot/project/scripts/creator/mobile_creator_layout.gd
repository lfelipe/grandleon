# SPDX-License-Identifier: MIT
# Keep desktop layouts intact. Android forms gain readable type and bounded,
# focus-following scroll areas rather than shrinking an entire form to fit.
extends Node
const MobileLayout = preload("res://scripts/mobile_layout.gd")
var screen: Control
var pending := false
var last_keyboard_height := 0

static func metrics(units_per_dp: float, canvas_scale := 1.0) -> Dictionary:
	var unit := maxf(0.5, units_per_dp) / maxf(0.1, canvas_scale)
	return {"touch": ceilf(48 * unit), "font": roundi(16 * unit), "body": roundi(14 * unit), "unit": unit}

static func install(control: Control) -> void:
	if not OS.has_feature("android") or not control.is_inside_tree(): return
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
	if not is_instance_valid(screen) or not screen.is_inside_tree(): return
	apply_tree(screen, metrics(MobileLayout.units_per_dp(screen.get_viewport()), screen.scale.x))

static func apply_tree(node: Node, sizes: Dictionary) -> void:
	for child: Node in node.get_children(): apply_tree(child, sizes)
	if node is Control and node.get_meta("mobile_tile", false): return
	if node is Button: node.mouse_filter = Control.MOUSE_FILTER_PASS
	if node is Button or node is LineEdit or node is TextEdit or node is SpinBox:
		node.add_theme_font_size_override("font_size", maxi(int(_original(node, "font", node.get_theme_font_size("font_size"))), sizes.font))
		node.custom_minimum_size.y = maxf(float(_original(node, "min_y", node.custom_minimum_size.y)), sizes.touch)
		if node is BaseButton:
			node.custom_minimum_size.x = maxf(float(_original(node, "min_x", node.custom_minimum_size.x)), sizes.touch)
		if node is OptionButton:
			# Long authored chapter/unit names must not widen the whole form.
			# Keep the full item text in the choice menu, within a bounded field.
			node.fit_to_longest_item = false
			node.clip_text = true
			var popup: PopupMenu = node.get_popup()
			popup.add_theme_font_size_override("font_size", sizes.font)
			popup.add_theme_constant_override("v_separation", roundi(32 * sizes.unit))
	elif node is Label or node is RichTextLabel:
		var parent := node.get_parent()
		if node is Label and parent != null and (parent is VBoxContainer or parent.get_meta("mobile_wrap_actions", false)):
			# Headings and helper text must wrap with the form rather than set
			# an unbounded minimum width when text grows for a dense display.
			node.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			if parent.get_meta("mobile_wrap_actions", false):
				node.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		node.add_theme_font_size_override("font_size" if node is Label else "normal_font_size", maxi(int(_original(node, "font", node.get_theme_font_size("font_size" if node is Label else "normal_font_size"))), sizes.body))
		if node is RichTextLabel:
			for variant: String in ["bold_font_size", "italics_font_size", "bold_italics_font_size", "mono_font_size"]:
				node.add_theme_font_size_override(variant, maxi(int(_original(node, variant, node.get_theme_font_size(variant))), sizes.body))
	elif node is HSlider or node is VSlider:
		node.custom_minimum_size.y = maxf(float(_original(node, "min_y", node.custom_minimum_size.y)), sizes.touch)
	if node is BoxContainer and node.get_meta("mobile_wrap_actions", false):
		var width := 0.0
		var count := 0
		for child: Node in node.get_children():
			if child is Control and child.visible:
				width += child.get_combined_minimum_size().x
				count += 1
		width += maxi(0, count - 1) * node.get_theme_constant("separation")
		# Recompute from the children, so a larger viewport or smaller text can
		# restore the original horizontal row without accumulating overrides.
		node.vertical = width > node.size.x
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


static func keyboard_pan_delta(field: Rect2, keyboard_top: float, padding: float) -> float:
	return maxf(0, field.end.y - keyboard_top + padding)


func _process(_delta: float) -> void:
	if not is_instance_valid(screen) or not screen.is_inside_tree(): return
	var height := DisplayServer.virtual_keyboard_get_height()
	if height <= 0:
		if last_keyboard_height > 0:
			MobileLayout.fit_canvas(screen)
			schedule()
		last_keyboard_height = 0
		return
	last_keyboard_height = height
	var focused := screen.get_viewport().gui_get_focus_owner()
	if not (focused is LineEdit or focused is TextEdit) or not screen.is_ancestor_of(focused): return
	# Android's IME may overlay the window without a viewport resize. Recompute
	# from the original safe canvas every frame, then pan only enough to keep
	# the actual editing field above the keyboard. Do not accumulate offsets.
	MobileLayout.fit_canvas(screen)
	var viewport_size := screen.get_viewport().get_visible_rect().size
	var window := Vector2(DisplayServer.window_get_size())
	var stretch := minf(window.x / maxf(viewport_size.x, 1), window.y / maxf(viewport_size.y, 1))
	var letterbox_y := (window.y - viewport_size.y * stretch) * 0.5
	var keyboard_top := (window.y - height - letterbox_y) / maxf(stretch, 0.1)
	var safe := MobileLayout.current_rect(screen.get_viewport())
	keyboard_top = minf(keyboard_top, safe.end.y)
	var padding := 8 * MobileLayout.units_per_dp(screen.get_viewport())
	screen.position.y -= keyboard_pan_delta(focused.get_global_rect(), keyboard_top, padding)
