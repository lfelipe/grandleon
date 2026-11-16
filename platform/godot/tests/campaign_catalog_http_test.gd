# SPDX-License-Identifier: MIT
extends "native_campaign_play.gd"
const Catalog = preload("res://scripts/campaign_catalog.gd")
var library: Node
var base := ""
var work := ""
var checks := 0
var busy_refused := false
var progress_events := 0

func _init() -> void:
	call_deferred("_run")

func expect(condition: bool, message: String) -> bool:
	checks += 1
	if not condition:
		push_error("catalog integration: " + message)
		quit(1)
		assert(condition, message)
	return condition

func control(mode: String, extra := {}) -> void:
	var file := FileAccess.open(work.path_join("control.json"), FileAccess.WRITE)
	extra["mode"] = mode
	file.store_string(JSON.stringify(extra))
	file.close()

func first(id := "steel-and-cedar", source := "/a/catalog.json") -> Dictionary:
	for entry: Dictionary in library.entries():
		if entry.id == id and entry.source_url == base + source:
			return entry
	return {}

func _attempt_parallel() -> void:
	busy_refused = (await library.refresh()).get("error") == "busy" and library.add_source(base + "/busy/catalog.json").get("error") == "busy"

func _run() -> void:
	root.get_node("Localization").select_language("en", false)
	base = OS.get_environment("GRANDLEON_CATALOG_TEST_URL")
	work = OS.get_environment("GRANDLEON_CATALOG_TEST_WORK")
	expect(not base.is_empty() and not work.is_empty(), "real HTTP fixture configured")
	control("valid")
	library = Catalog.new(work.path_join("cache"), base + "/a/catalog.json")
	root.add_child(library)
	for url in ["file:///tmp/catalog.json", "http://example.com/catalog.json", "https://user:pass@example.com/catalog.json", "https://127.0.0.1/catalog.json", "https://10.1.2.3/catalog.json", "https://example.com\\evil"]:
		expect(not library.add_source(url).ok, "unsafe URL refused: " + url)
	expect(library.add_source(base + "/b/catalog.json").ok, "second actual HTTP source added")
	control("slow")
	call_deferred("_attempt_parallel")
	expect((await library.refresh()).ok, "real HTTP catalogs refresh")
	expect(busy_refused, "overlapping network requests refuse busy without dangling timers")
	library.progress.connect(func(_downloaded, _total): progress_events += 1)
	control("valid")
	expect(library.entries().size() == 2, "same id from two sources remains separate")
	var one := first()
	expect(one.available and not one.installed, "compatible builtin listing offered")
	expect(one.source_title == "HTTP fixture", "validated catalog title decorates entries")
	expect((await library.install(one)).ok, "real redirected package download installs")
	var launch: Dictionary = library.read_launch(one)
	expect(progress_events > 0, "real request emits progress")
	expect(launch.ok and launch.campaign == "steel_and_cedar" and launch.package.size() == int(one.package.sizeBytes), "native inspected actual Steel and Cedar package launches")
	var initial_hash: String = launch.package.hex_encode().sha256_text()
	var save_scope: String = launch.save_scope
	var original_session = ClassDB.instantiate("GrandleonSession")
	expect(original_session.start_package(launch.package, launch.campaign, 1, work.path_join("saves").path_join(save_scope), "en"), "original campaign starts in its actual save namespace")
	var played: Dictionary = await _play_to_checkpoint(original_session)
	expect(played.get("ok", false), "original campaign reaches a real first-battle victory")
	var checkpoint: PackedByteArray = original_session.campaign_checkpoint_bytes()
	expect(not checkpoint.is_empty(), "native campaign writes a real checkpoint")
	original_session.stop()
	var other := first("steel-and-cedar", "/b/catalog.json")
	expect((await library.install(other)).ok, "second source installs same id independently")
	expect(library.read_launch(other).save_scope != save_scope, "source-specific save isolation")
	var local_path: String = library.installed_entry(one).path
	# A presentation-only update retains the authored save revision. Compile
	# normally rather than mutating a loaded runtime or patching package bytes.
	var source := FileAccess.get_file_as_string("res://fixtures/source.json")
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var original_compiled: Dictionary = compiler.compile_source_json(source)
	expect(original_compiled.get("ok", false) and original_compiled.package == launch.package, "served package exactly matches the actual authored source fixture")
	var current_revision: String = str(JSON.parse_string(source).contentRevision)
	source = source.replace('"title": "Steel and Cedar"', '"title": "Steel and Cedar Updated"')
	var compiled: Dictionary = compiler.compile_source_json(source)
	expect(compiled.get("ok", false), "normal compiler produces updated campaign fixture: " + str(compiled.get("diagnostics", [])))
	for policy in ["one", "two"]:
		var policy_source := source.replace('"playerMode": "choice"', '"playerMode": "' + policy + '"')
		var policy_compiled: Dictionary = compiler.compile_source_json(policy_source)
		expect(policy_compiled.ok, "normal compiler accepts canonical policy " + policy)
		var policy_bytes: PackedByteArray = policy_compiled.package
		var policy_file := FileAccess.open(work.path_join(policy + ".gpk"), FileAccess.WRITE)
		policy_file.store_buffer(policy_bytes)
		policy_file.close()
		var policy_hash := HashingContext.new()
		policy_hash.start(HashingContext.HASH_SHA256)
		policy_hash.update(policy_bytes)
		control(policy, {"sha": policy_hash.finish().hex_encode(), "size": policy_bytes.size()})
		expect((await library.refresh()).ok, "canonical policy listing accepted " + policy)
		var policy_entry := first("con" if policy == "one" else "aux")
		expect((await library.install(policy_entry)).ok, "canonical policy package installed " + policy)
		expect(library.read_launch(policy_entry).get("playerMode") == policy, "native policy agrees with catalog " + policy)
		expect(library.installed_entry(policy_entry).path.get_file().ends_with(".gpk"), "Windows reserved id uses safe hashed path " + policy)
	var update: PackedByteArray = compiled.package
	var file := FileAccess.open(work.path_join("updated.gpk"), FileAccess.WRITE)
	file.store_buffer(update)
	file.close()
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(update)
	var update_info := {"sha": hash.finish().hex_encode(), "size": update.size()}
	control("update", update_info)
	expect((await library.refresh()).ok, "updated catalog refreshed")
	one = first()
	expect(one.update_available and one.installed_version == "0.5.0", "available update keeps installed version pinned")
	expect(library.read_launch(one).package.hex_encode().sha256_text() == initial_hash, "refresh never replaces installed artifact")
	for mode in ["checksum", "size", "corrupt", "missing_campaign", "body_oversized", "unsafe_redirect", "redirect_loop"]:
		control(mode)
		expect((await library.refresh()).ok, "failure fixture metadata valid: " + mode)
		var refused: Dictionary = await library.install(first())
		expect(not refused.ok, "bad install refused: " + mode)
		var expected_errors := {"checksum": "checksum_mismatch", "size": "size_mismatch", "corrupt": "invalid_package", "missing_campaign": "invalid_package", "body_oversized": "network_error", "unsafe_redirect": "invalid_url", "redirect_loop": "network_error"}
		expect(refused.error == expected_errors[mode], "specific install failure reason: " + mode)
		expect(library.read_launch(one).package.hex_encode().sha256_text() == initial_hash, "failed install retains exact artifact: " + mode)
		expect(FileAccess.file_exists(local_path), "previous blob retained: " + mode)
	for mode in ["incompatible", "external"]:
		control(mode)
		expect((await library.refresh()).ok, "unsupported listing still readable: " + mode)
		var unavailable := first()
		expect(not unavailable.available, "unsupported entry disabled: " + mode)
		expect((await library.install(unavailable)).error == ("incompatible_player" if mode == "incompatible" else "unsupported_assets"), "actionable compatibility reason: " + mode)
		expect(library.read_launch(unavailable).ok, "installed compatible version stays playable")
	control("valid")
	await library.refresh()
	for mode in ["traversal", "duplicate", "oversized", "invalid_json", "catalog_too_large", "malformed_assets", "invalid_version", "boolean_format_version", "down"]:
		control(mode)
		var refreshed: Dictionary = await library.refresh()
		expect(not refreshed.sources[0].ok, "invalid response rejected: " + mode)
		expect(first().version == "0.5.0" and library.read_launch(first()).ok, "valid cached listing/install retained: " + mode)
	control("update", update_info)
	await library.refresh()
	one = first()
	var pointer_temporary := local_path.get_base_dir().get_base_dir().path_join("installed/entry-" + one.id.sha256_text() + ".json.part")
	DirAccess.make_dir_recursive_absolute(pointer_temporary)
	expect((await library.install(one)).get("error") == "storage_error", "installation pointer write failure reported")
	expect(library.read_launch(one).package.hex_encode().sha256_text() == initial_hash, "pointer write failure retains previous package")
	DirAccess.remove_absolute(pointer_temporary)
	expect((await library.install(one)).ok, "valid update installs atomically")
	var updated_launch: Dictionary = library.read_launch(one)
	expect(updated_launch.ok and updated_launch.save_scope == save_scope, "update preserves version-independent saves")
	expect(updated_launch.package == update and library.installed_entry(one).version == "0.5.1", "exact updated artifact selected")
	control("valid")
	expect((await library.refresh()).ok and not first().update_available, "older catalogue version is not offered as an update")
	var compatible_session = ClassDB.instantiate("GrandleonSession")
	expect(compatible_session.start_package(updated_launch.package, one.campaign, 1, work.path_join("saves").path_join(save_scope), "en"), "compatible updated package starts")
	expect(compatible_session.restore_campaign_checkpoint(checkpoint), "real previous checkpoint restores into compatible updated package")
	compatible_session.stop()
	var revision_parts := current_revision.split(".")
	var breaking_revision := "%s.%d.0" % [revision_parts[0], int(revision_parts[1]) + 1]
	var breaking: Dictionary = compiler.compile_source_json(source.replace('"contentRevision": "%s"' % current_revision, '"contentRevision": "%s"' % breaking_revision))
	expect(breaking.ok, "normal compiler produces an incompatible-revision fixture")
	file = FileAccess.open(work.path_join("updated.gpk"), FileAccess.WRITE)
	file.store_buffer(breaking.package)
	file.close()
	hash = HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(breaking.package)
	control("update", {"sha": hash.finish().hex_encode(), "size": breaking.package.size(), "version": "0.5.2"})
	expect((await library.refresh()).ok, "incompatible update listing refreshed")
	one = first()
	var browser = load("res://scripts/campaign_browser.gd").new()
	browser.configure(library)
	root.add_child(browser)
	await process_frame
	var update_button_found := false
	for button in browser.find_children("*", "Button", true, false):
		if button.text == "Update" and button.get_meta("campaign_id", "") == one.id:
			update_button_found = true
	expect(update_button_found, "newer incompatible release has a real Update button")
	await browser._install(one)
	var dialog: ConfirmationDialog
	for child in browser.get_children():
		if child is ConfirmationDialog: dialog = child
	expect(dialog != null and dialog.visible and dialog.dialog_text.contains("cannot use your current saves"), "real browser shows incompatible-save choice before installation")
	expect(library.read_launch(one).package == update and library.read_launch(one).save_scope == save_scope, "declining update retains exact playable package and progress namespace")
	dialog.get_cancel_button().pressed.emit()
	await process_frame
	expect(library.read_launch(one).package == update, "Keep installed version leaves current package playable")
	root.size = Vector2i(390, 780)
	root.get_node("Localization").select_language("pt-BR", false)
	await process_frame
	await browser._install(one)
	for child in browser.get_children():
		if child is ConfirmationDialog: dialog = child
	expect(dialog.visible, "incompatible update still requires choice after cancellation")
	expect(dialog.dialog_text.contains("não pode usar"), "Brazilian Portuguese update guidance includes accents")
	expect(dialog.size.x <= root.size.x, "update choice fits a portrait phone viewport")
	dialog.get_ok_button().pressed.emit()
	await process_frame
	await process_frame
	expect(library.read_launch(one).package == breaking.package, "real confirmation button installs the verified incompatible update")
	browser.queue_free()
	await process_frame
	updated_launch = library.read_launch(one)
	expect(updated_launch.save_scope != save_scope, "incompatible update uses a different save namespace")
	expect(original_session.campaign_checkpoint_bytes() == checkpoint, "updating preserves actual previous checkpoint bytes")
	var fresh_session = ClassDB.instantiate("GrandleonSession")
	expect(fresh_session.start_package(updated_launch.package, one.campaign, 1, work.path_join("saves").path_join(updated_launch.save_scope), "en"), "new revision starts in its fresh namespace")
	expect(not fresh_session.restore_campaign_checkpoint(checkpoint), "native runtime confirms old checkpoint is incompatible")
	fresh_session.stop()
	update = breaking.package
	control("down")
	library.queue_free()
	await process_frame
	library = Catalog.new(work.path_join("cache"), base + "/a/catalog.json")
	root.add_child(library)
	expect(library.entries().size() == 4 and first().installed, "new process library loads offline metadata and removed installed listings")
	expect(library.sources().size() == 2, "added sources persisted")
	expect(library.read_launch(first()).package == update, "offline launch uses validated cached package")
	expect(first().source_title == "HTTP fixture" and library.installed_entry(first()).source_title == "HTTP fixture", "source title persists for offline and installed metadata")
	expect(not (await library.refresh()).ok and library.read_launch(first()).ok, "failed refresh leaves offline launch intact")
	# Corrupt an already installed blob: launch must verify again, not trust cache.
	file = FileAccess.open(library.installed_entry(first()).path, FileAccess.WRITE)
	file.store_buffer(PackedByteArray([1, 2, 3]))
	file.close()
	expect(not library.read_launch(first()).ok, "corrupt installed blob cannot launch")
	print("CAMPAIGN_CATALOG_HTTP_PASS checks=%d real_http=true native_inspection=true offline=true atomic_update=true" % checks)
	compiler = null
	library.queue_free()
	await process_frame
	quit()
