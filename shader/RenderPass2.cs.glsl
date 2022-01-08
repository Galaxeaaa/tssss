#version 450

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h;

layout(rgba32f, binding = 0) uniform image2D radiance_map;
layout(rgba32f, binding = 1) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

void haar2D(uint height, uint width, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp);

void main() {
	uint tex_w = imageSize(radiance_map).x;
	uint tex_h = imageSize(radiance_map).y;
	uint size_coef_array = coef_w * coef_h;
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
	// Transform radiance map.
	haar2D(tex_h, tex_w, radiance_map, haar_wavelet_temp_image);
	barrier();
	// Store some coefficients.
	if (GlobalInvocationIndex < coef_h * coef_w)
	{
		uint index_coef = GlobalInvocationIndex;
		radiance_coef.data[index_coef] = vec4(imageLoad(radiance_map, ivec2(index_coef / coef_w, index_coef % coef_w)));
	}
	barrier();
}

void haar2D(uint m, uint n, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp)
{
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

	uint i;
	uint j;
	uint k;
	float s = sqrt(2.0);

	// copy img into tmp
	j = GlobalInvocationIndex;
	for (i = 0; i < m; i++)
	{
		imageStore(tmp, ivec2(i, j), imageLoad(img, ivec2(i, j)));
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
			imageStore(tmp, ivec2(i, j), (imageLoad(img, ivec2(2 * i, j)) + imageLoad(img, ivec2(2 * i + 1, j))) / s);
			imageStore(tmp, ivec2(k + i, j), (imageLoad(img, ivec2(2 * i, j)) - imageLoad(img, ivec2(2 * i + 1, j))) / s);
		}
		for (i = 0; i < 2 * k; i++)
		{
			imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
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
			imageStore(tmp, ivec2(i, j), (imageLoad(img, ivec2(i, 2 * j)) + imageLoad(img, ivec2(i, 2 * j + 1))) / s);
			imageStore(tmp, ivec2(i, k + j), (imageLoad(img, ivec2(i, 2 * j)) - imageLoad(img, ivec2(i, 2 * j + 1))) / s);
		}
		for (j = 0; j < 2 * k; j++)
		{
			imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
		}
	}
	barrier();

	return;
}
