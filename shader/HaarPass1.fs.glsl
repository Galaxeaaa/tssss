#version 460 core

layout (location = 0) out vec4 world_pos;

layout(binding = 0) uniform sampler2D diffuse;

in vec3 FragPos;
in vec2 TexCoord;

void main()
{             
	// world_pos = texture(diffuse, TexCoord);
	world_pos = vec4(FragPos, 1.0);
}