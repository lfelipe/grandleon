# SPDX-License-Identifier: MIT
# Data-only campaign library. HTTPRequest stays on Godot's main loop; packages
# are inspected before an immutable blob and its atomic installation pointer.
extends Node

signal changed()
signal progress(downloaded: int, total: int)

const OFFICIAL_URL := "https://raw.githubusercontent.com/lfelipe/grandleon-campaigns/main/catalog.json"
const CATALOG_LIMIT := 1024 * 1024
const PACKAGE_LIMIT := 16 * 1024 * 1024
const ENTRY_LIMIT := 128

# Tests and developer tools can select an isolated root before first use.
var storage_root := "user://campaign-library"
var _default_source := OFFICIAL_URL
var _loaded := false
var _sources: Array = []
var _catalogs: Dictionary = {}
var _busy := false
var _request: HTTPRequest
var _progress_clock := 0.0
var _session: Object
var _pending_update: Dictionary = {}

func _init(root := "", default_source := OFFICIAL_URL) -> void:
	if not root.is_empty():
		storage_root = root
	_default_source = default_source


func _exit_tree() -> void:
	if is_instance_valid(_request):
		_request.cancel_request()
	_request = null
	_session = null


func _process(delta: float) -> void:
	if _request == null:
		return
	_progress_clock += delta
	if _progress_clock >= 0.1:
		_progress_clock = 0.0
		progress.emit(_request.get_downloaded_bytes(), _request.get_body_size())

func _error(code: String, detail := "") -> Dictionary:
	return {"ok": false, "error": code, "reason": detail if not detail.is_empty() else code}

func _matches(pattern: String, value: String) -> bool:
	var expression := RegEx.new()
	expression.compile(pattern)
	return expression.search(value) != null

func _url_ok(url: String) -> bool:
	if url.to_utf8_buffer().size() > 2048 or _matches("[\\x00-\\x20\\x7f]", url) or url.contains("\\"):
		return false
	var expression := RegEx.new()
	expression.compile("^(https?)://([A-Za-z0-9.-]+|\\[::1\\])(?::([0-9]{1,5}))?(/[^# ]*)?$")
	var match_url := expression.search(url)
	if match_url == null:
		return false
	var host := match_url.get_string(2).to_lower()
	var port := match_url.get_string(3)
	if not port.is_empty() and (int(port) < 1 or int(port) > 65535):
		return false
	var loopback := host in ["localhost", "127.0.0.1", "[::1]"]
	if match_url.get_string(1) == "http":
		return loopback # Explicit local development sources only.
	if loopback or host.ends_with(".") or host.begins_with(".") or host.ends_with(".local") or host.ends_with(".localhost") or not host.contains("."):
		return false
	if _matches("^[0-9.]+$", host):
		var parts := host.split(".")
		if parts.size() != 4:
			return false
		for part in parts:
			if part.is_empty() or int(part) > 255 or str(int(part)) != part:
				return false
		var a := int(parts[0])
		var b := int(parts[1])
		if a in [0, 10, 127] or a >= 224 or (a == 169 and b == 254) or (a == 172 and b >= 16 and b <= 31) or (a == 192 and b == 168) or (a == 100 and b >= 64 and b <= 127):
			return false
	return true

func _source_dir(key: String) -> String:
	return storage_root.path_join(key)

func _parse_json(text: String) -> Variant:
	var parser := JSON.new()
	return parser.data if parser.parse(text) == OK else null


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path) or FileAccess.get_file_as_bytes(path).size() > CATALOG_LIMIT:
		return null
	return _parse_json(FileAccess.get_file_as_string(path))

func _write_atomic(path: String, bytes: PackedByteArray) -> bool:
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir())) != OK:
		return false
	var temporary := path + ".part"
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
		return false
	var result := DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary), ProjectSettings.globalize_path(path))
	if result != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
	return result == OK

func _ensure_loaded() -> void:
	if _loaded:
		return
	_loaded = true
	var saved: Variant = _read_json(storage_root.path_join("sources.json"))
	var urls: Array = [_default_source] if not _default_source.is_empty() and _url_ok(_default_source) else []
	if saved is Array:
		for url in saved:
			if url is String and _url_ok(url) and not urls.has(url) and urls.size() < 16:
				urls.append(url)
	for url: String in urls:
		var key := url.sha256_text()
		_sources.append({"url": url, "key": key, "status": "offline", "reason": ""})
		var cached: Variant = _read_json(_source_dir(key).path_join("catalog.json"))
		var checked := _validate_catalog(cached, url)
		if checked.ok:
			_catalogs[key] = checked.catalog

func sources() -> Array:
	_ensure_loaded()
	return _sources.duplicate(true)

func add_source(url: String) -> Dictionary:
	_ensure_loaded()
	if _busy:
		return _error("busy")
	url = url.strip_edges()
	if not _url_ok(url):
		return _error("invalid_url")
	for source: Dictionary in _sources:
		if source.url == url:
			return {"ok": true, "source": source.duplicate(true)}
	if _sources.size() >= 16:
		return _error("invalid_catalog", "source_limit")
	var urls: Array = []
	for source: Dictionary in _sources:
		urls.append(source.url)
	urls.append(url)
	if not _write_atomic(storage_root.path_join("sources.json"), JSON.stringify(urls).to_utf8_buffer()):
		return _error("storage_error")
	var source := {"url": url, "key": url.sha256_text(), "status": "offline", "reason": ""}
	_sources.append(source)
	changed.emit()
	return {"ok": true, "source": source.duplicate(true)}

func _version_ok(version: Variant) -> bool:
	return version is String and _matches("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$", version)

func _older(current: String, required: String) -> bool:
	var a := current.split(".")
	var b := required.split(".")
	for index in 3:
		if int(a[index]) != int(b[index]):
			return int(a[index]) < int(b[index])
	return false

func _native() -> Object:
	if _session == null and ClassDB.class_exists("GrandleonSession"):
		_session = ClassDB.instantiate("GrandleonSession")
	return _session

func _player_version() -> String:
	var session := _native()
	return str(session.player_version()) if session != null and session.has_method("player_version") else "0.0.0"

func _validate_entry(value: Variant, url: String) -> Dictionary:
	if not value is Dictionary or not _url_ok(url):
		return _error("invalid_catalog")
	for field in ["id", "title", "description", "author", "version", "campaign", "playerMode", "minimumPlayerVersion"]:
		if not value.get(field) is String or value[field].length() > (2048 if field == "description" else 160):
			return _error("invalid_catalog", "invalid_" + field)
	if value.title.is_empty() or not _matches("^[a-z0-9][a-z0-9_-]{0,63}$", value.id) or not _matches("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$", value.campaign):
		return _error("invalid_catalog", "invalid_id")
	if not _version_ok(value.version) or not _version_ok(value.minimumPlayerVersion) or not value.playerMode in ["choice", "one", "two"]:
		return _error("invalid_catalog", "invalid_version_or_mode")
	if not value.get("languages") is Array or value.languages.is_empty() or value.languages.size() > 16:
		return _error("invalid_catalog", "invalid_languages")
	var seen: Array = []
	for language in value.languages:
		if not language is String or not _matches("^[a-z]{2,3}(-[A-Za-z0-9]{2,8})*$", language) or seen.has(language):
			return _error("invalid_catalog", "invalid_languages")
		seen.append(language)
	var package: Variant = value.get("package")
	if not package is Dictionary or not package.get("url") is String or not _url_ok(package.url) or (package.url.begins_with("http:") and not url.begins_with("http:")):
		return _error("invalid_catalog", "invalid_package_url")
	var size: Variant = package.get("sizeBytes")
	if not (size is int or size is float) or size != int(size) or int(size) < 1 or int(size) > PACKAGE_LIMIT:
		return _error("invalid_catalog", "invalid_package_size")
	if not package.get("sha256") is String or not _matches("^[a-fA-F0-9]{64}$", package.sha256):
		return _error("invalid_catalog", "invalid_checksum")
	var assets: Variant = value.get("assets")
	if not assets is Dictionary or not assets.get("kind") is String or assets.kind.is_empty():
		return _error("invalid_catalog", "invalid_assets")
	if assets.kind == "builtin" and (not assets.get("pack") is String or not (assets.get("version") is int or assets.get("version") is float) or assets.version != int(assets.version) or int(assets.version) < 1):
		return _error("invalid_catalog", "invalid_assets")
	var entry: Dictionary = value.duplicate(true)
	# Save lineage belongs to this installation, never to the remote catalogue.
	entry.erase("_save_scopes")
	entry.package.sha256 = entry.package.sha256.to_lower()
	entry.source_url = url
	entry.source_title = value.get("source_title", url) if value.get("source_title", url) is String and str(value.get("source_title", url)).length() <= 160 else url
	entry.source_key = url.sha256_text()
	entry.key = entry.source_key + ":" + entry.id
	entry.reason = ""
	if assets.get("kind") != "builtin" or assets.get("pack") != "grandleon-core" or assets.get("version") != 1:
		entry.reason = "unsupported_assets"
	elif _older(_player_version(), entry.minimumPlayerVersion):
		entry.reason = "incompatible_player"
	entry.available = entry.reason.is_empty()
	return {"ok": true, "entry": entry}

func _validate_catalog(value: Variant, url: String) -> Dictionary:
	if not value is Dictionary or value.get("format") != "grandleon-campaign-catalog" or not (value.get("formatVersion") is int or value.get("formatVersion") is float) or value.get("formatVersion") != 1:
		return _error("invalid_catalog")
	if not value.get("id") is String or not _matches("^[a-z0-9][a-z0-9._-]{0,95}$", value.id) or not value.get("title") is String or value.title.is_empty() or value.title.length() > 160:
		return _error("invalid_catalog")
	if not value.get("campaigns") is Array or value.campaigns.size() > ENTRY_LIMIT:
		return _error("invalid_catalog")
	var entries_by_id: Dictionary = {}
	var catalog: Dictionary = value.duplicate(true)
	catalog.campaigns = []
	for candidate in value.campaigns:
		var checked := _validate_entry(candidate, url)
		if not checked.ok:
			return checked
		if entries_by_id.has(checked.entry.id):
			return _error("invalid_catalog", "duplicate_id")
		entries_by_id[checked.entry.id] = true
		checked.entry.source_title = catalog.title
		catalog.campaigns.append(checked.entry)
	return {"ok": true, "catalog": catalog}

func _installed_pointer(entry: Dictionary) -> String:
	return _source_dir(entry.source_key).path_join("installed").path_join("entry-" + entry.id.sha256_text() + ".json")

func installed_entry(entry: Dictionary) -> Dictionary:
	_ensure_loaded()
	# All filesystem components are derived again from validated source/id data.
	var checked := _validate_entry(entry, str(entry.get("source_url", "")))
	if not checked.ok or not _url_ok(checked.entry.source_url):
		return {}
	var saved: Variant = _read_json(_installed_pointer(checked.entry))
	var installed := _validate_entry(saved, checked.entry.source_url)
	if not installed.ok:
		return {}
	var result: Dictionary = installed.entry
	if result.id != checked.entry.id:
		return {}
	var path := _source_dir(result.source_key).path_join("packages").path_join(result.package.sha256 + ".gpk")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var size := file.get_length()
	file.close()
	if size != int(result.package.sizeBytes):
		return {}
	result.path = path
	result.installed = true
	result.installed_version = result.version
	result._save_scopes = {}
	var scopes: Variant = saved.get("_save_scopes", {})
	if scopes is Dictionary and scopes.size() <= 128:
		for identity: Variant in scopes:
			if identity is String and _matches("^[a-f0-9]{64}$", identity) and (scopes[identity] == "legacy" or scopes[identity] == identity):
				result._save_scopes[identity] = scopes[identity]
	return result

func entries() -> Array:
	_ensure_loaded()
	var result: Array = []
	for source: Dictionary in _sources:
		var current: Array = _catalogs.get(source.key, {}).get("campaigns", []).duplicate(true)
		var ids: Dictionary = {}
		for entry: Dictionary in current:
			ids[entry.id] = true
		# A removed listing must not hide a campaign already installed offline.
		var installed_dir := _source_dir(source.key).path_join("installed")
		var installed_files := DirAccess.get_files_at(installed_dir) if DirAccess.dir_exists_absolute(installed_dir) else PackedStringArray()
		for filename: String in installed_files:
			if not filename.ends_with(".json"):
				continue
			var saved: Variant = _read_json(_source_dir(source.key).path_join("installed").path_join(filename))
			var checked := _validate_entry(saved, source.url)
			if checked.ok and not ids.has(checked.entry.id):
				current.append(checked.entry)
		for entry: Dictionary in current:
			entry.source_title = _catalogs.get(source.key, {}).get("title", entry.source_title)
			var installed := installed_entry(entry)
			entry.installed = not installed.is_empty()
			entry.installed_version = installed.get("version", "")
			entry.update_available = entry.installed and _older(installed.version, entry.version) and entry.package.sha256 != installed.package.sha256
			result.append(entry)
	return result

func _fetch(url: String, limit: int) -> Dictionary:
	for redirect in 5:
		if not _url_ok(url):
			return _error("invalid_url")
		_request = HTTPRequest.new()
		_request.timeout = 20.0
		_request.body_size_limit = limit
		_request.max_redirects = 0
		_request.accept_gzip = false
		add_child(_request)
		var started := _request.request(url)
		if started != OK:
			_request.queue_free()
			_request = null
			return _error("network_error", str(started))
		var response: Array = await _request.request_completed
		progress.emit(_request.get_downloaded_bytes(), _request.get_body_size())
		_request.queue_free()
		_request = null
		var status := int(response[1])
		# With automatic redirects disabled Godot reports REDIRECT_LIMIT_REACHED
		# alongside the real 3xx headers; validate each destination ourselves.
		if response[0] != HTTPRequest.RESULT_SUCCESS and not (response[0] == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED and status in [301, 302, 303, 307, 308]):
			return _error("network_error", str(response[0]))
		if status in [301, 302, 303, 307, 308]:
			var next_url := ""
			for header: String in response[2]:
				if header.to_lower().begins_with("location:"):
					next_url = header.substr(9).strip_edges()
			if next_url.begins_with("/") and not next_url.begins_with("//"):
				var slash := url.find("/", url.find("://") + 3)
				next_url = (url.substr(0, slash) if slash >= 0 else url) + next_url
			if not _url_ok(next_url) or (url.begins_with("https:") and not next_url.begins_with("https:")):
				return _error("invalid_url", "unsafe_redirect")
			url = next_url
			continue
		if status != 200:
			return _error("http_error", str(status))
		return {"ok": true, "body": response[3]}
	return _error("network_error", "redirect_limit")

func refresh() -> Dictionary:
	_ensure_loaded()
	if _busy:
		return _error("busy")
	_busy = true
	var results: Array = []
	for source: Dictionary in _sources:
		var fetched := await _fetch(source.url, CATALOG_LIMIT)
		var checked: Dictionary = fetched
		if fetched.ok:
			checked = _validate_catalog(_parse_json(fetched.body.get_string_from_utf8()), source.url)
			if checked.ok:
				if _write_atomic(_source_dir(source.key).path_join("catalog.json"), fetched.body):
					_catalogs[source.key] = checked.catalog
				else:
					checked = _error("storage_error")
		source.status = "ready" if checked.ok else "offline"
		source.reason = "" if checked.ok else checked.error
		results.append({"url": source.url, "ok": checked.ok, "error": checked.get("error", "")})
	_busy = false
	changed.emit()
	var any_ready := false
	for source_result: Dictionary in results:
		any_ready = any_ready or source_result.ok
	return {"ok": any_ready, "sources": results, "error": "" if any_ready else "network_error"}

func _inspect(bytes: PackedByteArray, entry: Dictionary) -> Dictionary:
	if bytes.size() != int(entry.package.sizeBytes):
		return _error("size_mismatch")
	var hashing := HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	hashing.update(bytes)
	if hashing.finish().hex_encode() != entry.package.sha256:
		return _error("checksum_mismatch")
	var session := _native()
	if session == null or not session.has_method("inspect_campaign"):
		return _error("invalid_package", "native_inspector_unavailable")
	var checked: Dictionary = session.inspect_campaign(bytes, entry.campaign)
	if not checked.get("ok", false):
		return _error("unsupported_assets" if checked.get("error") == "assets_unsupported" else "invalid_package", str(checked.get("detail", checked.get("error", "invalid_package"))))
	if checked.get("playerMode", "") != entry.playerMode:
		return _error("invalid_package", "player_mode_mismatch")
	return {"ok": true, "compatibility": checked}

func _save_identity(info: Dictionary, entry: Dictionary) -> String:
	var identity := str(info.get("gameId", "")) + "\n" + str(info.get("contentRevision", "")) + "\n" + str(entry.campaign) + "\n" + str(info.get("playerMode", ""))
	# Keep every existing legacy save scope byte-identical. Personal ownership
	# cannot reuse it even when an author leaves contentRevision unchanged.
	if info.get("inventoryMode", "legacy") == "carried": identity += "\ncarried"
	return identity.sha256_text()

func install(entry: Dictionary, accept_fresh_start := false) -> Dictionary:
	_ensure_loaded()
	if _busy:
		return _error("busy")
	var checked := _validate_entry(entry, str(entry.get("source_url", "")))
	if not checked.ok:
		return checked
	entry = checked.entry
	var known := false
	for source: Dictionary in _sources:
		known = known or source.url == entry.source_url
	if not known:
		return _error("invalid_catalog", "unknown_source")
	if not entry.available:
		return _error(entry.reason)
	_busy = true
	var fetched: Dictionary
	if accept_fresh_start and _pending_update.get("key") == entry.key and _pending_update.get("sha") == entry.package.sha256:
		fetched = {"ok": true, "body": _pending_update.bytes}
	else:
		fetched = await _fetch(entry.package.url, int(entry.package.sizeBytes))
	var result: Dictionary = fetched
	if fetched.ok:
		result = _inspect(fetched.body, entry)
		if result.ok:
			var previous := installed_entry(entry)
			var old_launch := read_launch(previous) if not previous.is_empty() else {}
			var identity := _save_identity(result.compatibility, entry)
			var scopes: Dictionary = previous.get("_save_scopes", {}).duplicate()
			var compatible := previous.is_empty()
			if old_launch.get("ok", false):
				var old_identity := _save_identity(old_launch.compatibility, previous)
				if not scopes.has(old_identity): scopes[old_identity] = "legacy"
				compatible = identity == old_identity
			if not compatible and not accept_fresh_start:
				_pending_update = {"key": entry.key, "sha": entry.package.sha256, "bytes": fetched.body}
				_busy = false
				return {"ok": false, "error": "save_incompatible", "installed_version": previous.get("version", ""), "version": entry.version}
			if not scopes.has(identity): scopes[identity] = "legacy" if previous.is_empty() else identity
			entry._save_scopes = scopes
			var blob := _source_dir(entry.source_key).path_join("packages").path_join(entry.package.sha256 + ".gpk")
			if not _write_atomic(blob, fetched.body) or not _write_atomic(_installed_pointer(entry), JSON.stringify(entry).to_utf8_buffer()):
				result = _error("storage_error")
			else:
				result.save_compatible = compatible
				_pending_update.clear()
	_busy = false
	changed.emit()
	return {"ok": true, "entry": installed_entry(entry), "save_compatible": result.get("save_compatible", true)} if result.ok else result

func cancel_update() -> void:
	_pending_update.clear()

func read_launch(entry: Dictionary) -> Dictionary:
	var installed := installed_entry(entry)
	if installed.is_empty():
		return _error("not_installed")
	if not installed.available:
		return _error(installed.reason)
	var bytes := FileAccess.get_file_as_bytes(installed.path)
	var checked := _inspect(bytes, installed)
	if not checked.ok:
		return checked
	var identity := _save_identity(checked.compatibility, installed)
	var generation: String = installed.get("_save_scopes", {}).get(identity, "legacy")
	var scope: String = "catalog-" + (installed.source_url + "\n" + installed.id).sha256_text()
	if generation != "legacy": scope += "-" + generation
	return {"ok": true, "package": bytes, "bytes": bytes, "campaign": installed.campaign,
		"playerMode": installed.playerMode, "languages": installed.languages,
		"title": installed.title, "save_scope": scope, "compatibility": checked.compatibility}
