#version 460 core

// layout(r32f, binding = 0) uniform image2D dbgMap;

layout (location = 0) out vec4 FragColor;
in vec3 FragPos;
in vec2 TexCoord;
in vec3 Normal;

layout (binding = 0) uniform sampler2D texture_diffuse;

void main()
{             
    vec3 Diffuse = texture(texture_diffuse, TexCoord).rgb;
	vec3 lightDir = normalize(vec3(10.0, 1.0, -1.0));
	vec3 lightColor = vec3(1.0, 1.0, 1.0);
	
	vec3 diffuse = max(dot(Normal, lightDir), 0.0) * Diffuse * lightColor;
	vec3 lighting = diffuse;

	FragColor = vec4(lighting, 1.0);
}