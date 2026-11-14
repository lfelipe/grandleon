// SPDX-License-Identifier: MIT
shader_type spatial;
render_mode cull_disabled;
uniform vec4 blade_colour : source_color = vec4(0.35, 0.45, 0.22, 1.0);
uniform float motion_strength = 1.0;
uniform float animation_time = 0.0;
void vertex() {
	vec3 at = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float wind = sin(animation_time * 0.9 + at.x * 1.7 + at.z * 1.1);
	VERTEX.x += wind * UV.y * UV.y * 0.17 * motion_strength;
}
void fragment() {
	ALBEDO = blade_colour.rgb * mix(0.62, 1.15, UV.y);
	ROUGHNESS = 0.94;
}
