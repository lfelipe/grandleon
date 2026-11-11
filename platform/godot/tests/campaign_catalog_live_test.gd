# SPDX-License-Identifier: MIT
# Explicit opt-in smoke of the unauthenticated public catalog and release URL.
extends SceneTree
const Catalog = preload("res://scripts/campaign_catalog.gd")
func _init() -> void:
	call_deferred("_run")
func _run() -> void:
	var library := Catalog.new(OS.get_environment("GRANDLEON_CATALOG_TEST_WORK").path_join("live-cache"))
	root.add_child(library)
	var refreshed: Dictionary = await library.refresh()
	if not refreshed.ok:
		push_error("public catalog HTTPRequest refresh failed: " + str(refreshed))
		quit(1)
		return
	var entry: Dictionary = {}
	for candidate: Dictionary in library.entries():
		if candidate.id == "steel-and-cedar":
			entry = candidate
	if entry.is_empty():
		push_error("public catalog missing Steel and Cedar")
		quit(1)
		return
	var installed: Dictionary = await library.install(entry)
	if not installed.ok:
		push_error("public release HTTPRequest install failed: " + str(installed))
		quit(1)
		return
	var launch: Dictionary = library.read_launch(entry)
	if not launch.ok or launch.package.size() != int(entry.package.sizeBytes):
		push_error("public installed package failed native launch inspection")
		quit(1)
		return
	print("CAMPAIGN_CATALOG_LIVE_PASS source=%s package=%s bytes=%d sha256=%s auth=none" % [entry.source_url, entry.package.url, launch.package.size(), entry.package.sha256])
	library.queue_free()
	await process_frame
	quit()
