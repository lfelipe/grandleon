# SPDX-License-Identifier: MIT
extends Control

signal launch_requested(launch: Dictionary)
signal closed

var catalog: Node
var rows: VBoxContainer
var status: Label
var source_input: LineEdit
var refresh_button: Button
var back_button: Button
var busy := false

func configure(value: Node) -> void:
	catalog = value
	catalog.changed.connect(_render_entries)
	catalog.progress.connect(_progress)

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var shade := ColorRect.new()
	shade.color = Color("07111b")
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(shade)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 44)
	add_child(margin)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 16)
	margin.add_child(column)
	var heading := Label.new()
	heading.text = _t("catalog.heading")
	heading.add_theme_font_size_override("font_size", 36)
	column.add_child(heading)
	var help := Label.new()
	help.text = _t("catalog.explanation")
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(help)
	var source_row := HBoxContainer.new()
	column.add_child(source_row)
	source_input = LineEdit.new()
	source_input.placeholder_text = _t("catalog.url_hint")
	source_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	source_input.custom_minimum_size.y = 56
	source_input.text_submitted.connect(func(_text: String): _add_source())
	source_row.add_child(source_input)
	var add := _button("catalog.add")
	add.pressed.connect(_add_source)
	source_row.add_child(add)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	rows = VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 12)
	scroll.add_child(rows)
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(status)
	var footer := HBoxContainer.new()
	column.add_child(footer)
	refresh_button = _button("catalog.refresh")
	refresh_button.pressed.connect(refresh_catalog)
	footer.add_child(refresh_button)
	back_button = _button("common.back")
	back_button.pressed.connect(close)
	footer.add_child(back_button)
	_render_entries()

func open() -> void:
	show()
	back_button.grab_focus()
	refresh_catalog()

func close() -> void:
	hide()
	closed.emit()

func _unhandled_input(event: InputEvent) -> void:
	if visible and event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		close()

func refresh_catalog() -> void:
	if busy: return
	busy = true
	refresh_button.disabled = true
	status.text = _t("catalog.loading")
	_render_entries()
	var result: Dictionary = await catalog.refresh()
	busy = false
	refresh_button.disabled = false
	var partial := false
	for source: Dictionary in result.get("sources", []):
		if not source.get("ok", false): partial = true
	status.text = (_t("catalog.partial") if partial else _t("catalog.ready")) if result.get("ok", false) else _error(result)
	_render_entries()

func _add_source() -> void:
	if busy: return
	var result: Dictionary = catalog.add_source(source_input.text.strip_edges())
	if not result.get("ok", false):
		status.text = _error(result)
		return
	source_input.clear()
	refresh_catalog()

func _render_entries() -> void:
	if rows == null: return
	for child: Node in rows.get_children():
		rows.remove_child(child)
		child.queue_free()
	var entries: Array = catalog.entries()
	if entries.is_empty():
		var empty := Label.new()
		empty.text = _t("catalog.empty")
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		rows.add_child(empty)
	for entry: Dictionary in entries:
		var panel := PanelContainer.new()
		var style := StyleBoxFlat.new()
		style.bg_color = Color("142735")
		style.set_content_margin_all(18)
		panel.add_theme_stylebox_override("panel", style)
		rows.add_child(panel)
		var column := VBoxContainer.new()
		panel.add_child(column)
		var title := Label.new()
		title.text = str(entry.get("title", ""))
		title.add_theme_font_size_override("font_size", 26)
		title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		column.add_child(title)
		var detail := Label.new()
		detail.text = str(entry.get("description", ""))
		detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		column.add_child(detail)
		var credit := Label.new()
		credit.text = _t("catalog.credit", {"author": str(entry.get("author", "")), "version": str(entry.get("version", ""))})
		credit.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		column.add_child(credit)
		var actions := HBoxContainer.new()
		column.add_child(actions)
		if bool(entry.get("installed", false)):
			var play := _button("catalog.play")
			play.pressed.connect(_play.bind(entry))
			actions.add_child(play)
		if not bool(entry.get("installed", false)) or bool(entry.get("update_available", false)):
			var install := _button("catalog.update" if entry.get("installed", false) else "catalog.download")
			install.disabled = not bool(entry.get("available", false)) or busy
			install.pressed.connect(_install.bind(entry))
			actions.add_child(install)
		if not bool(entry.get("available", false)):
			var reason := Label.new()
			reason.text = _error({"error": entry.get("reason", "invalid_catalog")})
			reason.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			column.add_child(reason)

func _install(entry: Dictionary) -> void:
	if busy: return
	busy = true
	refresh_button.disabled = true
	_render_entries()
	status.text = _t("catalog.downloading")
	var result: Dictionary = await catalog.install(entry)
	busy = false
	refresh_button.disabled = false
	status.text = _t("catalog.installed") if result.get("ok", false) else _error(result)
	_render_entries()

func _play(entry: Dictionary) -> void:
	var launch: Dictionary = catalog.read_launch(entry)
	if not launch.get("ok", false):
		status.text = _error(launch)
		return
	launch_requested.emit(launch)

func _progress(downloaded: int, total: int) -> void:
	status.text = _t("catalog.progress", {"percent": str(clampi(downloaded * 100 / maxi(total, 1), 0, 100))}) if total > 0 else _t("catalog.downloading")

func _error(result: Dictionary) -> String:
	var code := str(result.get("error", "network_error"))
	if code not in ["invalid_url", "invalid_catalog", "unsupported_assets", "incompatible_player", "invalid_package", "checksum_mismatch", "size_mismatch", "network_error", "http_error", "busy", "not_installed", "storage_error"]:
		code = "network_error"
	return _t("catalog.error." + code)

func _button(key: String) -> Button:
	var button := Button.new()
	button.text = _t(key)
	button.custom_minimum_size = Vector2(160, 56)
	button.add_theme_font_size_override("font_size", 20)
	return button

func _t(key: String, values: Dictionary = {}) -> String:
	return Localization.text(key, values)
