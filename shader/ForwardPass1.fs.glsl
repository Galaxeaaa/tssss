#version 450 core

layout(location = 0) out vec4 FragColor;

in vec3 FragPos;
in vec2 TexCoords;
in vec3 Normal;

layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

layout(std430, binding = 1) buffer KernelCoef
{
	vec4 data[];
} kernel_coef;

uniform int tex_h;
uniform int tex_w;
uniform int coef_h;
uniform int coef_w;
uniform vec3 view_pos;
layout(binding = 0) uniform sampler2D diffuse_map;

vec3 map(vec3 value, vec3 inMin, vec3 inMax, vec3 outMin, vec3 outMax);

void main()
{
	vec3 lighting;
	// lighting = colorAt(int(floor(TexCoords.x * tex_w)), int(floor(TexCoords.y * tex_h)));
	lighting = texture(diffuse_map, TexCoords).rgb;

	FragColor = vec4(lighting, 1.0);
}

vec3 map(vec3 value, vec3 inMin, vec3 inMax, vec3 outMin, vec3 outMax)
{
	return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
}