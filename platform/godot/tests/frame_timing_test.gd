# SPDX-License-Identifier: MIT
extends SceneTree

const FrameTiming = preload("res://scripts/frame_timing.gd")

var failures := 0


func _init() -> void:
	var timing = FrameTiming.new()
	_expect("an empty monitor is warming up", timing.metrics().ready, false)
	timing.sample(0.0)
	timing.sample(-1.0)
	_expect("invalid durations are ignored", timing.metrics().sample_count, 0)

	for unused in 120:
		timing.sample(1.0 / 60.0)
	var steady: Dictionary = timing.metrics()
	_expect("one second makes the readout ready", steady.ready, true)
	_expect_near("steady FPS", float(steady.fps), 60.0, 0.01)
	_expect_near("steady p95 frame time", float(steady.p95_msec), 16.667, 0.01)
	_expect_near("steady peak frame time", float(steady.peak_msec), 16.667, 0.01)

	timing.reset()
	for unused in 94:
		timing.sample(0.01)
	for unused in 6:
		timing.sample(0.04)
	var uneven: Dictionary = timing.metrics()
	_expect_near("p95 exposes repeated slow frames", float(uneven.p95_msec), 40.0, 0.01)
	_expect_near("peak exposes the slowest frame", float(uneven.peak_msec), 40.0, 0.01)

	timing.reset()
	for unused in 600:
		timing.sample(1.0 / 60.0)
	var rolling: Dictionary = timing.metrics()
	_expect("the monitor retains at most one rolling window", float(rolling.sample_seconds) <= 5.01, true)
	_expect("the rolling window remains representative", float(rolling.sample_seconds) >= 4.95, true)
	var keys := rolling.keys()
	keys.sort()
	_expect(
		"metrics contain only timing values",
		keys,
		["fps", "p95_msec", "peak_msec", "ready", "sample_count", "sample_seconds"]
	)

	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_near(label: String, actual: float, expected: float, tolerance: float) -> void:
	if absf(actual - expected) > tolerance:
		failures += 1
		push_error("%s: expected %.3f, got %.3f" % [label, expected, actual])
