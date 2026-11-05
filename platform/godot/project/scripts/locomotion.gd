# SPDX-License-Identifier: MIT
class_name GrandleonLocomotion
extends RefCounted

# Presentation-only gait derived from distance, not wall-clock time. That keeps
# a footfall attached to the board when a route accelerates, slows at its end,
# or is interrupted and resumed from the visible position.


static func pose(distance: float, config: Dictionary) -> Dictionary:
	var safe_distance := maxf(0.0, distance)
	var phase := fposmod(safe_distance, 1.0) * TAU
	return {
		"phase": phase,
		"animation_time": safe_distance * float(
			config.get("strideSecondsPerCell", 2.0 / 7.0)
		),
		# Two rises per full left/right cycle, with both planted contacts at
		# ground height. The stronger rise makes the two authored walk poses
		# read as steps at tactical camera distance instead of a sliding swap.
		"bob": absf(sin(phase)) * float(
			config.get("moveBobHeight", 0.045)
		),
		"sway": sin(phase) * float(
			config.get("moveSwayRadians", 0.025)
		),
	}
