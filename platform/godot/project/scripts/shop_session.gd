# SPDX-License-Identifier: MIT
# Transient shopping coordination. Native purchases own inventory, wallet,
# stock and saves; this only orders intents and tracks this visit's readiness.
extends RefCounted
var native
var company: Dictionary = {}
var token := ""
var ready: Array[bool] = [false, false]
var pending: Array[bool] = [false, false]
var finished := false
var queue: Array[Dictionary] = []
var inflight: Dictionary = {}
var events: Array = []

func _init(session) -> void: native = session
func active() -> bool: return not company.get("shop_offers", []).is_empty() and str(company.get("refused", "none")) in ["", "none"]
func busy() -> bool: return not queue.is_empty() or not inflight.is_empty()
func state() -> Dictionary:
	return {"type":"shop_state", "node":int(company.get("node_id", 0)), "token":token, "ready":ready.duplicate(), "pending":pending.duplicate(), "finished":finished}
func publish() -> void: events.append(state())
func reset() -> void:
	events.clear()
	company.clear(); queue.clear(); inflight.clear()
	token = ""; ready = [false, false]; pending = [false, false]; finished = false
func reconnect() -> void:
	# An accepted native transaction may already have saved. Let its result
	# finish; never repeat it or mistake it for a new connection's request.
	events.clear()
	queue.clear()
	pending = [false, false]
	if not inflight.is_empty(): pending[int(inflight.player) - 1] = true
	ready = [false, false]
	finished = false
	_auto_ready_empty_armies()
	if active(): token = Crypto.new().generate_random_bytes(16).hex_encode(); publish(); _finish_if_ready()
func clear_readiness() -> void:
	if not active(): return
	events.clear()
	ready = [false, false]
	finished = false
	_auto_ready_empty_armies()
	token = Crypto.new().generate_random_bytes(16).hex_encode()
	publish()
	_finish_if_ready()
func owns(member: int, player: int) -> bool:
	if player < 1 or player > int(company.get("local_players", 1)): return false
	for hero: Dictionary in company.get("roster", []):
		if int(hero.get("member_id", 0)) == member:
			return int(hero.get("local_player", 1)) == player and hero.get("availability") in ["available", "retired"]
	return false
func _has_eligible_hero(player: int) -> bool:
	return company.get("roster", []).any(func(hero): return int(hero.get("local_player", 1)) == player and hero.get("availability") in ["available", "retired"])
func _auto_ready_empty_armies() -> void:
	for player in int(company.get("local_players", 1)):
		if not _has_eligible_hero(player + 1): ready[player] = true
func buy(member: int, offer: int, player: int, request := 0) -> bool:
	if not active() or finished or not owns(member, player) or pending[player - 1]: return false
	if not company.shop_offers.any(func(row): return int(row.offer_id) == offer): return false
	ready[player - 1] = false
	pending[player - 1] = true
	queue.append({"member":member, "offer":offer, "player":player, "request":request, "token":token})
	publish()
	return true
func set_ready(player: int, value: bool) -> bool:
	if not active() or finished or player < 1 or player > int(company.get("local_players", 1)) or pending[player - 1]: return false
	if not value and not _has_eligible_hero(player): return false
	ready[player - 1] = value
	publish()
	_finish_if_ready()
	return true
func _finish_if_ready() -> void:
	if not active() or finished or busy(): return
	for player in int(company.get("local_players", 1)):
		if not ready[player]: return
	finished = true
	publish()
	events.append({"type":"shop_finished", "node":int(company.node_id), "token":token})
func observe(event: Dictionary) -> void:
	match str(event.get("type", "")):
		"management":
			var next: Dictionary = event.company
			if int(next.get("node_id", 0)) != int(company.get("node_id", 0)):
				reset()
				token = Crypto.new().generate_random_bytes(16).hex_encode()
			company = next.duplicate(true)
			_auto_ready_empty_armies()
			if not inflight.is_empty() and inflight.has("successful"):
				pending[int(inflight.player) - 1] = false
				events.append({"type":"shop_result", "player":int(inflight.player), "request":int(inflight.request), "token":str(inflight.token), "successful":bool(inflight.successful)})
				inflight.clear()
			if active(): publish(); _finish_if_ready()
		"management_committed":
			if not inflight.is_empty(): inflight.successful = bool(event.get("successful", false))
		"battle_begins", "campaign_ended": reset()
func pump() -> void:
	if not active() or finished or not inflight.is_empty() or queue.is_empty(): return
	var next: Dictionary = queue[0]
	if native.buy_shop_offer(int(next.member), int(next.offer), int(next.player)):
		inflight = next
		queue.pop_front()
func drain() -> Array:
	var result := events
	events = []
	return result
