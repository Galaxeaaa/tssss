#version 460 core
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;

out vec3 FragPos;
out vec2 TexCoord;

uniform mat4 model;

void main()
{
    vec4 worldPos = model * vec4(inPos, 1.0);
    FragPos = worldPos.xyz;
	TexCoord = inTexCoord;
	
	gl_Position = vec4(inTexCoord * 2.0 - 1.0, 0.0, 1.0);
}