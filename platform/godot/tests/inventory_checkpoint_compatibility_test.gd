# SPDX-License-Identifier: MIT
# Real authored packages, native checkpoints, and the normal HTTP/browser update.
extends SceneTree
const Catalog = preload("res://scripts/campaign_catalog.gd")
var checks := 0
var failures := 0
var work := ""
var base := ""
var compiler: Object
var library: Node
var sessions: Array = []

func _initialize() -> void: call_deferred("run")
func check(value: bool, message: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("inventory compatibility: " + message)

func compiled(project: Dictionary) -> PackedByteArray:
	var source_library = load("res://scripts/creator/project_library.gd").new()
	var result: Dictionary = compiler.compile_source_json(JSON.stringify(source_library._integer_numbers(project)))
	check(result.get("ok", false), "authored compatibility fixture compiles: " + str(result.get("diagnostics", [])))
	return result.get("package", PackedByteArray())

func entry(bytes: PackedByteArray, version: String) -> Dictionary:
	var path := version + ".gpk"
	var file := FileAccess.open(work.path_join(path), FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(bytes)
	return {"id": "inventory", "title": "Inventory compatibility", "description": "Authored inventory-mode update", "author": "Grandleon", "version": version,
		"campaign": "first_adventure", "playerMode": "choice", "languages": ["en"], "minimumPlayerVersion": "0.1.0",
		"assets": {"kind": "builtin", "pack": "grandleon-core", "version": 1}, "source_url": base + "/catalog.json",
		"package": {"url": base + "/" + path, "sizeBytes": bytes.size(), "sha256": hash.finish().hex_encode()}}

func battle(session: Object) -> bool:
	var deadline := Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "management": session.submit_management("proceed")
			elif event.type == "deployment_begins": session.submit_intent("begin")
			elif event.type in ["battle_begins", "snapshot"]: return true
			elif event.type == "session_failed": return false
	return false

func start(bytes: PackedByteArray, scope: String) -> Object:
	var session = ClassDB.instantiate("GrandleonSession")
	sessions.append(session)
	check(session.start_package(bytes, "first_adventure", 1, work.path_join("saves").path_join(scope), "en"), "native campaign starts: " + scope)
	check(await battle(session), "normal company Proceed reaches actual battle: " + scope)
	check(not session.campaign_checkpoint_bytes().is_empty(), "native pre-battle checkpoint exists: " + scope)
	return session

func reject_without_write(session: Object, candidate: PackedByteArray, label: String) -> void:
	var before: PackedByteArray = session.campaign_checkpoint_bytes()
	var live: Dictionary = session.localized_state()
	check(not session.restore_campaign_checkpoint(candidate), label + " is refused before restore")
	check(session.running(), label + " leaves running battle intact")
	check(session.localized_state() == live, label + " preserves the actual live snapshot and presentation state")
	check(session.campaign_checkpoint_bytes() == before, label + " preserves exact previous checkpoint bytes")

# A valid-checksum, semantically incompatible save: transplant the real shared
# equipment store from a legacy native save into a carried native save. This
# exercises the ownership guard, not the damaged-byte/checksum rejection path.
func fnv(bytes: PackedByteArray, begin: int, end: int, envelope := false) -> int:
	var value: int = -3750763034362895579
	for index in range(begin, end):
		value = (value ^ (0 if envelope and index >= 48 and index < 56 else int(bytes[index]))) * 1099511628211
	return value

func section(bytes: PackedByteArray, kind: int) -> PackedByteArray:
	for index in bytes.decode_u32(36):
		var row := bytes.decode_u32(40) + index * 32
		if bytes.decode_u32(row) == kind:
			return bytes.slice(bytes.decode_u32(row + 12), bytes.decode_u32(row + 12) + bytes.decode_u32(row + 16))
	return PackedByteArray()

func with_store(bytes: PackedByteArray, stock: PackedByteArray) -> PackedByteArray:
	var directory := bytes.decode_u32(40)
	var count := bytes.decode_u32(36)
	var result := bytes.slice(0, directory + count * 32)
	for index in count:
		var row := directory + index * 32
		var body := stock if bytes.decode_u32(row) == 2 else section(bytes, bytes.decode_u32(row))
		while result.size() % 4: result.append(0)
		result.encode_u32(row + 12, result.size())
		result.encode_u32(row + 16, body.size())
		result.encode_u64(row + 24, fnv(body, 0, body.size()))
		result.append_array(body)
	result.encode_u32(12, result.size())
	result.encode_u64(48, fnv(result, 0, directory + count * 32, true))
	return result

func choose_update(browser: Node, candidate: Dictionary, accept: bool) -> void:
	var before: Dictionary = library.read_launch(candidate)
	await browser._install(candidate)
	var dialog: ConfirmationDialog
	for child in browser.get_children():
		if child is ConfirmationDialog and child.visible: dialog = child
	check(dialog != null, "normal browser requires explicit fresh-start confirmation for inventory-mode update")
	check(library.read_launch(candidate).package == before.package and library.read_launch(candidate).save_scope == before.save_scope, "pending confirmation preserves installed package and save scope")
	if dialog == null: return
	if not accept:
		dialog.get_cancel_button().pressed.emit()
		await process_frame
		check(library.read_launch(candidate).package == before.package, "Keep installed version retains exact old package")
		return
	dialog.get_ok_button().pressed.emit()
	var deadline := Time.get_ticks_msec() + 5000
	while library.installed_entry(candidate).get("version") != candidate.version and Time.get_ticks_msec() < deadline:
		await process_frame
	check(library.installed_entry(candidate).get("version") == candidate.version, "normal confirmation installs verified package")

func run() -> void:
	work = OS.get_environment("GRANDLEON_INVENTORY_COMPAT_WORK")
	base = OS.get_environment("GRANDLEON_INVENTORY_COMPAT_URL")
	check(not work.is_empty() and not base.is_empty(), "isolated real loopback HTTP fixture configured")
	if work.is_empty() or base.is_empty():
		root.get_node("PCAudio").request_quit(1)
		return
	root.get_node("Localization").select_language("en", false)
	compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var fixture = load(get_script().resource_path.get_base_dir().path_join("personal_inventory_fixture.gd"))
	var project: Dictionary = JSON.parse_string(fixture.source())
	project.campaigns[0].erase("inventoryMode")
	for role: Dictionary in project.unitTypes:
		for key in ["carryCapacity", "dropGold", "dropWeaponId", "dropWeaponChance"]: role.erase(key)
	var legacy := compiled(project)
	var carried_project := project.duplicate(true)
	carried_project.campaigns[0].inventoryMode = "carried"
	var carried := compiled(carried_project)
	var update_project := project.duplicate(true)
	update_project.title = "Presentation-only update"
	var compatible := compiled(update_project)
	var stock_project := project.duplicate(true)
	stock_project.campaigns[0].startingStore = [{"weaponId": "traveller_blade", "quantity": 1}]
	var stock := compiled(stock_project)
	library = Catalog.new(work.path_join("cache"), base + "/catalog.json")
	root.add_child(library)
	var first := entry(legacy, "0.6.0")
	var inspected_legacy: Dictionary = library._inspect(legacy, first)
	var carried_entry := entry(carried, "0.6.2")
	var inspected_carried: Dictionary = library._inspect(carried, carried_entry)
	check(inspected_legacy.ok and inspected_carried.ok, "native inspector accepts both real mode variants")
	var old_info: Dictionary = inspected_legacy.compatibility
	var new_info: Dictionary = inspected_carried.compatibility
	check(old_info.get("inventoryMode") == "legacy" and new_info.get("inventoryMode") == "carried", "native inspector exposes actual authored inventory mode")
	check(old_info.gameId == new_info.gameId and old_info.contentRevision == new_info.contentRevision, "fixture changes ownership mode without changing game or content revision")
	var historical_identity := (str(old_info.gameId) + "\n" + str(old_info.contentRevision) + "\nfirst_adventure\n" + str(old_info.playerMode)).sha256_text()
	check(library._save_identity(old_info, first) == historical_identity, "legacy save hash remains byte-identical to previous algorithm")
	var omitted_mode := old_info.duplicate()
	omitted_mode.erase("inventoryMode")
	check(library._save_identity(omitted_mode, first) == historical_identity, "old inspection metadata without mode retains legacy identity")
	check(library._save_identity(new_info, carried_entry) != historical_identity, "carried mode has a distinct identity despite same revision")
	check((await library.install(first)).ok, "real HTTP installs original legacy package")
	var original: Dictionary = library.read_launch(first)
	var legacy_session = await start(legacy, original.save_scope)
	var legacy_save: PackedByteArray = legacy_session.campaign_checkpoint_bytes()
	var carried_session = await start(carried, "carried-native")
	var carried_save: PackedByteArray = carried_session.campaign_checkpoint_bytes()
	check(section(legacy_save, 9).is_empty() and section(carried_save, 9) == PackedByteArray([1]), "actual native saves distinguish inventory mode even with zero gold")
	reject_without_write(carried_session, legacy_save, "legacy checkpoint into carried package")
	reject_without_write(legacy_session, carried_save, "carried checkpoint into legacy package")
	var stock_session = await start(stock, "legacy-stock")
	var store: PackedByteArray = section(stock_session.campaign_checkpoint_bytes(), 2)
	check(store.decode_u32(0) == 1, "real legacy save contains authored equipment store")
	var stock_target = await start(carried, "carried-store-guard")
	reject_without_write(stock_target, with_store(carried_save, store), "carried checkpoint with incompatible shared equipment")
	var compatible_session = await start(compatible, "compatible-native")
	check(compatible_session.restore_campaign_checkpoint(legacy_save), "same-mode presentation update accepts actual legacy checkpoint")
	check(await battle(compatible_session), "compatible restore reaches native play")
	check(compatible_session.campaign_checkpoint_bytes() == legacy_save, "compatible restore retains checkpoint exactly")
	var update_entry := entry(compatible, "0.6.1")
	var installed: Dictionary = await library.install(update_entry)
	check(installed.ok and installed.save_compatible and library.read_launch(update_entry).save_scope == original.save_scope, "compatible presentation update keeps existing save namespace without confirmation")
	var browser = load("res://scripts/campaign_browser.gd").new()
	browser.configure(library)
	root.add_child(browser)
	await process_frame
	await choose_update(browser, carried_entry, false)
	await choose_update(browser, carried_entry, true)
	var carried_launch: Dictionary = library.read_launch(carried_entry)
	check(carried_launch.save_scope != original.save_scope, "accepted carried update uses a fresh save namespace")
	check(legacy_session.campaign_checkpoint_bytes() == legacy_save, "carried catalog update leaves legacy checkpoint file intact")
	var reverse_entry := entry(compatible, "0.6.3")
	await choose_update(browser, reverse_entry, false)
	await choose_update(browser, reverse_entry, true)
	check(library.read_launch(reverse_entry).save_scope == original.save_scope, "confirmed return to legacy reuses its previous untouched lineage")
	check(carried_session.campaign_checkpoint_bytes() == carried_save, "return to legacy leaves carried checkpoint file intact")
	for session in sessions: session.stop()
	browser.queue_free()
	library.queue_free()
	await process_frame
	print("INVENTORY_CHECKPOINT_COMPATIBILITY_", "PASS" if failures == 0 else "FAIL", " checks=", checks)
	root.get_node("PCAudio").request_quit(1 if failures else 0)
