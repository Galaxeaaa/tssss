#version 460

#define M_PI 3.1415926535897932384626433832795

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h, tex_w, tex_h;
uniform ivec2 index_kernel_iv;
shared int WorkGroupSize;
shared int size_coef_array;
shared int index_kernel_row, index_kernel_col;
shared int index_kernel;
shared vec3 pos_i_j;

layout(rgba32f, binding = 0) uniform image2D world_pos_map;
layout(rgba32f, binding = 1) uniform image2D kernel;
layout(rgba32f, binding = 2) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 1) buffer KernelCoef {
	vec4 data[];
} kernel_coef;

void haar2D();
float fDiffuseProfile(float r, float A = 0.6, float s = 4.031441);

void main() {
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	if (GlobalInvocationIndex == 0)
	{
		WorkGroupSize = int(gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z);
		size_coef_array = coef_w * coef_h;
		index_kernel_row = index_kernel_iv.x;
		index_kernel_col = index_kernel_iv.y;
		index_kernel = index_kernel_row * tex_w + index_kernel_col;
		pos_i_j = imageLoad(world_pos_map, ivec2(index_kernel_row, index_kernel_col)).xyz;
	}
	barrier();
	// Compute kernel at (i, j).
	for (int index_texel = GlobalInvocationIndex; index_texel < tex_h * tex_w; index_texel += WorkGroupSize)
	{
		int row = index_texel / tex_w;
		int col = index_texel % tex_w;
		if (pos_i_j == vec3(0, 0, 0))
		{
			imageStore(kernel, ivec2(row, col), vec4(0, 0, 0, 0));
		}
		else
		{
			vec3 pos_row_col = vec3(imageLoad(world_pos_map, ivec2(row, col)));
			float l = length(pos_i_j - pos_row_col);
			imageStore(kernel, ivec2(row, col), vec4(fDiffuseProfile(l), 0, 0, 0));
		}
	}
	barrier();
	// Transform kernel.
	haar2D();
	barrier();
	// Store some coefficients.
	for (int index_coef = GlobalInvocationIndex; index_coef < coef_h * coef_w; index_coef += WorkGroupSize)
	{
		kernel_coef.data[index_coef] = vec4(imageLoad(kernel, ivec2(index_coef / coef_w, index_coef % coef_w)));
	}
	barrier();
}

void haar2D()
{
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	int i, j, k;
	float s = sqrt(2.0);
	// copy kernel into haar_wavelet_temp_image
	for (i = GlobalInvocationIndex; i < tex_w * tex_h; i += WorkGroupSize)
	{
		int row = i / tex_w;
		int col = i % tex_w;
		imageStore(haar_wavelet_temp_image, ivec2(row, col), imageLoad(kernel, ivec2(row, col)));
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
				imageStore(haar_wavelet_temp_image, ivec2(i, j), (imageLoad(kernel, ivec2(2 * i, j)) + imageLoad(kernel, ivec2(2 * i + 1, j))) / s);
				imageStore(haar_wavelet_temp_image, ivec2(k + i, j), (imageLoad(kernel, ivec2(2 * i, j)) - imageLoad(kernel, ivec2(2 * i + 1, j))) / s);
			}
			for (i = 0; i < 2 * k; i++)
			{
				imageStore(kernel, ivec2(i, j), imageLoad(haar_wavelet_temp_image, ivec2(i, j)));
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
				imageStore(haar_wavelet_temp_image, ivec2(i, j), (imageLoad(kernel, ivec2(i, 2 * j)) + imageLoad(kernel, ivec2(i, 2 * j + 1))) / s);
				imageStore(haar_wavelet_temp_image, ivec2(i, k + j), (imageLoad(kernel, ivec2(i, 2 * j)) - imageLoad(kernel, ivec2(i, 2 * j + 1))) / s);
			}
			for (j = 0; j < 2 * k; j++)
			{
				imageStore(kernel, ivec2(i, j), imageLoad(haar_wavelet_temp_image, ivec2(i, j)));
			}
		}
	}
	barrier();

	return;
}

float fDiffuseProfile(float r, float A, float s)
{
	return s * ( ( exp(-s * r) + exp(-s * r / 3) ) / (8 * M_PI) );
}
