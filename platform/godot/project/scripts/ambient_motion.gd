# SPDX-License-Identifier: MIT
extends RefCounted


static func pose(seconds: float, identity: int, enabled := true) -> Vector2:
	if not enabled:
		return Vector2(1, 0)
	var seed := (identity & 0xffffffff) ^ ((identity >> 32) & 0xffffffff)
	var phase := float(seed & 1023) * TAU / 1024.0
	var period := 5.4 + float((seed >> 10) & 7) * 0.15
	# Keep a steady silhouette: less than one percent of slow, continuous
	# breathing, with no tick quantization or whole-body rocking.
	return Vector2(1.0 + sin(seconds * TAU / period + phase) * 0.0065, 0)
