# SPDX-License-Identifier: MIT
extends OptionButton


func _ready() -> void:
	RenderQuality.changed.connect(_refresh)
	item_selected.connect(_selected)
	get_popup().about_to_popup.connect(_fit_touch_rows)
	_refresh()


func _refresh() -> void:
	clear()
	for value: String in RenderQuality.CHOICES:
		var label := Localization.text("graphics." + value) if value != "auto" else Localization.text("graphics.auto", {"value": Localization.text("graphics." + RenderQuality.recommended)})
		add_item(Localization.text("graphics.quality", {"value": label}))
	select(RenderQuality.CHOICES.find(RenderQuality.choice))


func _selected(index: int) -> void:
	if not RenderQuality.select(RenderQuality.CHOICES[index]):
		select(RenderQuality.CHOICES.find(RenderQuality.choice))


func _fit_touch_rows() -> void:
	if not OS.has_feature("android"): return
	# Popup windows use physical pixels rather than the player's logical canvas.
	var popup := get_popup()
	var font_height := popup.get_theme_font("font").get_height(popup.get_theme_font_size("font_size"))
	var touch_pixels := 48.0 * DisplayServer.screen_get_dpi() / 160.0
	popup.add_theme_constant_override("v_separation", maxi(8, ceili(touch_pixels - font_height)))
