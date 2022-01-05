#version 460 core

layout(binding = 0) uniform sampler2D tex;
// uniform sampler2D radiance_map_after_wavelet;
layout(rgba32f, binding = 0) uniform image2D radiance_map;
layout(rgba32f, binding = 1) uniform image2D world_pos_map;
layout(rgba32f, binding = 2) uniform image2D kernel;

layout (location = 0) out vec4 FragColor;

in vec2 TexCoords;

void main()
{             
	// vec4 color = imageLoad(radiance_map, ivec2(TexCoords * imageSize(radiance_map)));
	// vec4 color = imageLoad(world_pos_map, ivec2(TexCoords * imageSize(world_pos_map)));
	vec4 color = imageLoad(kernel, ivec2(TexCoords * imageSize(kernel)));
	// vec4 color = texture(tex, TexCoords);
	// vec4 color = texture(radiance_map_after_wavelet, TexCoords);

	FragColor = color;
}