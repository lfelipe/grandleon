# SPDX-License-Identifier: MIT
extends RefCounted

static func safe_rect(viewport_size: Vector2, window_size: Vector2, safe_pixels: Rect2) -> Rect2:
	var full := Rect2(Vector2.ZERO, viewport_size)
	if viewport_size.x <= 0 or viewport_size.y <= 0 or window_size.x <= 0 or window_size.y <= 0 or not safe_pixels.has_area():
		return full
	var stretch := minf(window_size.x / viewport_size.x, window_size.y / viewport_size.y)
	var letterbox := (window_size - viewport_size * stretch) * 0.5
	return Rect2((safe_pixels.position - letterbox) / stretch, safe_pixels.size / stretch).intersection(full)

static func current_rect(viewport: Viewport) -> Rect2:
	var size := viewport.get_visible_rect().size
	if not OS.has_feature("android"):
		return Rect2(Vector2.ZERO, size)
	var safe := Rect2(DisplayServer.get_display_safe_area())
	safe.position -= Vector2(DisplayServer.window_get_position())
	return safe_rect(size, Vector2(DisplayServer.window_get_size()), safe)

static func units_per_dp(viewport: Viewport) -> float:
	var size := viewport.get_visible_rect().size
	var window := Vector2(DisplayServer.window_get_size())
	var stretch := minf(window.x / maxf(1.0, size.x), window.y / maxf(1.0, size.y))
	return maxf(0.75, float(DisplayServer.screen_get_dpi()) / 160.0 / maxf(0.1, stretch))

static func native_3d_scale(local_to_screen: Transform2D) -> float:
	# The UI uses a logical canvas. Do not shade a larger 3D image than the
	# pixels that canvas actually occupies on a phone. Keep enough samples
	# for both axes when a parent scales them differently.
	return clampf(maxf(local_to_screen.x.length(), local_to_screen.y.length()), 0.25, 1.0)

static func fit_canvas(control: Control, design := Vector2(1920, 1080)) -> void:
	if not OS.has_feature("android"):
		return
	var safe := current_rect(control.get_viewport())
	var factor := minf(safe.size.x / design.x, safe.size.y / design.y)
	control.set_anchors_preset(Control.PRESET_TOP_LEFT)
	control.size = design
	control.position = safe.position + (safe.size - design * factor) * 0.5
	control.scale = Vector2.ONE * factor
