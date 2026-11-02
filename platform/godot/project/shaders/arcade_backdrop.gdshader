shader_type canvas_item;

uniform vec4 top_colour : source_color = vec4(0.025, 0.055, 0.09, 1.0);
uniform vec4 bottom_colour : source_color = vec4(0.008, 0.014, 0.025, 1.0);
uniform vec4 glow_colour : source_color = vec4(0.09, 0.44, 0.58, 1.0);

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void fragment() {
	vec2 uv = UV;
	vec3 colour = mix(top_colour.rgb, bottom_colour.rgb, smoothstep(0.0, 1.0, uv.y));
	float horizon = exp(-pow((uv.y - 0.37) * 5.0, 2.0));
	colour += glow_colour.rgb * horizon * 0.16;
	float rays = pow(max(0.0, sin((uv.x - 0.5) * 13.0 + uv.y * 4.0)), 12.0);
	colour += glow_colour.rgb * rays * horizon * 0.035;
	float grain = hash(floor(uv * vec2(960.0, 540.0)) + floor(TIME * 12.0));
	colour += (grain - 0.5) * 0.012;
	float vignette = smoothstep(0.92, 0.26, length((uv - 0.5) * vec2(1.05, 0.82)));
	colour *= mix(0.48, 1.0, vignette);
	COLOR = vec4(colour, 1.0);
}
