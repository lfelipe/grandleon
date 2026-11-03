# SPDX-License-Identifier: MIT
extends SceneTree

const ArtLibrary = preload("res://scripts/art_library.gd")
const GraphicsProfiles = preload("res://scripts/graphics_profiles.gd")
const TerrainSceneSelector = preload("res://scripts/terrain_scene_selector.gd")

var failures := 0


func _init() -> void:
	var manifest := {
		"assets": [
			{
				"path": "characters/knight_blue.png",
				"kind": "character",
				"archetype": "knight",
				"faction_colour": "blue",
				"style": "medieval",
				"figure": "first",
				"width": 48,
				"height": 72,
				"columns": 1,
				"rows": 1,
				"tile_width": 48,
				"tile_height": 72,
			},
			{
				"path": "characters/knight_blue_frames.png",
				"kind": "character-frames",
				"archetype": "knight",
				"faction_colour": "blue",
				"style": "medieval",
				"figure": "first",
				"width": 256,
				"height": 96,
				"columns": 4,
				"rows": 1,
				"tile_width": 64,
				"tile_height": 96,
			},
		],
		"character_animation": {
			"pivot": [0.25, 0.9],
			"sequences": {
				"idle": {"asset_kind": "character", "frames": [0]},
				"move": {
					"asset_kind": "character-frames",
					"frames": [1, 3],
					"frames_per_second": 2.0,
					"loop": true,
				},
				"attack": {
					"asset_kind": "character-frames", "frames": [2],
				},
				"hurt": {
					"asset_kind": "character-frames", "frames": [99],
				},
			},
		},
	}
	var art = ArtLibrary.new(manifest)
	var unit := {"archetype": 0, "colour": 0, "style": 0, "figure": 0}
	_expect("bridge previews as road", art.terrain_kind_for_source("bridge"), 1)
	_expect("plain previews as grass", art.terrain_kind_for_source("plain"), 9)
	_expect("custom terrain has visible fallback", art.terrain_kind_for_source("moon dust"), 9)

	var idle: Dictionary = art.character_frame(unit, "idle")
	_expect("idle uses standing asset", idle.path, "res://assets/characters/knight_blue.png")
	_expect("standing cell width", idle.cell_size.x, 48)
	_expect("standing cell height", idle.cell_size.y, 72)
	_expect("manifest pivot", idle.pivot, Vector2(0.25, 0.9))

	var move_start: Dictionary = art.character_frame(unit, "move", 0.0)
	var move_second: Dictionary = art.character_frame(unit, "move", 0.5)
	var move_loop: Dictionary = art.character_frame(unit, "move", 1.0)
	_expect("HD sheet path", move_start.path, "res://assets/characters/knight_blue_frames.png")
	_expect("HD frame width", move_start.cell_size.x, 64)
	_expect("HD frame height", move_start.cell_size.y, 96)
	_expect("first configured walk frame", move_start.frame, 1)
	_expect("timed walk frame", move_second.frame, 3)
	_expect("looped walk frame", move_loop.frame, 1)
	_expect("attack pose", art.character_frame(unit, "attack", 12.0).frame, 2)
	_expect("out-of-range pose is safe", art.character_frame(unit, "hurt").frame, 0)
	_expect("unknown state uses idle", art.character_frame(unit, "missing").path, idle.path)

	art.configure_profile({
		"assets": {
			"characterSprites": {
				"knight|blue|medieval|first": {
					"path": "res://art/characters/rook_commander_hd.png",
					"columns": 4,
					"rows": 2,
					"tile_width": 443,
					"tile_height": 443,
					"pivot": [0.5, 0.94],
					"sequences": {
						"idle": {
							"frames": [0, 1],
							"frames_per_second": 2.0,
							"loop": true,
						},
						"move": {
							"frames": [2, 3],
							"frames_per_second": 7.0,
							"loop": true,
						},
						"attack": {
							"frames": [4, 5],
							"frames_per_second": 10.0,
						},
						"hurt": {"frames": [7]},
					},
				},
			},
		},
	})
	var replacement_idle: Dictionary = art.character_frame(unit, "idle", 0.6)
	_expect(
		"profile sprite path",
		replacement_idle.path,
		"res://art/characters/rook_commander_hd.png"
	)
	_expect("profile sprite cell", replacement_idle.cell_size, Vector2i(443, 443))
	_expect("profile idle timing", replacement_idle.frame, 1)
	_expect("profile walk timing", art.character_frame(unit, "move", 0.15).frame, 3)
	_expect("profile attack timing", art.character_frame(unit, "attack", 0.11).frame, 5)
	_expect("profile hurt pose", art.character_frame(unit, "hurt").frame, 7)
	_expect("profile pivot", replacement_idle.pivot, Vector2(0.5, 0.94))

	art.configure_profile({
		"assets": {
			"characterSprites": {
				"knight|blue|medieval|first": {
					"path": "res://art/characters/missing.png",
					"columns": 4,
					"rows": 2,
					"tile_width": 443,
					"tile_height": 443,
				},
			},
		},
	})
	_expect(
		"missing profile sprite falls back",
		art.character_frame(unit, "idle").path,
		"res://assets/characters/knight_blue.png"
	)
	art.configure_profile({
		"assets": {
			"characterSprites": {
				"knight|blue|medieval|first": {
					"path": "res://art/characters/rook_commander_hd.png",
					"columns": 4,
					"rows": 2,
					"tile_width": 444,
					"tile_height": 443,
				},
			},
		},
	})
	_expect(
		"misaligned profile grid falls back",
		art.character_frame(unit, "idle").path,
		"res://assets/characters/knight_blue.png"
	)

	var legacy = ArtLibrary.new({})
	var legacy_move: Dictionary = legacy.character_frame(unit, "move", 0.2)
	_expect("legacy sheet path", legacy_move.path, "res://assets/characters/knight_blue_frames.png")
	_expect("legacy cell size", legacy_move.cell_size, Vector2i(32, 32))
	_expect("legacy walk timing", legacy_move.frame, 1)
	_expect("legacy pivot", legacy_move.pivot, Vector2(0.5, 0.94))
	_test_relay_terrain_pack()
	_test_terrain_scene_selector()

	if failures == 0:
		print("art library smoke passed")
	quit(1 if failures > 0 else 0)


func _test_relay_terrain_pack() -> void:
	var text := FileAccess.get_file_as_string("res://content/graphics_profiles.json")
	var document: Variant = JSON.parse_string(text)
	_expect("graphics profile parses", document is Dictionary, true)
	if not document is Dictionary:
		return
	var hybrid: Dictionary = document.get("profiles", {}).get("voxel-sprites", {})
	var terrain_scenes: Dictionary = hybrid.get("assets", {}).get("terrainScenes", {})
	_test_production_sprite_pack(hybrid)
	var expected := ["water", "road", "forest", "mountain", "sand", "ruins", "grass"]
	for terrain_name: String in expected:
		_expect("%s terrain is bound" % terrain_name, terrain_scenes.has(terrain_name), true)
		if not terrain_scenes.has(terrain_name):
			continue
		var binding: Variant = terrain_scenes[terrain_name]
		var path := String(binding.get("scene", "")) if binding is Dictionary else String(binding)
		_test_terrain_scene(terrain_name, path)
		if binding is Dictionary:
			var variants: Variant = binding.get("variants", [])
			if variants is Array:
				for variant_index in variants.size():
					var variant: Dictionary = TerrainSceneSelector.decode(variants[variant_index])
					_test_terrain_scene(
						"%s variant %d" % [terrain_name, variant_index + 1],
						String(variant.get("scene", ""))
					)

	var road: Dictionary = terrain_scenes.get("road", {})
	var water_variant: Variant = road.get("whenAdjacent", {}).get("water", {})
	var bridge_path := (
		String(water_variant.get("scene", ""))
		if water_variant is Dictionary else String(water_variant)
	)
	_test_terrain_scene("water-adjacent bridge", bridge_path)

	var voxel: Dictionary = GraphicsProfiles.new().profile("voxel")
	var inherited_scenes: Dictionary = voxel.get("assets", {}).get("terrainScenes", {})
	_expect("full-voxel profile inherits the relay terrain pack", inherited_scenes, terrain_scenes)
	_expect("full-voxel profile keeps 3D characters", voxel.get("characterRenderer"), "voxel")


func _test_production_sprite_pack(profile: Dictionary) -> void:
	var bindings: Dictionary = profile.get("assets", {}).get("characterSprites", {})
	var expected := {
		"commander|amber|scifi|first": "res://art/characters/rook_commander_hd.png",
		"rogue|amber|scifi|first": "res://art/characters/kestrel_rogue_hd.png",
		"stormcaller|violet|scifi|first": "res://art/characters/morrow_stormcaller_hd.png",
		"beast|violet|scifi|first": "res://art/characters/relay_beast_hd.png",
	}
	for key: String in expected:
		_expect("%s sprite is bound" % key, bindings.has(key), true)
		if not bindings.has(key):
			continue
		var binding: Dictionary = bindings[key]
		var path := String(binding.get("path", ""))
		_expect("%s sprite path" % key, path, expected[key])
		_expect("%s sprite grid width" % key, int(binding.get("width", 0)), 1772)
		_expect("%s sprite grid height" % key, int(binding.get("height", 0)), 886)
		_expect("%s sprite columns" % key, int(binding.get("columns", 0)), 4)
		_expect("%s sprite rows" % key, int(binding.get("rows", 0)), 2)
		_expect("%s sprite exists" % key, ResourceLoader.exists(path, "Texture2D"), true)
		if ResourceLoader.exists(path, "Texture2D"):
			var texture := ResourceLoader.load(path, "Texture2D") as Texture2D
			_expect("%s sprite loads" % key, texture != null, true)
			if texture != null:
				_expect("%s image width" % key, texture.get_width(), 1772)
				_expect("%s image height" % key, texture.get_height(), 886)

	var art = ArtLibrary.new()
	art.configure_profile(profile)
	var production_units := [
		{
			"label": "Rook",
			"unit": {"archetype": 5, "colour": 4, "style": 1, "figure": 0},
			"path": "res://art/characters/rook_commander_hd.png",
		},
		{
			"label": "Kestrel",
			"unit": {"archetype": 6, "colour": 4, "style": 1, "figure": 0},
			"path": "res://art/characters/kestrel_rogue_hd.png",
		},
		{
			"label": "Morrow",
			"unit": {"archetype": 3, "colour": 3, "style": 1, "figure": 0},
			"path": "res://art/characters/morrow_stormcaller_hd.png",
		},
		{
			"label": "Relay beast",
			"unit": {"archetype": 7, "colour": 3, "style": 1, "figure": 0},
			"path": "res://art/characters/relay_beast_hd.png",
		},
	]
	for entry: Dictionary in production_units:
		var frame: Dictionary = art.character_frame(entry.unit, "idle", 0.6)
		_expect(
			"%s resolves through its committed replacement" % String(entry.label),
			frame.path,
			entry.path
		)
		_expect(
			"%s replacement keeps its cell" % String(entry.label),
			frame.cell_size,
			Vector2i(443, 443)
		)
	var kestrel := {"archetype": 6, "colour": 4, "style": 1, "figure": 0}
	var idle: Dictionary = art.character_frame(kestrel, "idle", 0.6)
	_expect("Kestrel idle uses its second pose", idle.frame, 1)
	_expect("Kestrel Crosscut uses its follow-through", art.character_frame(kestrel, "attack", 0.11).frame, 5)
	_expect("Kestrel Veil uses its cast pose", art.character_frame(kestrel, "ability").frame, 6)


func _test_terrain_scene_selector() -> void:
	var binding := {
		"scene": "base.tscn",
		"variants": [
			{"scene": "quarter.tscn", "rotationYDegrees": 90},
			{"scene": "half.tscn", "rotationYDegrees": 180},
			{"scene": "three-quarter.tscn", "rotationYDegrees": 270},
		],
	}
	var first: Dictionary = TerrainSceneSelector.select(binding, 0, 0, 2)
	var repeat: Dictionary = TerrainSceneSelector.select(binding, 0, 0, 2)
	_expect("terrain variant selection is deterministic", repeat, first)
	_expect("terrain variant hashes its coordinate", first.get("scene"), "half.tscn")
	_expect(
		"terrain variant exposes its rotation",
		is_equal_approx(float(first.get("rotation_y")), PI),
		true
	)
	_expect(
		"terrain variant changes across cells",
		TerrainSceneSelector.select(binding, 1, 0, 2).get("scene"),
		"three-quarter.tscn"
	)
	_expect(
		"terrain variant handles negative surroundings",
		TerrainSceneSelector.select(binding, -1, 0, 2).get("scene"),
		"quarter.tscn"
	)
	_expect(
		"direct terrain binding remains valid",
		TerrainSceneSelector.select("direct.tscn", 5, 7, 3).get("scene"),
		"direct.tscn"
	)


func _test_terrain_scene(label: String, path: String) -> void:
	_expect("%s uses a replaceable scene" % label, path.begins_with("res://art/terrain/"), true)
	_expect("%s scene exists" % label, ResourceLoader.exists(path), true)
	if not ResourceLoader.exists(path):
		return
	var packed := load(path) as PackedScene
	_expect("%s scene loads" % label, packed != null, true)
	if packed == null:
		return
	var instance := packed.instantiate() as Node3D
	_expect("%s scene has a 3D root" % label, instance != null, true)
	if instance == null:
		return
	var foundation := instance.get_node_or_null("Foundation") as MeshInstance3D
	_expect("%s scene has a foundation" % label, foundation != null, true)
	if foundation != null:
		var box := foundation.mesh as BoxMesh
		_expect("%s foundation is a box" % label, box != null, true)
		if box != null:
			_expect(
				"%s foundation occupies one cell" % label,
				box.size.is_equal_approx(Vector3(0.94, 0.26, 0.94)),
				true
			)
	_expect("%s scene has visible detail" % label, _mesh_count(instance) >= 3, true)
	instance.free()


func _mesh_count(node: Node) -> int:
	var count := 1 if node is MeshInstance3D else 0
	for child: Node in node.get_children():
		count += _mesh_count(child)
	return count


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
