shader_type spatial;
render_mode blend_mix, depth_prepass_alpha, cull_disabled;

uniform vec3 deep_colour : source_color = vec3(0.035, 0.24, 0.34);
uniform vec3 shallow_colour : source_color = vec3(0.10, 0.50, 0.62);
uniform vec3 glint_colour : source_color = vec3(0.30, 0.88, 0.94);

void vertex() {
	float x_wave = sin((UV.x + TIME * 0.07) * 12.56637);
	float y_wave = cos((UV.y - TIME * 0.05) * 12.56637);
	VERTEX.y += (x_wave + y_wave) * 0.006;
}

void fragment() {
	float diagonal = sin((UV.x + UV.y + TIME * 0.09) * 12.56637);
	float crossing = cos((UV.x - UV.y - TIME * 0.06) * 18.84956);
	float ripple = diagonal * 0.5 + crossing * 0.5;
	float glint = smoothstep(0.76, 0.98, ripple);
	ALBEDO = mix(deep_colour, shallow_colour, 0.48 + ripple * 0.12);
	EMISSION = glint_colour * glint * 0.22;
	ROUGHNESS = 0.16;
	METALLIC = 0.08;
	ALPHA = 0.9;
}
