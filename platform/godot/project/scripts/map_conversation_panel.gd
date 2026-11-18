# SPDX-License-Identifier: MIT
extends Control

signal answered(answer_id: int)

var questions: Array
var prompt: Label
var buttons: VBoxContainer
var speaker: Label
var first_answer: Button

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.018, 0.032, 0.052, 0.68)
	add_child(shade)
	var panel := PanelContainer.new()
	add_child(panel)
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.anchor_left = 0.16
	panel.anchor_right = 0.84
	panel.anchor_top = 0.20
	panel.anchor_bottom = 0.80
	var style := StyleBoxFlat.new()
	style.bg_color = Color("101e2b")
	style.set_corner_radius_all(20)
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		style.set_content_margin(side, 26)
	panel.add_theme_stylebox_override("panel", style)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	panel.add_child(scroll)
	var content := VBoxContainer.new()
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	content.add_theme_constant_override("separation", 12)
	scroll.add_child(content)
	speaker = Label.new()
	speaker.add_theme_font_size_override("font_size", 26)
	speaker.add_theme_color_override("font_color", Color("69d9ff"))
	content.add_child(speaker)
	prompt = Label.new()
	prompt.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	prompt.add_theme_font_size_override("font_size", 24)
	content.add_child(prompt)
	buttons = VBoxContainer.new()
	buttons.add_theme_constant_override("separation", 10)
	content.add_child(buttons)
	var close := Button.new()
	close.text = Localization.text("map.cancel")
	close.custom_minimum_size.y = 54
	close.pressed.connect(hide)
	content.add_child(close)
	hide()

func start(character: String, pages: Array) -> void:
	if pages.is_empty(): return
	speaker.text = character
	questions = pages
	show()
	_show_question(questions[0])

func _show_question(question: Dictionary) -> void:
	prompt.text = str(question.text)
	for child: Node in buttons.get_children():
		buttons.remove_child(child)
		child.queue_free()
	first_answer = null
	for answer: Dictionary in question.answers:
		var button := Button.new()
		button.text = str(answer.text)
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		button.custom_minimum_size.y = 56
		button.add_theme_font_size_override("font_size", 22)
		button.pressed.connect(_choose.bind(answer))
		buttons.add_child(button)
		if first_answer == null: first_answer = button
	if first_answer != null: first_answer.grab_focus()

func _choose(answer: Dictionary) -> void:
	var next := int(answer.next)
	if next == 0:
		hide()
		answered.emit(int(answer.id))
		return
	for question: Dictionary in questions:
		if int(question.id) == next:
			_show_question(question)
			return

func activate_answer() -> void:
	var focused := get_viewport().gui_get_focus_owner()
	if focused is Button and is_ancestor_of(focused):
		focused.pressed.emit()
