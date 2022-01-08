#version 460 core

layout (location = 0) out vec4 world_pos;

in vec3 FragPos;

void main()
{             
	world_pos = vec4(FragPos, 1.0);
}