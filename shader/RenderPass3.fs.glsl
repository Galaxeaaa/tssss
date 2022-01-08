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

vec3 colorAt(int row, int col);
vec3 map(vec3 value, vec3 inMin, vec3 inMax, vec3 outMin, vec3 outMax);

void main()
{
	vec3 lighting;
	// lighting = colorAt(int(floor(TexCoords.x * tex_w)), int(floor(TexCoords.y * tex_h)));
	lighting = texture(diffuse_map, TexCoords).rgb;
	// lighting = vec3(1, 1, 1);

	FragColor = vec4(lighting, 1.0);
}

vec3 colorAt(int row, int col)
{
	int kernel_index_base = (row * tex_w + col) * coef_h * coef_w;
	vec3 color = vec3(0, 0, 0);
	for (int i = 0; i < coef_h * coef_w; i++)
	{
		color += radiance_coef.data[i].rgb * kernel_coef.data[kernel_index_base + i].r;
	}
	return color;
}

vec3 map(vec3 value, vec3 inMin, vec3 inMax, vec3 outMin, vec3 outMax)
{
	return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
}