// SPDX-License-Identifier: MIT
shader_type spatial;
render_mode unshaded, depth_draw_never, cull_disabled;
uniform vec4 tint : source_color;
uniform bool outlined = false;
void fragment() {
	float distance_to_edge = min(min(UV.x, 1.0 - UV.x), min(UV.y, 1.0 - UV.y));
	float border = outlined ? 1.0 - smoothstep(0.035, 0.065, distance_to_edge) : 0.0;
	ALBEDO = tint.rgb;
	ALPHA = mix(tint.a, 0.9, border);
}
