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
					"facesRight": false,
					"directionalFlip": true,
					"directionalSheet": {
						"path": "res://art/characters/medieval_knight_male_directions_hd.png",
						"teamMaskPath": "res://art/characters/medieval_knight_male_directions_hd_team_mask.png",
						"columns": 4,
						"rows": 2,
						"tile_width": 443,
						"tile_height": 443,
						"pivot": [0.5, 0.94],
						"headings": {
							"north": {
								"idle": {"frames": [0, 1], "frames_per_second": 2.0, "loop": true},
								"move": {"frames": [2, 3], "frames_per_second": 7.0, "loop": true},
							},
							"south": {
								"idle": {"frames": [4, 5], "frames_per_second": 2.0, "loop": true},
								"move": {"frames": [6, 7], "frames_per_second": 7.0, "loop": true},
							},
						},
					},
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
	_expect("profile native facing", replacement_idle.faces_right, false)
	_expect("profile directional mirroring", replacement_idle.directional_flip, true)
	var north_move := art.character_frame(unit, "move", 0.15, "north")
	_expect(
		"north route uses authored directional sheet",
		north_move.path,
		"res://art/characters/medieval_knight_male_directions_hd.png"
	)
	_expect("north route uses north walk phase", north_move.frame, 3)
	_expect("north route never mirrors front/back art", north_move.directional_flip, false)
	_expect(
		"north route keeps its matching team mask",
		north_move.team_mask_path,
		"res://art/characters/medieval_knight_male_directions_hd_team_mask.png"
	)
	var south_idle := art.character_frame(unit, "idle", 0.6, "south")
	_expect("south route uses south idle phase", south_idle.frame, 5)
	_expect(
		"combat pose stays on the original action sheet",
		art.character_frame(unit, "attack", 0.0, "north").path,
		"res://art/characters/rook_commander_hd.png"
	)
	_expect(
		"east route stays on the original action sheet",
		art.character_frame(unit, "move", 0.0, "east").path,
		"res://art/characters/rook_commander_hd.png"
	)

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
	art.configure_profile({
		"assets": {
			"characterSprites": {
				"knight|blue|medieval|first": {
					"path": "res://art/characters/rook_commander_hd.png",
					"columns": 4,
					"rows": 2,
					"tile_width": 443,
					"tile_height": 443,
					"directionalFlip": "false",
				},
			},
		},
	})
	_expect(
		"invalid facing metadata falls back",
		art.character_frame(unit, "idle").path,
		"res://assets/characters/knight_blue.png"
	)

	var legacy = ArtLibrary.new({})
	var legacy_move: Dictionary = legacy.character_frame(unit, "move", 0.2)
	_expect("legacy sheet path", legacy_move.path, "res://assets/characters/knight_blue_frames.png")
	_expect("legacy cell size", legacy_move.cell_size, Vector2i(32, 32))
	_expect("legacy walk timing", legacy_move.frame, 1)
	_expect("legacy pivot", legacy_move.pivot, Vector2(0.5, 0.94))
	_test_team_colour_support()
	_test_relay_terrain_pack()
	_test_terrain_scene_selector()

	if failures == 0:
		print("art library smoke passed")
	quit(1 if failures > 0 else 0)


func _test_team_colour_support() -> void:
	var art = ArtLibrary.new({})
	art.configure_profile({
		"sprite": {"teamColours": {"bone": "a9a38f"}},
		"assets": {
			"characterSprites": {
				"knight|*|mythical|first": {
					"path": "res://icon.png",
					"teamMaskPath": "res://icon.png",
					"columns": 1,
					"rows": 1,
					"tile_width": 512,
					"tile_height": 512,
				},
				"knight|red|mythical|first": {
					"path": "res://art/characters/kestrel_rogue_hd.png",
					"teamMaskPath": "res://art/characters/missing_mask.png",
					"columns": 4,
					"rows": 2,
					"tile_width": 443,
					"tile_height": 443,
				},
			},
		},
	})
	var blue := {"archetype": 0, "colour": 0, "style": 2, "figure": 0}
	var red := {"archetype": 0, "colour": 1, "style": 2, "figure": 0}
	var wildcard_frame: Dictionary = art.character_frame(blue, "idle")
	var exact_frame: Dictionary = art.character_frame(red, "idle")
	_expect("wildcard sprite serves another faction", wildcard_frame.path, "res://icon.png")
	_expect("wildcard frame keeps faction colour", wildcard_frame.team_colour, "blue")
	_expect("valid team mask is retained", wildcard_frame.team_mask_path, "res://icon.png")
	_expect(
		"exact-colour sprite takes precedence over wildcard",
		exact_frame.path,
		"res://art/characters/kestrel_rogue_hd.png"
	)
	_expect("missing team mask falls back cleanly", exact_frame.team_mask_path, "")
	_expect(
		"missing team mask leaves source texture unchanged",
		art.character_frame_texture(exact_frame).get_rid(),
		art.texture(String(exact_frame.path)).get_rid()
	)
	var first_variant := art.character_frame_texture(wildcard_frame)
	var cached_variant := art.character_frame_texture(wildcard_frame)
	_expect("recoloured texture is created", first_variant != null, true)
	_expect(
		"recoloured texture is reused",
		first_variant.get_rid(), cached_variant.get_rid()
	)

	var palette_values: Dictionary = {}
	for name: String in ArtLibrary.COLOUR_NAMES:
		palette_values[art.team_colour(name).to_html(false)] = true
	_expect("all six faction colours are distinct", palette_values.size(), 6)
	_expect("profile may tune a faction colour", art.team_colour("bone"), Color("a9a38f"))

	var source := Image.create(2, 1, false, Image.FORMAT_RGBA8)
	var original := Color(0.18, 0.46, 0.72, 0.64)
	source.set_pixel(0, 0, original)
	source.set_pixel(1, 0, original)
	var mask := Image.create(2, 1, false, Image.FORMAT_RGBA8)
	mask.set_pixel(0, 0, Color(1.0, 1.0, 1.0, 0.0))
	mask.set_pixel(1, 0, Color.WHITE)
	var recoloured: Image = art._recolour_image(
		source, mask, art.team_colour("amber")
	)
	var unchanged := recoloured.get_pixel(0, 0)
	var changed := recoloured.get_pixel(1, 0)
	var source_pixel := source.get_pixel(1, 0)
	_expect("unmasked pixels remain unchanged", unchanged, source.get_pixel(0, 0))
	_expect("masked pixels receive a faction hue", changed != source_pixel, true)
	_expect(
		"masked alpha remains unchanged",
		is_equal_approx(changed.a, source_pixel.a), true
	)
	_expect(
		"masked source luminance is preserved",
		absf(art._luminance(changed) - art._luminance(source_pixel)) < 0.01,
		true
	)


func _test_relay_terrain_pack() -> void:
	var text := FileAccess.get_file_as_string("res://content/graphics_profiles.json")
	var document: Variant = JSON.parse_string(text)
	_expect("graphics profile parses", document is Dictionary, true)
	if not document is Dictionary:
		return
	var team_colours: Dictionary = document.get("common", {}).get(
		"sprite", {}
	).get("teamColours", {})
	for name: String in ArtLibrary.COLOUR_NAMES:
		_expect("%s team colour is configured" % name, team_colours.has(name), true)
	var hybrid: Dictionary = document.get("profiles", {}).get("voxel-sprites", {})
	var terrain_scenes: Dictionary = hybrid.get("assets", {}).get("terrainScenes", {})
	_test_production_sprite_pack(hybrid)
	var expected := [
		"water",
		"road",
		"forest",
		"mountain",
		"sand",
		"ruins",
		"grass",
		"farmland",
		"snow",
		"swamp",
		"hills",
		"bamboo",
		"paved",
	]
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
		"knight|*|scifi|first": "res://art/characters/relay_knight_hd.png",
		"knight|*|scifi|second": "res://art/characters/scifi_knight_female_hd.png",
		"archer|*|scifi|first": "res://art/characters/relay_archer_hd.png",
		"archer|*|scifi|second": "res://art/characters/scifi_archer_female_hd.png",
		"mage|*|scifi|first": "res://art/characters/relay_mage_hd.png",
		"mage|*|scifi|second": "res://art/characters/scifi_mage_female_hd.png",
		"healer|*|scifi|first": "res://art/characters/relay_healer_hd.png",
		"healer|*|scifi|second": "res://art/characters/scifi_healer_female_hd.png",
		"commander|*|scifi|first": "res://art/characters/rook_commander_hd.png",
		"commander|*|scifi|second": "res://art/characters/scifi_commander_female_hd.png",
		"stormcaller|*|scifi|first": "res://art/characters/morrow_stormcaller_hd.png",
		"stormcaller|*|scifi|second": "res://art/characters/scifi_stormcaller_female_hd.png",
		"rogue|*|scifi|first": "res://art/characters/kestrel_rogue_hd.png",
		"rogue|*|scifi|second": "res://art/characters/scifi_rogue_female_hd.png",
		"beast|*|scifi|first": "res://art/characters/relay_beast_hd.png",
		"beast|*|scifi|second": "res://art/characters/scifi_beast_female_hd.png",
		"knight|*|mythical|first": "res://art/characters/mythical_knight_hd.png",
		"knight|*|mythical|second": "res://art/characters/mythical_knight_female_hd.png",
		"archer|*|mythical|first": "res://art/characters/mythical_archer_male_hd.png",
		"archer|*|mythical|second": "res://art/characters/mythical_archer_hd.png",
		"mage|*|mythical|first": "res://art/characters/mythical_mage_male_hd.png",
		"mage|*|mythical|second": "res://art/characters/mythical_mage_female_hd.png",
		"healer|*|mythical|first": "res://art/characters/mythical_healer_male_hd.png",
		"healer|*|mythical|second": "res://art/characters/mythical_healer_female_hd.png",
		"commander|*|mythical|first": "res://art/characters/mythical_commander_male_hd.png",
		"commander|*|mythical|second": "res://art/characters/mythical_commander_female_hd.png",
		"stormcaller|*|mythical|first": "res://art/characters/mythical_stormcaller_male_hd.png",
		"stormcaller|*|mythical|second": "res://art/characters/mythical_stormcaller_female_hd.png",
		"rogue|*|mythical|first": "res://art/characters/mythical_rogue_male_hd.png",
		"rogue|*|mythical|second": "res://art/characters/mythical_rogue_female_hd.png",
		"beast|*|mythical|first": "res://art/characters/mythical_beast_male_hd.png",
		"beast|*|mythical|second": "res://art/characters/mythical_beast_female_hd.png",
	}
	for style: String in ["medieval", "nature", "sengoku", "undead", "pirates"]:
		for archetype: String in ArtLibrary.ARCHETYPE_NAMES:
			for figure: String in ["first", "second"]:
				var body := "male" if figure == "first" else "female"
				expected["%s|*|%s|%s" % [archetype, style, figure]] = (
					"res://art/characters/%s_%s_%s_hd.png" % [style, archetype, body]
				)
	var expected_sequences := {
		"idle": {"frames": [0.0, 1.0], "frames_per_second": 2.0, "loop": true},
		"move": {"frames": [2.0, 3.0], "frames_per_second": 7.0, "loop": true},
		"attack": {"frames": [4.0, 5.0], "frames_per_second": 10.0, "loop": false},
		"ability": {"frames": [6.0]},
		"item": {"frames": [6.0]},
		"talk": {"frames": [0.0, 1.0], "frames_per_second": 2.0, "loop": true},
		"hurt": {"frames": [7.0]},
		"defeat": {"frames": [7.0]},
	}
	var expected_directional := {
		"archer|*|medieval|first": "medieval_archer_male",
		"archer|*|medieval|second": "medieval_archer_female",
		"archer|*|nature|second": "nature_archer_female",
		"archer|*|pirates|second": "pirates_archer_female",
		"archer|*|scifi|second": "scifi_archer_female",
		"archer|*|sengoku|first": "sengoku_archer_male",
		"archer|*|undead|second": "undead_archer_female",
		"beast|*|medieval|first": "medieval_beast_male",
		"beast|*|mythical|first": "mythical_beast_male",
		"beast|*|mythical|second": "mythical_beast_female",
		"beast|*|nature|second": "nature_beast_female",
		"beast|*|scifi|first": "relay_beast",
		"beast|*|sengoku|first": "sengoku_beast_male",
		"beast|*|medieval|second": "medieval_beast_female",
		"commander|*|medieval|second": "medieval_commander_female",
		"commander|*|medieval|first": "medieval_commander_male",
		"commander|*|mythical|second": "mythical_commander_female",
		"commander|*|scifi|second": "scifi_commander_female",
		"commander|*|nature|first": "nature_commander_male",
		"commander|*|nature|second": "nature_commander_female",
		"commander|*|pirates|first": "pirates_commander_male",
		"healer|*|medieval|second": "medieval_healer_female",
		"healer|*|medieval|first": "medieval_healer_male",
		"healer|*|mythical|first": "mythical_healer_male",
		"healer|*|nature|first": "nature_healer_male",
		"healer|*|pirates|first": "pirates_healer_male",
		"healer|*|scifi|second": "scifi_healer_female",
		"healer|*|sengoku|first": "sengoku_healer_male",
		"healer|*|undead|second": "undead_healer_female",
		"knight|*|medieval|first": "medieval_knight_male",
		"knight|*|medieval|second": "medieval_knight_female",
		"knight|*|mythical|first": "mythical_knight",
		"knight|*|mythical|second": "mythical_knight_female",
		"knight|*|nature|second": "nature_knight_female",
		"knight|*|pirates|second": "pirates_knight_female",
		"knight|*|scifi|first": "relay_knight",
		"knight|*|scifi|second": "scifi_knight_female",
		"knight|*|sengoku|first": "sengoku_knight_male",
		"knight|*|undead|second": "undead_knight_female",
		"mage|*|medieval|second": "medieval_mage_female",
		"mage|*|mythical|first": "mythical_mage_male",
		"mage|*|mythical|second": "mythical_mage_female",
		"mage|*|nature|first": "nature_mage_male",
		"mage|*|nature|second": "nature_mage_female",
		"mage|*|pirates|second": "pirates_mage_female",
		"mage|*|scifi|second": "scifi_mage_female",
		"mage|*|sengoku|second": "sengoku_mage_female",
		"mage|*|undead|first": "undead_mage_male",
		"beast|*|nature|first": "nature_beast_male",
		"archer|*|sengoku|second": "sengoku_archer_female",
		"healer|*|undead|first": "undead_healer_male",
		"rogue|*|medieval|second": "medieval_rogue_female",
		"rogue|*|scifi|first": "kestrel_rogue",
		"rogue|*|nature|second": "nature_rogue_female",
		"rogue|*|pirates|first": "pirates_rogue_male",
		"rogue|*|pirates|second": "pirates_rogue_female",
		"rogue|*|sengoku|first": "sengoku_rogue_male",
		"rogue|*|undead|second": "undead_rogue_female",
		"stormcaller|*|medieval|first": "medieval_stormcaller_male",
		"stormcaller|*|mythical|first": "mythical_stormcaller_male",
		"stormcaller|*|pirates|first": "pirates_stormcaller_male",
		"stormcaller|*|sengoku|second": "sengoku_stormcaller_female",
		"stormcaller|*|undead|first": "undead_stormcaller_male",
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
		_expect("%s sprite declares native facing" % key, binding.get("facesRight"), true)
		_expect("%s sprite permits mirroring" % key, binding.get("directionalFlip"), true)
		_expect(
			"%s declares its adjacent team mask" % key,
			String(binding.get("teamMaskPath", "")),
			path.trim_suffix(".png") + "_team_mask.png"
		)
		_expect(
			"%s uses the production action timing" % key,
			binding.get("sequences", {}), expected_sequences
		)
		var directional_value: Variant = binding.get("directionalSheet", {})
		_expect(
			"%s directional coverage is explicit" % key,
			directional_value is Dictionary and not directional_value.is_empty(),
			expected_directional.has(key)
		)
		if expected_directional.has(key) and directional_value is Dictionary:
			var directional: Dictionary = directional_value
			var stem := String(expected_directional[key])
			_expect(
				"%s directional source" % key,
				String(directional.get("path", "")),
				"res://art/characters/%s_directions_hd.png" % stem
			)
			_expect(
				"%s directional team mask" % key,
				String(directional.get("teamMaskPath", "")),
				"res://art/characters/%s_directions_hd_team_mask.png" % stem
			)
			_expect(
				"%s north walk cells" % key,
				directional.get("headings", {}).get("north", {}).get("move", {}).get("frames", []),
				[2.0, 3.0]
			)
			_expect(
				"%s south walk cells" % key,
				directional.get("headings", {}).get("south", {}).get("move", {}).get("frames", []),
				[6.0, 7.0]
			)
		_expect("%s sprite exists" % key, ResourceLoader.exists(path, "Texture2D"), true)
		if ResourceLoader.exists(path, "Texture2D"):
			var texture := ResourceLoader.load(path, "Texture2D") as Texture2D
			_expect("%s sprite loads" % key, texture != null, true)
			if texture != null:
				_expect("%s image width" % key, texture.get_width(), 1772)
				_expect("%s image height" % key, texture.get_height(), 886)

	var art = ArtLibrary.new()
	art.configure_profile(profile)
	var resolution_counts := {
		"scifi": 0,
		"mythical": 0,
		"medieval": 0,
		"nature": 0,
		"sengoku": 0,
		"undead": 0,
		"pirates": 0,
	}
	for key: String in expected:
		if not key.contains("|*|"):
			continue
		var fields := key.split("|")
		var archetype_index := ArtLibrary.ARCHETYPE_NAMES.find(fields[0])
		var style_name := String(fields[2])
		var style_index := ArtLibrary.STYLE_NAMES.find(style_name)
		var figure_index := 0 if fields[3] == "first" else 1
		for colour_index in ArtLibrary.COLOUR_NAMES.size():
			var unit := {
				"archetype": archetype_index,
				"colour": colour_index,
				"style": style_index,
				"figure": figure_index,
			}
			var frame: Dictionary = art.character_frame(unit, "idle")
			var identity := "%s %s %s %s" % [
				style_name, fields[0], fields[3], ArtLibrary.COLOUR_NAMES[colour_index],
			]
			_expect(
				"%s resolves through its wildcard" % identity,
				frame.path, expected[key]
			)
			_expect(
				"%s keeps its requested team colour" % identity,
				frame.team_colour, ArtLibrary.COLOUR_NAMES[colour_index]
			)
			if expected_directional.has(key):
				var north := art.character_frame(unit, "move", 0.0, "north")
				var south := art.character_frame(unit, "move", 0.0, "south")
				_expect(
					"%s resolves authored north" % identity,
					north.path.ends_with("_directions_hd.png"), true
				)
				_expect("%s north starts at cell 2" % identity, north.frame, 2)
				_expect("%s south starts at cell 6" % identity, south.frame, 6)
				_expect(
					"%s compass art cannot be mirrored" % identity,
					north.directional_flip, false
				)
			else:
				_expect(
					"%s north falls back to its side sheet" % identity,
					art.character_frame(unit, "move", 0.0, "north").path,
					expected[key]
				)
			resolution_counts[style_name] += 1
	_expect(
		"all mythical body and faction identities resolve",
		resolution_counts.mythical, 96
	)
	_expect(
		"all sci-fi body and faction identities resolve",
		resolution_counts.scifi, 96
	)
	_expect(
		"all medieval body and faction identities resolve",
		resolution_counts.medieval, 96
	)
	_expect(
		"all nature body and faction identities resolve",
		resolution_counts.nature, 96
	)
	_expect(
		"all sengoku body and faction identities resolve",
		resolution_counts.sengoku, 96
	)
	_expect(
		"all undead body and faction identities resolve",
		resolution_counts.undead, 96
	)
	_expect(
		"all pirates body and faction identities resolve",
		resolution_counts.pirates, 96
	)
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
		{
			"label": "Relay knight",
			"unit": {"archetype": 0, "colour": 0, "style": 1, "figure": 0},
			"path": "res://art/characters/relay_knight_hd.png",
		},
		{
			"label": "Mythical knight, blue, male figure",
			"unit": {"archetype": 0, "colour": 0, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_knight_hd.png",
		},
		{
			"label": "Mythical knight, blue, female figure",
			"unit": {"archetype": 0, "colour": 0, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_knight_female_hd.png",
		},
		{
			"label": "Mythical archer, violet, male figure",
			"unit": {"archetype": 1, "colour": 3, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_archer_male_hd.png",
		},
		{
			"label": "Mythical archer, violet, female figure",
			"unit": {"archetype": 1, "colour": 3, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_archer_hd.png",
		},
		{
			"label": "Mythical mage, green, male figure",
			"unit": {"archetype": 2, "colour": 2, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_mage_male_hd.png",
		},
		{
			"label": "Mythical mage, green, female figure",
			"unit": {"archetype": 2, "colour": 2, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_mage_female_hd.png",
		},
		{
			"label": "Mythical healer, amber, male figure",
			"unit": {"archetype": 4, "colour": 4, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_healer_male_hd.png",
		},
		{
			"label": "Mythical healer, amber, female figure",
			"unit": {"archetype": 4, "colour": 4, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_healer_female_hd.png",
		},
		{
			"label": "Mythical commander, blue, male figure",
			"unit": {"archetype": 5, "colour": 0, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_commander_male_hd.png",
		},
		{
			"label": "Mythical commander, blue, female figure",
			"unit": {"archetype": 5, "colour": 0, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_commander_female_hd.png",
		},
		{
			"label": "Mythical stormcaller, red, male figure",
			"unit": {"archetype": 3, "colour": 1, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_stormcaller_male_hd.png",
		},
		{
			"label": "Mythical stormcaller, red, female figure",
			"unit": {"archetype": 3, "colour": 1, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_stormcaller_female_hd.png",
		},
		{
			"label": "Mythical rogue, violet, male figure",
			"unit": {"archetype": 6, "colour": 3, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_rogue_male_hd.png",
		},
		{
			"label": "Mythical rogue, violet, female figure",
			"unit": {"archetype": 6, "colour": 3, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_rogue_female_hd.png",
		},
		{
			"label": "Mythical beast, bone, male figure",
			"unit": {"archetype": 7, "colour": 5, "style": 2, "figure": 0},
			"path": "res://art/characters/mythical_beast_male_hd.png",
		},
		{
			"label": "Mythical beast, bone, female figure",
			"unit": {"archetype": 7, "colour": 5, "style": 2, "figure": 1},
			"path": "res://art/characters/mythical_beast_female_hd.png",
		},
		{
			"label": "Relay archer",
			"unit": {"archetype": 1, "colour": 0, "style": 1, "figure": 0},
			"path": "res://art/characters/relay_archer_hd.png",
		},
		{
			"label": "Relay mage",
			"unit": {"archetype": 2, "colour": 0, "style": 1, "figure": 0},
			"path": "res://art/characters/relay_mage_hd.png",
		},
		{
			"label": "Relay healer",
			"unit": {"archetype": 4, "colour": 0, "style": 1, "figure": 0},
			"path": "res://art/characters/relay_healer_hd.png",
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

	var forest_block: Array = [
		2, 2, 2,
		2, 2, 2,
		2, 2, 2,
	]
	var centre: Dictionary = TerrainSceneSelector.cluster_context(
		forest_block, 3, 3, 1, 1, 2
	)
	_expect("forest centre sees all four matching neighbours", centre.same_mask, 15)
	_expect("forest centre is marked as interior", centre.interior, true)
	_expect("forest centre counts four matching neighbours", centre.same_neighbours, 4)
	var corner: Dictionary = TerrainSceneSelector.cluster_context(
		forest_block, 3, 3, 0, 0, 2
	)
	_expect("forest corner exposes its outer edges", corner.same_mask, 6)
	_expect("forest corner is not marked as interior", corner.interior, false)

	var mixed_woodland: Array = [
		9, 11, 9,
		11, 2, 2,
		9, 9, 9,
	]
	var woodland_edge: Dictionary = TerrainSceneSelector.cluster_context(
		mixed_woodland, 3, 3, 1, 1, 2
	)
	_expect("woodland context distinguishes exact terrain", woodland_edge.same_mask, 2)
	_expect("woodland context includes bamboo family", woodland_edge.family_mask, 11)
	_expect("woodland family count is deterministic", woodland_edge.family_neighbours, 3)
	var clustered: Dictionary = TerrainSceneSelector.select(
		binding, 0, 0, 2, centre
	)
	_expect("cluster-aware variant selection uses neighbours", clustered.scene, "base.tscn")
	_expect(
		"cluster-aware selection repeats exactly",
		TerrainSceneSelector.select(binding, 0, 0, 2, centre), clustered
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
	_expect("%s scene has a shared foundation" % label, foundation != null, true)
	if foundation != null and foundation.mesh is BoxMesh:
		var foundation_mesh := foundation.mesh as BoxMesh
		_expect(
			"%s foundation closes horizontal seams" % label,
			is_equal_approx(foundation_mesh.size.x, 1.0)
			and is_equal_approx(foundation_mesh.size.z, 1.0),
			true
		)
	for surface_name: String in [
		"Ground", "Soil", "Snow", "Mud", "Depth", "Surface",
		"WaterDepth", "Slab"
	]:
		var surface := instance.get_node_or_null(surface_name) as MeshInstance3D
		if surface == null:
			continue
		if surface.mesh is BoxMesh:
			var box := surface.mesh as BoxMesh
			_expect(
				"%s %s closes horizontal seams" % [label, surface_name],
				box.size.x >= 0.999 and box.size.z >= 0.999,
				true
			)
		elif surface.mesh is PlaneMesh:
			var plane := surface.mesh as PlaneMesh
			_expect(
				"%s %s closes horizontal seams" % [label, surface_name],
				plane.size.x >= 0.999 and plane.size.y >= 0.999,
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
