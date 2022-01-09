#version 460

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h;

layout(rgba32f, binding = 0) uniform image2D radiance_map;
layout(rgba32f, binding = 1) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

void haar2D(uint height, uint width);

void main() {
	uint tex_w = imageSize(radiance_map).x;
	uint tex_h = imageSize(radiance_map).y;
	uint size_coef_array = coef_w * coef_h;
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
	// Transform radiance map.
	haar2D(tex_h, tex_w);
	barrier();
	// Store some coefficients.
	if (GlobalInvocationIndex < coef_h * coef_w)
	{
		uint index_coef = GlobalInvocationIndex;
		radiance_coef.data[index_coef] = vec4(imageLoad(radiance_map, ivec2(index_coef / coef_w, index_coef % coef_w)));
	}
	barrier();
}

void haar2D(uint m, uint n)
{
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

	uint i;
	uint j;
	uint k;
	float s = sqrt(2.0);

	// copy radiance_map into haar_wavelet_temp_image
	j = GlobalInvocationIndex;
	for (i = 0; i < m; i++)
	{
		imageStore(haar_wavelet_temp_image, ivec2(i, j), imageLoad(radiance_map, ivec2(i, j)));
	}
	barrier();
	// Determine K, the largest power of 2 such that K <= M.
	k = 1;
	while (k * 2 <= m)
	{
		k = k * 2;
	}
	// Transform all columns.
	j = GlobalInvocationIndex;
	while (1 < k)
	{
		k = k / 2;
		for (i = 0; i < k; i++)
		{
			imageStore(haar_wavelet_temp_image, ivec2(i, j), (imageLoad(radiance_map, ivec2(2 * i, j)) + imageLoad(radiance_map, ivec2(2 * i + 1, j))) / s);
			imageStore(haar_wavelet_temp_image, ivec2(k + i, j), (imageLoad(radiance_map, ivec2(2 * i, j)) - imageLoad(radiance_map, ivec2(2 * i + 1, j))) / s);
		}
		for (i = 0; i < 2 * k; i++)
		{
			imageStore(radiance_map, ivec2(i, j), imageLoad(haar_wavelet_temp_image, ivec2(i, j)));
		}
	}
	barrier();
	// Determine K, the largest power of 2 such that K <= N.
	k = 1;
	while (k * 2 <= n)
	{
		k = k * 2;
	}
	// Transform all rows.
	i = GlobalInvocationIndex;
	while (1 < k)
	{
		k = k / 2;
		for (j = 0; j < k; j++)
		{
			imageStore(haar_wavelet_temp_image, ivec2(i, j), (imageLoad(radiance_map, ivec2(i, 2 * j)) + imageLoad(radiance_map, ivec2(i, 2 * j + 1))) / s);
			imageStore(haar_wavelet_temp_image, ivec2(i, k + j), (imageLoad(radiance_map, ivec2(i, 2 * j)) - imageLoad(radiance_map, ivec2(i, 2 * j + 1))) / s);
		}
		for (j = 0; j < 2 * k; j++)
		{
			imageStore(radiance_map, ivec2(i, j), imageLoad(haar_wavelet_temp_image, ivec2(i, j)));
		}
	}
	barrier();

	return;
}
