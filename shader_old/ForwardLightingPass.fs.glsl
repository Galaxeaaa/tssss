#version 450 core

#define MAX_NLIGHTS_PER_TILE 2048
#define TILE_SIZE 16

// layout(r32f, binding = 0) uniform image2D dbgMap;

struct Light {
    vec4 Position;
    vec4 Color;
    float Radius;
};

struct DebugStruct {
	int nlight;
	float mindepth;
	float maxdepth;
};

layout(std430, binding = 10) readonly buffer LightBuffer {
	Light data[];
} lightBuffer;

layout(std430, binding = 1) readonly buffer visible_lights_indices {
	int lights_indices[];
};

layout(std430, binding = 2) buffer DebugBuffer {
	DebugStruct data[];
} debugBuffer;

out vec4 FragColor;
in vec3 FragPos;
in vec2 TexCoords;
in vec3 Normal;

uniform vec3 viewPos;
uniform int nTilesX;
uniform int nTilesY;
uniform int V;
uniform int N_LIGHTS;

uniform sampler2D texture_diffuse1;
uniform sampler2D texture_specular1;
uniform sampler2D fDepth;

void main()
{             
    vec3 Diffuse = texture(texture_diffuse1, TexCoords).rgb;
    float Specular = texture(texture_specular1, TexCoords).r;
    vec3 lighting  = Diffuse * 0.1; // hard-coded ambient component
    vec3 viewDir  = normalize(viewPos - FragPos);
	
	ivec2 location = ivec2(gl_FragCoord.xy);
	ivec2 tileID = location / ivec2(TILE_SIZE, TILE_SIZE);
	uint index = tileID.y * nTilesX + tileID.x;
    uint offset = index * MAX_NLIGHTS_PER_TILE;

    for(int j = 0; j < MAX_NLIGHTS_PER_TILE; j++)
    {
		int i = lights_indices[offset + j];
		if (i == -1)
			break;

		// vec3 position = vec3(lights[i].Position);
		// vec3 color = vec3(lights[i].Color);
		// float radius = lights[i].Radius;
		vec3 position = vec3(lightBuffer.data[i].Position);
		vec3 color = vec3(lightBuffer.data[i].Color);
		float radius = lightBuffer.data[i].Radius;

        float d = length(position - FragPos);
        if (d <= radius)
        {
            // diffuse
            vec3 lightDir = normalize(position - FragPos);
            vec3 diffuse = max(dot(Normal, lightDir), 0.0) * Diffuse * color;
            // specular
            vec3 halfwayDir = normalize(lightDir + viewDir);  
            float spec = pow(max(dot(Normal, halfwayDir), 0.0), 16.0);
            vec3 specular = color * spec * Specular;
            // attenuation
            float attenuation = 1.0 / (1.0 + d * d) - 1.0 / (1.0 + radius * radius);
			// float attenuation = 1;
            diffuse *= attenuation;
            specular *= attenuation;
            lighting += diffuse + specular;
        }
    }
	if (V == 0)
    	FragColor = vec4(lighting, 1.0);
	else if (V == 1)
		FragColor = vec4(0, float(debugBuffer.data[index].nlight) / N_LIGHTS, 0, 1);
	else if (V == 2)
		FragColor = vec4(float(debugBuffer.data[index].maxdepth), float(debugBuffer.data[index].maxdepth), float(debugBuffer.data[index].maxdepth), 1);
	else if (V == 3)
		FragColor = vec4(float(debugBuffer.data[index].mindepth), float(debugBuffer.data[index].mindepth), float(debugBuffer.data[index].mindepth), 1);
}