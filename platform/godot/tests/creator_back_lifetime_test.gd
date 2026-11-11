# SPDX-License-Identifier: MIT
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var editor = load(get_script().resource_path.get_base_dir().path_join("creator_back_lifetime_host.gd")).new()
	root.add_child(editor)
	var cancel := InputEventAction.new()
	cancel.action = "ui_cancel"
	cancel.pressed = true
	editor._unhandled_input(cancel)
	if editor.is_inside_tree():
		push_error("Cancel must exercise a real scene-tree detachment")
		quit(2)
		return
	editor.free()
	print("creator Back navigation retains original viewport: PASS")
	quit()
