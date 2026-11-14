// SPDX-License-Identifier: MIT
shader_type spatial;
render_mode unshaded, cull_disabled, depth_prepass_alpha;

uniform sampler2D source_texture : source_color, filter_linear_mipmap;
uniform int season = 0;

void vertex() {
	// Match Sprite3D's full billboard while retaining its authored size and feet.
	vec3 scale = vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
		vec4(INV_VIEW_MATRIX[0].xyz * scale.x, 0.0),
		vec4(INV_VIEW_MATRIX[1].xyz * scale.y, 0.0),
		vec4(INV_VIEW_MATRIX[2].xyz * scale.z, 0.0), MODEL_MATRIX[3]);
}

void fragment() {
	vec4 source = texture(source_texture, UV);
	float leaf = smoothstep(0.005, 0.07, source.g - max(source.r, source.b));
	float light = dot(source.rgb, vec3(0.299, 0.587, 0.114));
	vec3 foliage = source.rgb;
	if (season == 1) {
		foliage = light * vec3(1.65, 0.94, 0.28);
	} else if (season == 2) {
		foliage = vec3(light * 0.60 + 0.34, light * 0.65 + 0.36, light * 0.66 + 0.36);
	} else if (season == 3) {
		foliage = light * vec3(0.60, 0.55, 0.48);
	}
	ALBEDO = mix(source.rgb, foliage, leaf);
	ALPHA = source.a;
	ALPHA_SCISSOR_THRESHOLD = 0.45;
}
