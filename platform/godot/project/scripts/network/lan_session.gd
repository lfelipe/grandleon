# SPDX-License-Identifier: MIT
# Discovery is independent of the reliable session transport. Only the host
# runs the native campaign. No objects/scripts are deserialized from the wire.
extends Node
signal rooms_changed
signal status_changed
signal game_ready
signal message_received(message: Dictionary)
const PROTOCOL := 1
const DISCOVERY_PORT := 29531
const GAME_PORT := 29532
const MAGIC := "grandleon-lan"
const MAX_MESSAGE := 20 * 1024 * 1024
const CHUNK := 24 * 1024
var role := ""
var connected := false
var game_paused := false
var status_key := "lan.idle"
var rooms: Dictionary = {}
var metadata: Dictionary = {}
var package_bytes := PackedByteArray()
var storage_root := "user://lan"
var peer: ENetMultiplayerPeer
var discovery: PacketPeerUDP
var discovering := false
var remote_peer := 0
var adapter: RefCounted
var address := ""
var port := GAME_PORT
var _discovery_port := DISCOVERY_PORT
var _next_discover := 0
var _started := 0
var _authenticated := false
var _room: Dictionary = {}
var _queued_messages: Array = []
var _parts := PackedByteArray()
var _part_count := 0
var _part_next := 0
var _parts_started := 0
var _multicast_lock

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS

func active() -> bool: return role != ""
func local_seat() -> int: return 1 if role == "client" else 0
func _status(key: String) -> void:
	status_key = key
	status_changed.emit()

func _load_json(name: String) -> Dictionary:
	var path := storage_root.path_join(name)
	if not FileAccess.file_exists(path): return {}
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return value if value is Dictionary else {}

func _save_json(name: String, value: Dictionary) -> bool:
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(storage_root)) != OK: return false
	var path := storage_root.path_join(name)
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null: return false
	file.store_string(JSON.stringify(value))
	file.flush()
	file.close()
	return DirAccess.rename_absolute(ProjectSettings.globalize_path(path + ".tmp"), ProjectSettings.globalize_path(path)) == OK

static func _hash(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()

func _token() -> String: return Crypto.new().generate_random_bytes(24).hex_encode()

func host(bytes: PackedByteArray, campaign: String, title: String, scope := "", game_port := GAME_PORT, discovery_port := DISCOVERY_PORT) -> bool:
	leave()
	var inspector = ClassDB.instantiate("GrandleonSession")
	var info: Dictionary = inspector.inspect_campaign(bytes, campaign)
	if not info.get("ok", false):
		_status("lan.invalid_campaign")
		return false
	peer = ENetMultiplayerPeer.new()
	if peer.create_server(game_port, 1) != OK:
		peer = null
		_status("lan.port_busy")
		return false
	role = "host"
	port = game_port
	_discovery_port = discovery_port
	package_bytes = bytes.duplicate()
	var key := (scope + ":" + campaign).sha256_text() + ".json"
	_room = _load_json(key)
	if _room.is_empty(): _room = {"id": _token(), "seat_token": ""}
	_room.file = key
	if not _save_json(key, _room):
		leave()
		_status("lan.storage_error")
		return false
	var languages: Dictionary = inspector.package_languages(bytes)
	var language := str(languages.get("source", "en"))
	if Localization.current_language() in languages.get("languages", []): language = Localization.current_language()
	metadata = {"campaign": campaign, "title": str(info.get("title", title)).left(120), "scope": scope + ":lan", "language": language, "room": str(_room.id), "sha256": _hash(bytes)}
	peer.peer_connected.connect(_peer_connected)
	peer.peer_disconnected.connect(_peer_disconnected)
	discovery = PacketPeerUDP.new()
	if discovery.bind(discovery_port) != OK:
		leave()
		_status("lan.port_busy")
		return false
	_acquire_multicast_lock()
	_status("lan.waiting")
	return true

func browse(discovery_port := DISCOVERY_PORT) -> bool:
	if role == "host": return false
	if discovery != null: discovery.close()
	discovery = PacketPeerUDP.new()
	if discovery.bind(0) != OK:
		_status("lan.network_error")
		return false
	discovery.set_broadcast_enabled(true)
	discovering = true
	_discovery_port = discovery_port
	_next_discover = 0
	rooms.clear()
	_acquire_multicast_lock()
	_status("lan.searching")
	return true

func join(host_address: String, game_port := GAME_PORT) -> bool:
	_close_peer()
	role = "client"
	address = host_address.strip_edges()
	port = game_port
	# Optional IPv4/hostname port keeps direct joining useful when a host uses
	# a non-default listener (for example two development instances).
	if address.count(":") == 1:
		var fields := address.split(":")
		if not fields[1].is_valid_int():
			_status("lan.network_error")
			return false
		address = fields[0]
		port = int(fields[1])
	if address.is_empty() or port < 1 or port > 65535:
		_status("lan.network_error")
		return false
	peer = ENetMultiplayerPeer.new()
	_queued_messages.clear()
	game_paused = false
	if peer.create_client(address, port) != OK:
		peer = null
		_status("lan.network_error")
		return false
	peer.peer_disconnected.connect(_peer_disconnected)
	_started = Time.get_ticks_msec()
	_status("lan.connecting")
	return true

func rejoin() -> void:
	if role == "client": join(address, port)

func _peer_connected(id: int) -> void:
	if role != "host": return
	remote_peer = id
	_started = Time.get_ticks_msec()
	_send({"type": "challenge", "version": PROTOCOL, "room": str(_room.id)})

func _peer_disconnected(_id: int) -> void:
	connected = false
	_authenticated = false
	remote_peer = 0
	_started = 0
	_parts.clear()
	_status("lan.partner_left" if role == "host" else "lan.disconnected")

func _process(_delta: float) -> void:
	var now := Time.get_ticks_msec()
	if discovery != null:
		if discovering and now >= _next_discover:
			_next_discover = now + 1200
			for destination: String in ["255.255.255.255", "127.0.0.1"]:
				discovery.set_dest_address(destination, _discovery_port)
				discovery.put_packet(JSON.stringify({"magic": MAGIC, "version": PROTOCOL, "type": "discover"}).to_utf8_buffer())
		for _index in mini(32, discovery.get_available_packet_count()):
			var bytes := discovery.get_packet()
			var ip := discovery.get_packet_ip()
			var source_port := discovery.get_packet_port()
			if bytes.size() > 2048: continue
			var value: Variant = JSON.parse_string(bytes.get_string_from_utf8())
			if not value is Dictionary or value.get("magic") != MAGIC or value.get("version") != PROTOCOL: continue
			if role == "host" and value.get("type") == "discover":
				discovery.set_dest_address(ip, source_port)
				discovery.put_packet(JSON.stringify({"magic": MAGIC, "version": PROTOCOL, "type": "room", "room": metadata.room, "title": metadata.title, "port": port, "occupied": connected}).to_utf8_buffer())
			elif discovering and value.get("type") == "room" and typeof(value.get("port")) in [TYPE_INT, TYPE_FLOAT]:
				var room_id := str(value.get("room", ""))
				if room_id.length() != 48 or rooms.size() > 64: continue
				var prior: Dictionary = rooms.get(room_id, {})
				rooms[room_id] = {"address": ip, "port": int(value.port), "title": str(value.get("title", "Grandleon")).left(120), "occupied": bool(value.get("occupied", false)), "seen": now}
				if prior.get("address") != ip or prior.get("port") != int(value.port) or prior.get("title") != rooms[room_id].title or prior.get("occupied") != rooms[room_id].occupied: rooms_changed.emit()
		var expired: Array = []
		for key: String in rooms:
			if now - int(rooms[key].seen) > 4500: expired.append(key)
		for key: String in expired: rooms.erase(key)
		if not expired.is_empty(): rooms_changed.emit()
	if peer == null: return
	if peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
		if connected: _peer_disconnected(1)
		elif _started > 0 and now - _started > 15000:
			_close_peer()
			_status("lan.timeout")
		return
	peer.poll()
	for _index in mini(128, peer.get_available_packet_count()):
		var sender := peer.get_packet_peer()
		var packet := peer.get_packet()
		if packet.size() > CHUNK + 512: continue
		if role == "client" and sender != 1: continue
		if role == "host" and sender != remote_peer: continue
		var value: Variant = bytes_to_var(packet)
		if not value is Dictionary: continue
		_receive(value)
	if not connected and _started > 0 and now - _started > 15000:
		_close_peer()
		_status("lan.timeout")
	if _parts_started > 0 and now - _parts_started > 10000:
		_parts.clear()
		_part_count = 0
		_parts_started = 0

func _send(value: Dictionary) -> void:
	if peer == null or peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED: return
	peer.set_target_peer(remote_peer if role == "host" else 1)
	peer.transfer_mode = MultiplayerPeer.TRANSFER_MODE_RELIABLE
	var bytes := var_to_bytes(value)
	if bytes.size() > MAX_MESSAGE:
		_status("lan.message_too_large")
		return
	if bytes.size() <= CHUNK:
		peer.put_packet(bytes)
		return
	var count := ceili(float(bytes.size()) / CHUNK)
	for index in count:
		peer.put_packet(var_to_bytes({"type": "chunk", "index": index, "count": count, "data": bytes.slice(index * CHUNK, mini(bytes.size(), (index + 1) * CHUNK))}))

func send_message(value: Dictionary) -> void:
	if connected: _send(value)

func _receive(value: Dictionary) -> void:
	var kind := str(value.get("type", ""))
	if kind == "chunk":
		var index := int(value.get("index", -1))
		var count := int(value.get("count", 0))
		var data: Variant = value.get("data")
		if not data is PackedByteArray or data.size() > CHUNK or count < 1 or count > ceili(float(MAX_MESSAGE) / CHUNK): return
		if index == 0:
			_parts.clear()
			_part_next = 0
			_part_count = count
			_parts_started = Time.get_ticks_msec()
		if count != _part_count or index != _part_next or _parts.size() + data.size() > MAX_MESSAGE: return
		_parts.append_array(data)
		_part_next += 1
		if _part_next == count:
			var result: Variant = bytes_to_var(_parts)
			_parts.clear()
			_part_count = 0
			_parts_started = 0
			if result is Dictionary and result.get("type") != "chunk": _receive(result)
		return
	if role == "client" and kind == "challenge":
		if value.get("version") != PROTOCOL:
			_close_peer()
			_status("lan.version_mismatch")
			return
		var saved := _load_json("joined.json")
		_send({"type": "hello", "version": PROTOCOL, "token": str(saved.get(str(value.get("room", "")), ""))})
		return
	if role == "host" and kind == "hello":
		if value.get("version") != PROTOCOL or (not str(_room.seat_token).is_empty() and str(value.get("token", "")) != str(_room.seat_token)):
			_send({"type": "error", "key": "lan.version_mismatch" if value.get("version") != PROTOCOL else "lan.seat_reserved"})
			peer.disconnect_peer(remote_peer)
			return
		if str(_room.seat_token).is_empty():
			_room.seat_token = _token()
			if not _save_json(str(_room.file), _room):
				_status("lan.storage_error")
				return
		_authenticated = true
		_send({"type": "launch", "metadata": metadata, "package": package_bytes, "token": str(_room.seat_token)})
		return
	if role == "client" and kind == "launch":
		var meta: Variant = value.get("metadata")
		var bytes: Variant = value.get("package")
		if not meta is Dictionary or not bytes is PackedByteArray or bytes.size() > 16 * 1024 * 1024 or _hash(bytes) != meta.get("sha256", ""): return
		var inspector = ClassDB.instantiate("GrandleonSession")
		if not inspector.inspect_campaign(bytes, str(meta.get("campaign", ""))).get("ok", false):
			_status("lan.invalid_campaign")
			return
		var saved := _load_json("joined.json")
		saved[str(meta.get("room", ""))] = str(value.get("token", ""))
		if not _save_json("joined.json", saved):
			_status("lan.storage_error")
			return
		metadata = meta
		package_bytes = bytes
		connected = true
		_status("lan.connected")
		_send({"type": "ready"})
		if adapter != null: adapter.resynchronize()
		else: game_ready.emit()
		return
	if role == "host" and kind == "ready" and _authenticated:
		connected = true
		_status("lan.connected")
		if adapter != null: adapter.peer_rejoined()
		else: game_ready.emit()
		_send({"type": "pause", "paused": game_paused})
		return
	if role == "client" and kind == "error":
		_close_peer()
		_status(str(value.get("key", "lan.network_error")))
		return
	if connected and kind == "pause" and role == "client":
		game_paused = bool(value.get("paused", false))
		_status("lan.host_paused" if game_paused else "lan.connected")
		return
	if connected:
		if adapter != null: message_received.emit(value)
		elif _queued_messages.size() < 32: _queued_messages.append(value)

func attach(value: RefCounted) -> void:
	adapter = value
	if connected and role == "client":
		if _queued_messages.is_empty(): _send({"type": "state_request"})
		else:
			for message: Dictionary in _queued_messages: message_received.emit(message)
			_queued_messages.clear()

func _close_peer() -> void:
	if peer != null: peer.close()
	peer = null
	connected = false
	_authenticated = false
	remote_peer = 0
	_started = 0
	_parts.clear()
	_parts_started = 0

func leave() -> void:
	_close_peer()
	if discovery != null: discovery.close()
	discovery = null
	discovering = false
	role = ""
	game_paused = false
	_queued_messages.clear()
	adapter = null
	package_bytes.clear()
	metadata.clear()
	_release_multicast_lock()

func set_host_paused(value: bool) -> void:
	if role != "host": return
	game_paused = value
	send_message({"type": "pause", "paused": value})

func has_reserved_seat() -> bool:
	return role == "host" and not str(_room.get("seat_token", "")).is_empty()

func release_seat() -> void:
	if role != "host" or connected or remote_peer != 0: return
	_room.seat_token = ""
	if not _save_json(str(_room.file), _room): _status("lan.storage_error")
	else: _status("lan.waiting")

func _acquire_multicast_lock() -> void:
	if not OS.has_feature("android") or _multicast_lock != null or not Engine.has_singleton("AndroidRuntime"): return
	var runtime = Engine.get_singleton("AndroidRuntime")
	var context = runtime.getApplicationContext()
	if context == null: return
	var wifi = context.getSystemService("wifi")
	if wifi == null: return
	_multicast_lock = wifi.createMulticastLock("grandleon-lan")
	if _multicast_lock != null:
		_multicast_lock.setReferenceCounted(false)
		_multicast_lock.acquire()
		var jni = Engine.get_singleton("JavaClassWrapper")
		if jni != null and jni.get_exception() != null: _multicast_lock = null

func _release_multicast_lock() -> void:
	if _multicast_lock != null:
		_multicast_lock.release()
		_multicast_lock = null

func _exit_tree() -> void: leave()
