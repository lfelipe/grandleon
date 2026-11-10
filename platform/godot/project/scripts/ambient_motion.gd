# SPDX-License-Identifier: MIT
extends RefCounted


static func _wave(tick: int, phase: int) -> int:
	var cycle := (tick + phase) & 127
	var folded := cycle if cycle < 64 else 127 - cycle
	var t := folded << 6
	var square := (t * t) >> 12
	return (3 * square - ((2 * square * t) >> 12)) * 2 - 4096


static func pose(tick: int, identity: int, enabled := true) -> Vector2:
	if not enabled:
		return Vector2(1, 0)
	var seed := (identity & 0xffffffff) ^ ((identity >> 32) & 0xffffffff)
	return Vector2(
		float(4096 + int(float(_wave(tick, seed) * 96) / 4096.0)) / 4096.0,
		float(int(float(_wave(tick >> 1, seed ^ 53) * 72) / 4096.0)) / 4096.0
	)
