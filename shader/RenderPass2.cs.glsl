#version 460

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h, tex_w, tex_h;
shared int WorkGroupSize;
shared int size_coef_array;

layout(rgba32f, binding = 0) uniform image2D radiance_map;
layout(rgba32f, binding = 1) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

void gauss();
void haar2D();

void main() {
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	if (GlobalInvocationIndex == 0)
	{
		WorkGroupSize = int(gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z);
		size_coef_array = coef_w * coef_h;
	}
	barrier();
	// Gaussian blur.
	gauss();
	barrier();
	// Transform radiance map.
	haar2D();
	barrier();
	// Store some coefficients.
	for (int index_coef = GlobalInvocationIndex; index_coef < coef_h * coef_w; index_coef += WorkGroupSize)
	{
		int row = index_coef / coef_w;
		int col = index_coef % coef_w;
		radiance_coef.data[index_coef] = vec4(imageLoad(radiance_map, ivec2(row, col)).xyz, 0);
	}
	barrier();
}

void gauss()
{
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);	
	// copy radiance_map into haar_wavelet_temp_image
	for (int i = GlobalInvocationIndex; i < tex_w * tex_h; i += WorkGroupSize)
	{
		int row = i / tex_w;
		int col = i % tex_w;
		imageStore(haar_wavelet_temp_image, ivec2(row, col), imageLoad(radiance_map, ivec2(row, col)));
	}
	barrier();
	// Transform rows.
	for (int row = GlobalInvocationIndex; row < tex_h; row += WorkGroupSize)
	{
		for (int col = 0; col < tex_w; col++)
		{
			float sum = 0;
			sum += imageLoad(radiance_map, ivec2(row, col)).r * weight[0];
			for (int i = 1; i < 5; i++)
			{
				if (col - i >= 0)
				{
					sum += imageLoad(radiance_map, ivec2(row, col - i)).r * weight[i];
				}
				if (col + i < tex_h)
				{
					sum += imageLoad(radiance_map, ivec2(row, col + i)).r * weight[i];
				}

			}
			imageStore(haar_wavelet_temp_image, ivec2(row, col), vec4(sum, 0, 0, 0));
		}
	}
	barrier();
	// Transform cols.
	for (int col = GlobalInvocationIndex; col < tex_w; col += WorkGroupSize)
	{
		for (int row = 0; row < tex_w; row++)
		{
			float sum = 0;
			sum += imageLoad(haar_wavelet_temp_image, ivec2(row, col)).r * weight[0];
			for (int i = 1; i < 5; i++)
			{
				if (row - i >= 0)
				{
					sum += imageLoad(haar_wavelet_temp_image, ivec2(row - i, col)).r * weight[i];
				}
				if (row + i < tex_h)
				{
					sum += imageLoad(haar_wavelet_temp_image, ivec2(row + i, col)).r * weight[i];
				}

			}
			imageStore(radiance_map, ivec2(row, col), vec4(sum, 0, 0, 0));
		}
	}
	barrier();
}

void haar2D()
{
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	int i, j, k;
	float s = sqrt(2.0);
	// copy radiance_map into haar_wavelet_temp_image
	for (i = GlobalInvocationIndex; i < tex_w * tex_h; i += WorkGroupSize)
	{
		int row = i / tex_w;
		int col = i % tex_w;
		imageStore(haar_wavelet_temp_image, ivec2(row, col), imageLoad(radiance_map, ivec2(row, col)));
	}
	barrier();
	// Determine K, the largest power of 2 such that K <= M.
	k = 1;
	while (k * 2 <= tex_h)
	{
		k = k * 2;
	}
	// Transform all columns.
	for (j = GlobalInvocationIndex; j < tex_w; j += WorkGroupSize)
	{
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
	}
	barrier();
	// Determine K, the largest power of 2 such that K <= N.
	k = 1;
	while (k * 2 <= tex_w)
	{
		k = k * 2;
	}
	// Transform all rows.
	for (i = GlobalInvocationIndex; i < tex_h; i += WorkGroupSize)
	{
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
	}
	barrier();

	return;
}
