#version 460 core

layout (location = 0) out vec4 radiance;
layout (location = 1) out vec4 world_pos;

in vec3 FragPos;
in vec2 TexCoord;
in vec3 Normal;

uniform sampler2D texture_diffuse;

void main()
{             
	// Lighting
	// --------------------------------
    vec3 diffuse_color = texture(texture_diffuse, TexCoord).rgb;
	vec3 light_dir = normalize(vec3(10.0, 1.0, -1.0));
	vec3 light_color = vec3(1.0, 1.0, 1.0);
	vec3 diffuse = max(dot(Normal, light_dir), 0.0) * diffuse_color * light_color;
	vec3 lighting = diffuse;

	radiance = vec4(lighting, 1.0);
	world_pos = vec4(FragPos, 1.0);
}