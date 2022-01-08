#version 460 core

layout (location = 0) out vec4 radiance;

in vec3 FragPos;
in vec2 TexCoord;
in vec3 Normal;

void main()
{
	vec3 light_dir = normalize(vec3(10.0, 1.0, -1.0));
	vec3 lighting = max(dot(Normal, light_dir), 0.0) * vec3(1, 1, 1);

	radiance = vec4(lighting, 1.0);
}