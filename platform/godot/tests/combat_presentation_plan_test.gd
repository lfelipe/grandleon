# SPDX-License-Identifier: MIT
extends SceneTree

const CombatPresentationPlan = preload("res://scripts/combat_presentation_plan.gd")

var failures := 0


func _init() -> void:
	var before := _snapshot(9, 7)
	var after := _snapshot(7, 3)
	var events := [
		{
			"kind": "unit_damaged", "unit_id": 20,
			"related_unit_id": 10, "amount": 4,
		},
		{
			"kind": "attack_missed", "unit_id": 10,
			"related_unit_id": 20, "amount": 0,
		},
		{"kind": "unit_endured", "unit_id": 10, "related_unit_id": 20},
	]
	var plan := CombatPresentationPlan.build(before, after, events, "attack", 77)
	_expect_true("accepted exchange plans", bool(plan.get("valid", false)))
	_expect("accepted exchange has two beats", plan.get("beats", []).size(), 2)
	if plan.get("beats", []).size() == 2:
		var strike: Dictionary = plan.beats[0]
		var counter: Dictionary = plan.beats[1]
		_expect("strike actor is authoritative", strike.actor_id, 10)
		_expect("strike target is authoritative", strike.target_id, 20)
		_expect("damage is authoritative", strike.damage, 4)
		_expect_true("first strike is not a counter", not strike.counter)
		_expect("first actor locks the left stage slot", plan.stage.left_unit_id, 10)
		_expect("first target locks the right stage slot", plan.stage.right_unit_id, 20)
		_expect("opening actor renders on the left", strike.actor_slot, "left")
		_expect("opening target renders on the right", strike.target_slot, "right")
		_expect_true("second reversed event is a counter", counter.counter)
		_expect("counter actor stays in the right slot", counter.actor_slot, "right")
		_expect("counter target stays in the left slot", counter.target_slot, "left")
		_expect_true("counter miss remains a miss", not counter.landed)
		_expect_true("endurance attaches to affected beat", counter.endured)
	_expect("content identity survives planning", plan.get("content_id"), 77)
	_expect("source before snapshot is unchanged", before.units[1].health, 7)
	_expect("source events are unchanged", events[0].amount, 4)

	var area_before := before.duplicate(true)
	area_before.units.append({
		"id": 30, "name": "Sentinel", "side": "second", "player": 0,
		"x": 2, "y": 1, "health": 6, "maximum_health": 6,
		"on_board": true, "archetype": 2, "colour": 1,
		"style": 2, "figure": 0,
	})
	var area_after := area_before.duplicate(true)
	area_after.units[1].health = 4
	area_after.units[2].health = 3
	var area_plan := CombatPresentationPlan.build(
		area_before, area_after,
		[
			{"kind": "unit_damaged", "unit_id": 20, "related_unit_id": 10, "amount": 3},
			{"kind": "unit_damaged", "unit_id": 30, "related_unit_id": 10, "amount": 3},
		],
		"ability"
	)
	_expect_true("multi-target ability plans", bool(area_plan.get("valid", false)))
	_expect("multi-target ability keeps two beats", area_plan.beats.size(), 2)
	if area_plan.beats.size() == 2:
		_expect("first area target takes the right slot", area_plan.beats[0].stage_right.id, 20)
		_expect("area actor remains on the left", area_plan.beats[1].stage_left.id, 10)
		_expect("second area target rotates into the right slot", area_plan.beats[1].stage_right.id, 30)
		_expect("second area actor attacks from the left", area_plan.beats[1].actor_slot, "left")
		_expect("second area impact stays on the right", area_plan.beats[1].target_slot, "right")

	var lethal := CombatPresentationPlan.build(
		before, _snapshot(9, 0),
		[
			{"kind": "unit_damaged", "unit_id": 20, "related_unit_id": 10, "amount": 7},
			{"kind": "unit_defeated", "unit_id": 20, "related_unit_id": 10},
			{"kind": "item_dropped", "unit_id": 20, "content_id": 91},
		],
		"attack"
	)
	_expect_true("lethal exchange plans", bool(lethal.get("valid", false)))
	_expect_true("defeat attaches to its hit", bool(lethal.beats[0].defeated))
	_expect("drop result attaches to the defeat beat", lethal.beats[0].special_results.size(), 1)
	_expect("drop result keeps its content identity", lethal.beats[0].special_results[0].content_id, 91)
	_expect(
		"non-cinematic events remain available to other presenters",
		lethal.passthrough_events[0].kind, "item_dropped"
	)

	_expect(
		"non-combat event stream falls back",
		CombatPresentationPlan.build(before, after, [{"kind": "unit_moved"}], "move"),
		{}
	)
	_expect(
		"missing actor falls back",
		CombatPresentationPlan.build(
			before, after,
			[{"kind": "unit_damaged", "unit_id": 20, "related_unit_id": 999, "amount": 2}],
			"attack"
		),
		{}
	)

	if failures == 0:
		print("combat presentation plan smoke passed")
	quit(1 if failures > 0 else 0)


func _snapshot(first_health: int, second_health: int) -> Dictionary:
	return {
		"width": 4,
		"height": 3,
		"units": [
			{
				"id": 10, "name": "Auren", "side": "first", "player": 1,
				"x": 0, "y": 1, "health": first_health, "maximum_health": 9,
				"on_board": first_health > 0, "archetype": 0, "colour": 0,
				"style": 2, "figure": 0,
			},
			{
				"id": 20, "name": "Warden", "side": "second", "player": 0,
				"x": 1, "y": 1, "health": second_health, "maximum_health": 7,
				"on_board": second_health > 0, "archetype": 0, "colour": 1,
				"style": 2, "figure": 1,
			},
		],
	}


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_true(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)
