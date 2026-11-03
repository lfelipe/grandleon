# SPDX-License-Identifier: MIT
extends SceneTree

var session
var phase := 0
var resumed_manual := false
var deadline_msec := 0
var finished := false


func _initialize() -> void:
	session = ClassDB.instantiate("GrandleonSession")
	if session == null:
		_fail("native session bridge did not load")
		return
	var slots: Array = session.checkpoint_slots()
	if slots.size() != 3:
		_fail("native session did not publish three checkpoint slots")
		return
	var package := ProjectSettings.globalize_path("res://game.gpk")
	var save_root := ProjectSettings.globalize_path("user://checkpoint-slot-smoke")
	if not session.start(package, "muster_road", false, 2, save_root):
		_fail("campaign could not start: " + String(session.last_error()))
		return
	deadline_msec = Time.get_ticks_msec() + 30000


func _process(_delta: float) -> bool:
	if finished:
		return true
	if Time.get_ticks_msec() >= deadline_msec:
		_fail("checkpoint slot smoke timed out")
		return true
	for event: Dictionary in session.poll():
		match String(event.get("type", "")):
			"campaign_begun":
				if phase >= 2:
					resumed_manual = bool(event.get("resumed", false))
			"management":
				_handle_management(event.get("company", {}))
			"session_failed":
				_fail("campaign session failed: " + String(event.get("message", "unknown")))
	return false


func _handle_management(company: Dictionary) -> void:
	if phase == 0:
		if session.checkpoint_save_ready():
			_fail("a fresh campaign claimed an unwritten checkpoint")
			return
		var roster: Array = company.get("roster", [])
		if roster.is_empty() or roster[0].get("carried", []).is_empty():
			_fail("test campaign had no carried item to commit")
			return
		phase = 1
		var member_id := int(roster[0].get("member_id", 0))
		var item_id := int(roster[0].get("carried", [])[0].get("id", 0))
		if not session.submit_management("take", member_id, item_id, 0):
			_fail("management checkpoint gesture was refused")
	elif phase == 1:
		if not session.checkpoint_save_ready():
			_fail("committed between-battle progress was not saveable")
			return
		if not session.save_checkpoint(1):
			_fail("manual checkpoint could not be saved: " + String(session.last_error()))
			return
		var first := _slot(1)
		if not bool(first.get("exists", false)) or bool(first.get("active", false)):
			_fail("saved checkpoint did not remain an independent copy")
			return
		phase = 2
		if not session.load_checkpoint(1):
			_fail("manual checkpoint could not be loaded: " + String(session.last_error()))
	elif phase == 2:
		if not resumed_manual:
			_fail("manual checkpoint did not resume its campaign")
			return
		var first := _slot(1)
		if not bool(first.get("exists", false)) or not bool(first.get("active", false)):
			_fail("loaded checkpoint was not marked current")
			return
		if not session.checkpoint_save_ready() or not session.save_checkpoint(2):
			_fail("loaded checkpoint could not be copied to another slot")
			return
		if not bool(_slot(2).get("exists", false)):
			_fail("second checkpoint copy was not published")
			return
		if session.load_checkpoint(3):
			_fail("empty checkpoint slot unexpectedly loaded")
			return
		if session.save_checkpoint(4):
			_fail("out-of-range checkpoint slot unexpectedly saved")
			return
		phase = 3
		session.stop()
		print("checkpoint slot integration smoke passed")
		quit()


func _slot(index: int) -> Dictionary:
	for entry: Dictionary in session.checkpoint_slots():
		if int(entry.get("index", 0)) == index:
			return entry
	return {}


func _fail(message: String) -> void:
	finished = true
	if session != null:
		session.stop()
	push_error(message)
	quit(2)
