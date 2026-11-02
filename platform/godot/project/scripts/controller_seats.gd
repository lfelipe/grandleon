# SPDX-License-Identifier: MIT
class_name GrandleonControllerSeats
extends RefCounted


static func assign(previous: Array, connected: Array, player_count: int) -> Array[int]:
	var result: Array[int] = [-1, -1]
	var devices: Array[int] = []
	for value: Variant in connected:
		var device := int(value)
		if device >= 0 and device not in devices:
			devices.append(device)
	devices.sort()

	var limit := clampi(player_count, 1, 2)
	var claimed: Dictionary = {}
	for seat in min(limit, previous.size()):
		var device := int(previous[seat])
		if device in devices and not claimed.has(device):
			result[seat] = device
			claimed[device] = true

	var remaining: Array[int] = []
	for device: int in devices:
		if not claimed.has(device):
			remaining.append(device)
	if limit == 1:
		if result[0] < 0 and not remaining.is_empty():
			result[0] = remaining.pop_front()
		return result

	# Fill the lowest vacant player seat. A lone PC control is therefore
	# Player 1; a later second control joins without displacing it.
	for seat in 2:
		if result[seat] < 0 and not remaining.is_empty():
			result[seat] = remaining.pop_front()
	return result


static func assign_keyboard(previous: int, controllers: Array, player_count: int) -> int:
	var limit := clampi(player_count, 1, 2)
	if controllers.size() < limit:
		return -1
	if int(controllers[0]) < 0:
		return 0
	if previous >= 0 and previous < limit and int(controllers[previous]) < 0:
		return previous
	if previous >= 0:
		for seat in limit:
			if int(controllers[seat]) < 0:
				return seat
	return -1
