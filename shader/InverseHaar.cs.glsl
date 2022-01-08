#version 450

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h;

layout(rgba32f, binding = 0) uniform image2D radiance_map;
layout(rgba32f, binding = 1) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

void haar2dInverse(uint height, uint width, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp);

void main() {
	uint tex_w = imageSize(radiance_map).x;
	uint tex_h = imageSize(radiance_map).y;
	uint size_coef_array = coef_w * coef_h;
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
	uint i, j;

	// Black radiance map
	j = GlobalInvocationIndex;
	for (i = 0; i < tex_h; i++)
	{
		imageStore(radiance_map, ivec2(i, j), vec4(0, 0, 0, 1));
	}
	barrier();

	if (GlobalInvocationIndex == 0)
	{
		for (uint row = 0; row < coef_h; row++)
		{
			for (uint col = 0; col < coef_w; col++)
			{
				uint index_coef = row * coef_w + col;
				imageStore(radiance_map, ivec2(row, col), radiance_coef.data[index_coef]);
			}
		}
	}
	barrier();
	// Inversely transform radiance map.
	haar2dInverse(tex_h, tex_w, radiance_map, haar_wavelet_temp_image);
	barrier();
}

void haar2dInverse(uint m, uint n, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp)
{
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

	uint i;
	uint j;
	uint k;
	float s = sqrt(2.0);

	// Copy img into tmp
	j = GlobalInvocationIndex;
	for (i = 0; i < m; i++)
	{
		imageStore(tmp, ivec2(i, j), imageLoad(img, ivec2(i, j)));
	}
	// Inverse transform of all rows.
	k = 1;
	i = GlobalInvocationIndex;
	while (k * 2 <= n)
	{
		for (j = 0; j < k; j++)
		{
			imageStore(tmp, ivec2(i, 2 * j), (imageLoad(img, ivec2(i, j)) + imageLoad(img, ivec2(i, k + j))) / s);
			imageStore(tmp, ivec2(i, 2 * j + 1), (imageLoad(img, ivec2(i, j)) - imageLoad(img, ivec2(i, k + j))) / s);
		}

		for (j = 0; j < 2 * k; j++)
		{
			imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
		}
		k = k * 2;
	}
	// Inverse transform of all columns.
	k = 1;
	j = GlobalInvocationIndex;
	while (k * 2 <= m)
	{
		for (i = 0; i < k; i++)
		{
			imageStore(tmp, ivec2(2 * i, j), (imageLoad(img, ivec2(i, j)) + imageLoad(img, ivec2(k + i, j))) / s);
			imageStore(tmp, ivec2(2 * i + 1, j), (imageLoad(img, ivec2(i, j)) - imageLoad(img, ivec2(k + i, j))) / s);
		}

		for (i = 0; i < 2 * k; i++)
		{
			imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
		}
		k = k * 2;
	}

	return;
}