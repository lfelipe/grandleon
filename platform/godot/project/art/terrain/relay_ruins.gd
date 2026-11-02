# SPDX-License-Identifier: MIT
extends Node3D

@onready var crystal: MeshInstance3D = $Crystal
@onready var signal_ring: MeshInstance3D = $SignalRing
@onready var signal_light: OmniLight3D = $SignalLight

var _phase := 0.0


func _process(delta: float) -> void:
	_phase += delta
	crystal.rotation.y = _phase * 0.72
	signal_ring.rotation.y = -_phase * 0.44
	var pulse := 0.94 + sin(_phase * 2.4) * 0.08
	signal_ring.scale = Vector3(pulse, 1.0, pulse)
	signal_light.light_energy = 0.32 + sin(_phase * 2.4) * 0.08
