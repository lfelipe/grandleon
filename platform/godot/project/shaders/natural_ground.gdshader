// SPDX-License-Identifier: MIT
shader_type spatial;
render_mode diffuse_burley;
uniform int terrain_kind = 9;
uniform float motion_strength = 1.0;
uniform float animation_time = 0.0;
varying vec3 world_position;

float hash_value(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float grain(vec2 p) {
	vec2 cell = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash_value(cell), hash_value(cell + vec2(1, 0)), f.x),
		mix(hash_value(cell + vec2(0, 1)), hash_value(cell + vec2(1, 1)), f.x), f.y);
}
void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
void fragment() {
	vec2 p = world_position.xz;
	float broad = grain(p * 1.4);
	float detail = grain(p * 19.0);
	float fine = grain(p * 73.0);
	vec3 colour = COLOR.rgb * (0.83 + broad * 0.20 + detail * 0.14 + fine * 0.055);
	if (terrain_kind == 0) {
		float t = animation_time * motion_strength;
		float wave = sin(p.x * 12.0 + p.y * 5.0 + t * 0.6) * sin(p.y * 17.0 - t * 0.48);
		colour *= 0.92 + wave * 0.07;
		colour += vec3(0.05, 0.08, 0.09) * pow(max(0.0, wave), 10.0);
		ROUGHNESS = 0.26;
		SPECULAR = 0.65;
	} else {
		if (terrain_kind == 12 || terrain_kind == 8) {
			float rows = terrain_kind == 12 ? 1.55 : 1.25;
			vec2 stone = fract(vec2(p.x * rows + mod(floor(p.y * rows), 2.0) * 0.5, p.y * rows));
			float edge = min(min(stone.x, 1.0 - stone.x), min(stone.y, 1.0 - stone.y));
			colour *= mix(0.70, 1.0, smoothstep(0.015, 0.05, edge));
		} else if (terrain_kind == 10) {
			float row = sin(p.x * 24.0 + grain(p * 2.0) * 0.35);
			colour *= 0.87 + row * 0.13;
		} else if (terrain_kind == 4 || terrain_kind == 5) {
			colour *= 0.96 + sin(p.x * 14.0 + grain(p * 0.7) * 7.0) * 0.035;
		} else if (terrain_kind == 1) {
			colour *= 0.92 + grain(p * vec2(14.0, 0.8)) * 0.12;
		}
		ROUGHNESS = terrain_kind == 6 ? 0.65 : 0.94;
		SPECULAR = 0.25;
	}
	ALBEDO = colour;
}
