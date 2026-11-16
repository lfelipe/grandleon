# SPDX-License-Identifier: MIT
# Shared screen projection keeps dropped equipment visible in both board views.
extends Node2D
var player: Node
var drawn_cells: Array[Vector2i] = []
func _process(_delta: float) -> void: queue_redraw()
func _draw() -> void:
	drawn_cells.clear()
	if player == null or player.board == null or not player.board.visible or player.snapshot.is_empty(): return
	for pile: Dictionary in player.snapshot.get("ground_items", []):
		var cell := Vector2i(int(pile.x), int(pile.y))
		if cell in drawn_cells or not player.board.cell_is_visible(cell): continue
		drawn_cells.append(cell)
		var point: Vector2 = player.board.screen_position_for_cell(cell)
		var diamond := PackedVector2Array([point + Vector2(0, -12), point + Vector2(12, 0), point + Vector2(0, 12), point + Vector2(-12, 0), point + Vector2(0, -12)])
		draw_colored_polygon(diamond, Color("233342"))
		draw_polyline(diamond, Color("ffca63"), 3, true)
		draw_line(point + Vector2(-5, 0), point + Vector2(5, 0), Color("ffca63"), 2, true)
		draw_line(point + Vector2(0, -5), point + Vector2(0, 5), Color("ffca63"), 2, true)
