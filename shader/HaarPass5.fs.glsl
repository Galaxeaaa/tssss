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

uniform vec3 view_pos;
uniform sampler2D radiance_map_after_sss;

void main()
{
	vec3 lighting = texture(radiance_map_after_sss, TexCoords).rgb;

	FragColor = vec4(lighting, 1.0);
}