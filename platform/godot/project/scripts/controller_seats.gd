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

	# A lone pad complements Player 1's keyboard/mouse seat. If a second pad
	# arrives later it fills Player 1 without displacing the first controller.
	if result[0] < 0 and result[1] < 0 and remaining.size() == 1:
		result[1] = remaining.pop_front()
	else:
		for seat in 2:
			if result[seat] < 0 and not remaining.is_empty():
				result[seat] = remaining.pop_front()
	return result
