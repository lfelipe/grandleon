# SPDX-License-Identifier: MIT
extends Control

signal applied(questions: Array, next_reply_id: int)
signal cancelled

var questions: Array
var selected := 0
var next_reply_id := 1
var question_select: OptionButton
var question_text: TextEdit
var answer_rows: VBoxContainer
var add_question_button: Button
var remove_question_button: Button
var add_answer_button: Button
var apply_button: Button
var feedback: Label

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	z_index = 42
	var shade := ColorRect.new()
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.006, 0.012, 0.02, 0.94)
	add_child(shade)
	var panel := PanelContainer.new()
	panel.position = Vector2(390, 54)
	panel.size = Vector2(1140, 972)
	var style := StyleBoxFlat.new()
	style.bg_color = Color("0c1d29")
	style.set_corner_radius_all(18)
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		style.set_content_margin(side, 22)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var scroll := ScrollContainer.new()
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var form := VBoxContainer.new()
	form.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	form.add_theme_constant_override("separation", 12)
	scroll.add_child(form)
	var heading := Label.new()
	heading.text = Localization.text("conversation.editor.title")
	heading.add_theme_font_size_override("font_size", 28)
	form.add_child(heading)
	var hint := Label.new()
	hint.text = Localization.text("conversation.editor.help")
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	form.add_child(hint)
	question_select = OptionButton.new()
	question_select.fit_to_longest_item = false
	question_select.clip_text = true
	question_select.custom_minimum_size.y = 56
	question_select.item_selected.connect(_show_question)
	form.add_child(question_select)
	add_question_button = _button(form, "conversation.editor.add_question", _add_question)
	remove_question_button = _button(form, "conversation.editor.remove_question", _remove_question)
	question_text = TextEdit.new()
	question_text.placeholder_text = Localization.text("conversation.editor.question")
	question_text.custom_minimum_size.y = 106
	question_text.text_changed.connect(func() -> void:
		questions[selected].text = question_text.text
	)
	form.add_child(question_text)
	answer_rows = VBoxContainer.new()
	answer_rows.add_theme_constant_override("separation", 10)
	form.add_child(answer_rows)
	add_answer_button = _button(form, "conversation.editor.add_answer", _add_answer)
	feedback = Label.new()
	feedback.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback.add_theme_color_override("font_color", Color("ffca63"))
	form.add_child(feedback)
	apply_button = _button(form, "map.apply", func(): applied.emit(questions.duplicate(true), next_reply_id))
	_button(form, "map.cancel", func(): cancelled.emit())
	hide()

func _button(parent: Container, key: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = Localization.text(key)
	button.custom_minimum_size.y = 56
	button.pressed.connect(action)
	parent.add_child(button)
	return button

func open(original: Array, next_id: int) -> void:
	questions = original.duplicate(true)
	next_reply_id = maxi(1, next_id)
	for question: Dictionary in questions:
		for answer: Dictionary in question.answers:
			next_reply_id = maxi(next_reply_id, int(answer.id) + 1)
	feedback.text = ""
	show()
	if questions.is_empty():
		_add_question()
	else:
		_show_question(0)
	question_select.grab_focus()

func _show_question(index: int) -> void:
	selected = index
	var question: Dictionary = questions[index]
	question_select.clear()
	for position in questions.size():
		question_select.add_item(_question_label(position))
	question_select.select(index)
	question_text.text = str(question.text)
	for child: Node in answer_rows.get_children():
		answer_rows.remove_child(child)
		child.queue_free()
	for answer: Dictionary in question.answers:
		var row := VBoxContainer.new()
		answer_rows.add_child(row)
		var label := LineEdit.new()
		label.text = str(answer.text)
		label.placeholder_text = Localization.text("conversation.editor.answer")
		label.max_length = 240
		label.custom_minimum_size.y = 56
		label.text_changed.connect(func(value: String): answer.text = value)
		row.add_child(label)
		var destination := OptionButton.new()
		destination.fit_to_longest_item = false
		destination.clip_text = true
		destination.custom_minimum_size.y = 56
		destination.add_item(Localization.text("conversation.editor.ends"))
		destination.set_item_metadata(0, 0)
		for later in range(index + 1, questions.size()):
			destination.add_item(_question_label(later))
			var option := destination.item_count - 1
			destination.set_item_metadata(option, int(questions[later].id))
			if int(answer.get("nextQuestionId", 0)) == int(questions[later].id):
				destination.select(option)
		destination.item_selected.connect(func(option: int):
			var target := int(destination.get_item_metadata(option))
			if target == 0: answer.erase("nextQuestionId")
			else: answer.nextQuestionId = target
		)
		row.add_child(destination)
		if question.answers.size() > 2:
			_button(row, "conversation.editor.remove_answer", _remove_answer.bind(int(answer.id)))
	add_question_button.disabled = questions.size() >= 32 or next_reply_id > 32766
	remove_question_button.visible = index > 0
	remove_question_button.disabled = _used_as_follow_up(int(question.id))
	add_answer_button.disabled = question.answers.size() >= 4 or next_reply_id > 32767

func _question_label(index: int) -> String:
	var words := str(questions[index].text).strip_edges().replace("\n", " ")
	if words.is_empty():
		return Localization.text("conversation.editor.question_number", {"number": index + 1})
	return "%d · %s" % [index + 1, words.left(65)]

func _add_question() -> void:
	var identity := 1
	for question: Dictionary in questions: identity = maxi(identity, int(question.id) + 1)
	questions.append({"id": identity, "text": "", "answers": [
		{"id": next_reply_id, "text": ""}, {"id": next_reply_id + 1, "text": ""},
	]})
	next_reply_id += 2
	_show_question(questions.size() - 1)

func _add_answer() -> void:
	questions[selected].answers.append({"id": next_reply_id, "text": ""})
	next_reply_id += 1
	_show_question(selected)

func _used_as_follow_up(identity: int) -> bool:
	for question: Dictionary in questions:
		for answer: Dictionary in question.answers:
			if int(answer.get("nextQuestionId", 0)) == identity: return true
	return false

func _remove_question() -> void:
	questions.remove_at(selected)
	_show_question(maxi(0, selected - 1))

func _remove_answer(identity: int) -> void:
	questions[selected].answers = questions[selected].answers.filter(func(answer): return int(answer.id) != identity)
	_show_question(selected)
