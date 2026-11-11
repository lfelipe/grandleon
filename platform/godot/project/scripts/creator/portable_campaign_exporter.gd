# SPDX-License-Identifier: MIT
extends RefCounted

const LocalCampaigns = preload("res://scripts/creator/local_campaigns.gd")
const CampaignReadiness = preload("res://scripts/creator/campaign_readiness.gd")
const MAXIMUM_BYTES := 16 * 1024 * 1024
var library
var compiler: RefCounted

func _init(project_library) -> void:
	library = project_library
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")

func export_campaign_package(project_id: String, campaign_id: String, destination := "") -> Dictionary:
	if compiler == null: return _failure("native_compiler_unavailable")
	var loaded: Dictionary = library.load_source(project_id)
	if not loaded.get("ok", false): return loaded
	var campaign: Dictionary = {}
	for value: Variant in loaded.project.get("campaigns", []):
		if value is Dictionary and str(value.get("id", "")) == campaign_id:
			campaign = value
	if campaign.is_empty(): return _failure("campaign_selection_required")
	var ready := CampaignReadiness.playable_export(loaded.project, campaign_id)
	if not ready.ok: return ready
	# Portable campaigns currently use the same built-in artwork on every player.
	# Keep custom files in the editable archive until asset packs are supported.
	var assets: String = library.root_path.path_join(project_id).path_join("assets")
	if _has_asset_files(assets): return _failure("unsupported_assets")
	var compiled: Dictionary = compiler.compile_source_json(str(loaded.source))
	if not compiled.get("ok", false): return _failure("project_not_playable")
	var bytes: PackedByteArray = compiled.package
	if bytes.is_empty() or bytes.size() > MAXIMUM_BYTES: return _failure("package_too_large")
	var inspector: RefCounted = ClassDB.instantiate("GrandleonSession")
	var inspected: Dictionary = inspector.inspect_campaign(bytes, campaign_id)
	if not inspected.get("ok", false): return _failure("project_not_playable")
	var hashing := HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	hashing.update(bytes)
	var metadata := {
		"title": inspected.get("title", loaded.project.get("title", "")),
		"campaign": campaign_id, "playerMode": inspected.playerMode,
		"languages": inspected.languages, "sizeBytes": bytes.size(),
		"sha256": hashing.finish().hex_encode(),
		"minimumPlayerVersion": inspected.playerVersion,
		"assets": {"kind": "builtin", "pack": "grandleon-core", "version": 1},
	}
	var name := str(loaded.project.get("gameId", "campaign")).validate_filename() + ".gpk"
	if destination.is_empty():
		var root: String = library.root_path.get_base_dir().path_join("exports/campaigns")
		if DirAccess.make_dir_recursive_absolute(root) != OK: return _failure("storage_error")
		destination = root.path_join(name)
	var written := write_bytes(bytes, destination)
	if not written.ok: return written
	var bundle_path := destination.trim_suffix(".gpk") + ".grandleon.game.zip"
	var bundled: Dictionary = LocalCampaigns.new().write_bundle(bundle_path, bytes, metadata)
	if not bundled.ok: return _failure("storage_error")
	return {"ok": true, "path": destination, "name": name, "package": bytes, "metadata": metadata, "bundle_path": bundle_path, "bundle_name": bundle_path.get_file()}

static func write_bytes(bytes: PackedByteArray, destination: String) -> Dictionary:
	# SAF destinations are content:// URIs. Use FileAccess directly; filesystem
	# rename, mkdir, and globalize_path do not apply to document-provider URIs.
	var file := FileAccess.open(destination, FileAccess.WRITE)
	if file == null: return {"ok": false, "code": "storage_error"}
	file.store_buffer(bytes)
	file.flush()
	var error := file.get_error()
	file.close()
	return {"ok": error == OK, "code": "storage_error" if error != OK else ""}

func _has_asset_files(path: String) -> bool:
	if not DirAccess.dir_exists_absolute(path): return false
	if not DirAccess.get_files_at(path).is_empty(): return true
	for directory: String in DirAccess.get_directories_at(path):
		if _has_asset_files(path.path_join(directory)): return true
	return false

func _failure(code: String) -> Dictionary:
	return {"ok": false, "code": code}
