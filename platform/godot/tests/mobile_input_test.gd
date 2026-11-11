# SPDX-License-Identifier: MIT
extends SceneTree
const Gestures = preload("res://scripts/mobile_input.gd")
const Layout = preload("res://scripts/mobile_layout.gd")
var checks := 0
func expect(condition: bool, label: String) -> void:
	checks += 1
	if not condition:
		push_error(label)
		quit(1)
func touch(index: int, position: Vector2, pressed: bool, canceled := false) -> InputEventScreenTouch:
	var event := InputEventScreenTouch.new()
	event.index = index
	event.position = position
	event.pressed = pressed
	event.canceled = canceled
	return event
func drag(index: int, position: Vector2) -> InputEventScreenDrag:
	var event := InputEventScreenDrag.new()
	event.index = index
	event.position = position
	return event
func _init() -> void:
	var gestures := Gestures.new()
	expect(gestures.handle(touch(0, Vector2(100, 100), true)).is_empty(), "press must never issue command")
	expect(gestures.handle(drag(0, Vector2(105, 100))).is_empty(), "small finger jitter stays a tap")
	expect(gestures.handle(touch(0, Vector2(105, 100), false)).get("kind") == "tap", "tap occurs once on release")
	expect(gestures.handle(touch(0, Vector2(105, 100), false)).is_empty(), "duplicate release ignored")
	gestures.handle(touch(0, Vector2(100, 100), true))
	expect(gestures.handle(drag(0, Vector2(150, 100))).get("kind") == "pan", "drag pans")
	gestures.handle(drag(0, Vector2(100, 100)))
	expect(gestures.handle(touch(0, Vector2(100, 100), false)).is_empty(), "drag returning to start never becomes tap")
	gestures.handle(touch(0, Vector2(100, 100), true))
	gestures.handle(touch(1, Vector2(200, 100), true))
	var zoom: Dictionary = gestures.handle(drag(1, Vector2(250, 100)))
	expect(zoom.get("kind") == "zoom" and is_equal_approx(zoom.factor, 1.5), "real two-finger separation zooms")
	expect(gestures.handle(touch(1, Vector2(250, 100), false)).is_empty(), "first pinch release never taps")
	expect(gestures.handle(drag(0, Vector2(120, 100))).get("kind") == "pan", "remaining finger continues without jumping")
	expect(gestures.handle(touch(0, Vector2(120, 100), false)).is_empty(), "last pinch release never taps")
	gestures.handle(touch(7, Vector2(300, 100), true))
	expect(gestures.handle(touch(7, Vector2(300, 100), false, true)).is_empty(), "OS-canceled touch never acts")
	gestures.handle(touch(2, Vector2(100, 100), true))
	gestures.reset()
	expect(gestures.handle(touch(2, Vector2(100, 100), false)).is_empty(), "background reset discards held contact")
	expect(gestures.handle(drag(99, Vector2(2, 2))).is_empty(), "untracked drag ignored")
	gestures.handle(touch(0, Vector2(100, 100), true))
	expect(gestures.handle(touch(0, Vector2(100, 100), false)).get("kind") == "tap", "new gesture after cancel works")
	expect(Layout.safe_rect(Vector2(1920, 1080), Vector2(1920, 1080), Rect2(80, 24, 1760, 1032)) == Rect2(80, 24, 1760, 1032), "notch/navigation bounds preserved")
	expect(Layout.safe_rect(Vector2(1920, 1080), Vector2(2400, 1080), Rect2(120, 0, 2160, 1080)) == Rect2(0, 0, 1920, 1080), "letterboxing lies outside logical canvas")
	expect(Layout.safe_rect(Vector2(1920, 1080), Vector2.ZERO, Rect2()) == Rect2(0, 0, 1920, 1080), "missing display metadata keeps usable viewport")
	expect(Layout.safe_rect(Vector2(2400, 1080), Vector2(2400, 1080), Rect2(128, 0, 2272, 1080)) == Rect2(128, 0, 2272, 1080), "expanded ultrawide viewport retains actual notch inset")
	print("MOBILE_INPUT_PASS checks=%d real_screen_events=true tap_drag_pinch=true safe_area=true" % checks)
	quit()
