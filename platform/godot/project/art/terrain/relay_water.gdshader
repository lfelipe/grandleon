shader_type spatial;
render_mode blend_mix, depth_prepass_alpha, cull_disabled;

uniform float ambient_strength : hint_range(0.0, 1.0) = 1.0;
varying vec2 world_xz;

uniform vec3 deep_colour : source_color = vec3(0.035, 0.24, 0.34);
uniform vec3 shallow_colour : source_color = vec3(0.10, 0.50, 0.62);
uniform vec3 glint_colour : source_color = vec3(0.30, 0.88, 0.94);

void vertex() {
	world_xz = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xz;
	float x_wave = sin(dot(world_xz, vec2(1.7, 0.7)) + TIME * 0.88);
	float y_wave = cos(dot(world_xz, vec2(-0.8, 2.2)) - TIME * 0.65);
	VERTEX.y += (x_wave + y_wave) * 0.004 * ambient_strength;
}

void fragment() {
	float diagonal = sin(dot(world_xz, vec2(5.8, 4.7)) + TIME * 0.95 * ambient_strength);
	float crossing = cos(dot(world_xz, vec2(7.3, -6.1)) - TIME * 0.72 * ambient_strength);
	float ripple = diagonal * 0.5 + crossing * 0.5;
	float glint = smoothstep(0.76, 0.98, ripple);
	ALBEDO = mix(deep_colour, shallow_colour, 0.48 + ripple * 0.12);
	EMISSION = glint_colour * glint * 0.22;
	ROUGHNESS = 0.16;
	METALLIC = 0.08;
	ALPHA = 0.9;
}
