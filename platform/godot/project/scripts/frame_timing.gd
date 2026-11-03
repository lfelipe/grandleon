# SPDX-License-Identifier: MIT
class_name GrandleonFrameTiming
extends RefCounted

const WINDOW_SECONDS := 5.0
const READY_SECONDS := 1.0
const MAX_FRAME_SECONDS := 1.0

var _samples: Array[float] = []
var _sample_seconds := 0.0


func sample(delta: float) -> void:
	if delta <= 0.0 or is_nan(delta) or is_inf(delta):
		return
	var bounded := minf(delta, MAX_FRAME_SECONDS)
	_samples.append(bounded)
	_sample_seconds += bounded
	while _samples.size() > 1 and _sample_seconds > WINDOW_SECONDS:
		_sample_seconds -= _samples.pop_front()


func metrics() -> Dictionary:
	if _samples.is_empty() or _sample_seconds <= 0.0:
		return {
			"ready": false,
			"fps": 0.0,
			"p95_msec": 0.0,
			"peak_msec": 0.0,
			"sample_seconds": 0.0,
			"sample_count": 0,
		}
	var ordered := _samples.duplicate()
	ordered.sort()
	var p95_index := clampi(ceili(float(ordered.size()) * 0.95) - 1, 0, ordered.size() - 1)
	return {
		"ready": _sample_seconds >= READY_SECONDS,
		"fps": float(_samples.size()) / _sample_seconds,
		"p95_msec": ordered[p95_index] * 1000.0,
		"peak_msec": ordered[-1] * 1000.0,
		"sample_seconds": _sample_seconds,
		"sample_count": _samples.size(),
	}


func reset() -> void:
	_samples.clear()
	_sample_seconds = 0.0
