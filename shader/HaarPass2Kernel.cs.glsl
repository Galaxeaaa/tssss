#version 450

#define M_PI 3.1415926535897932384626433832795

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h, tex_w, tex_h;
uniform ivec2 index_kernel_iv;

layout(rgba32f, binding = 0) uniform readonly image2D world_pos_map;
layout(rgba32f, binding = 1) uniform image2D kernel;
layout(rgba32f, binding = 2) uniform image2D haar_wavelet_temp_image;
layout(std430, binding = 1) buffer KernelCoef {
	vec4 data[];
} kernel_coef;
layout(std430, binding = 2) buffer U {
	float u[];
};
layout(std430, binding = 3) buffer V {
	float v[];
};

void haar2D(uint height, uint width);
void haar2D_Image(uint m, uint n, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp);
float fDiffuseProfile(float r, float A = 1.0, float s = 23.651121);

void main() {
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
	uint size_coef_array = coef_w * coef_h;
	uint i = index_kernel_iv.x;
	uint j = index_kernel_iv.y;
	uint index_kernel = i * tex_w + j;
	uint row, col;
	// Compute kernel at (i, j).
	// - Array
	// if (GlobalInvocationIndex < tex_h)
	// {
	// 	row = GlobalInvocationIndex;
	// 	for (col = 0; col < tex_w; col++)
	// 	{
	// 		vec3 pos_i_j = vec3(imageLoad(world_pos_map, ivec2(i, j)));
	// 		vec3 pos_row_col = vec3(imageLoad(world_pos_map, ivec2(row, col)));
	// 		float l = length(pos_i_j - pos_row_col);
	// 		u[row * tex_w + col] = fDiffuseProfile(l);
	// 		// imageStore(kernel, ivec2(row, col), vec4(fDiffuseProfile(l), 0, 0, 0));
	// 	}
	// }
	// barrier();
	// - Image
	row = GlobalInvocationIndex;
	for (col = 0; col < tex_w; col++)
	{
		vec3 pos_i_j = vec3(imageLoad(world_pos_map, ivec2(i, j)));
		vec3 pos_row_col = vec3(imageLoad(world_pos_map, ivec2(row, col)));
		float l = length(pos_i_j - pos_row_col);
		if (l < 0.1)
			imageStore(kernel, ivec2(row, col), vec4(fDiffuseProfile(l), 0, 0, 0));
	}
	barrier();
	// Transform kernel.
	haar2D_Image(tex_h, tex_w, kernel, haar_wavelet_temp_image);
	// haar2D(tex_h, tex_w);
	barrier();
	// Store some coefficients.
	uint index_coef = GlobalInvocationIndex;
	kernel_coef.data[index_coef] = vec4(imageLoad(kernel, ivec2(index_coef / coef_w, index_coef % coef_w)));
	barrier();
}

void haar2D(uint m, uint n)
{
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

	uint i;
	uint j;
	uint k;
	float s = sqrt(2.0);

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
			v[i * tex_w + j]		= (u[2 * i * tex_w + j] + u[2 * i + 1 * tex_w + j]) / s;
			v[k + i * tex_w + j] = (u[2 * i * tex_w + j] - u[2 * i + 1 * tex_w + j]) / s;
			// imageStore(tmp, ivec2(i, j), (imageLoad(img, ivec2(2 * i, j)) + imageLoad(img, ivec2(2 * i + 1, j))) / s);
			// imageStore(tmp, ivec2(k + i, j), (imageLoad(img, ivec2(2 * i, j)) - imageLoad(img, ivec2(2 * i + 1, j))) / s);
		}
		for (i = 0; i < 2 * k; i++)
		{
			u[i * tex_w + j] = v[i * tex_w + j];
			// imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
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
			v[i * tex_w + j] 	= (u[i * tex_w + 2 * j] + u[i * tex_w + 2  * j + 1]) / s;
			v[i * tex_w + k + j] = (u[i * tex_w + 2 * j] - u[i * tex_w + 2  * j + 1]) / s;
			// imageStore(tmp, ivec2(i, j), (imageLoad(img, ivec2(i, 2 * j)) + imageLoad(img, ivec2(i, 2 * j + 1))) / s);
			// imageStore(tmp, ivec2(i, k + j), (imageLoad(img, ivec2(i, 2 * j)) - imageLoad(img, ivec2(i, 2 * j + 1))) / s);
		}
		for (j = 0; j < 2 * k; j++)
		{
			u[i * tex_w + j] = v[i * tex_w + j];
			// imageStore(img, ivec2(i, j), imageLoad(tmp, ivec2(i, j)));
		}
	}
	barrier();

	return;
}

void haar2D_Image(uint m, uint n, layout(rgba32f) image2D img, layout(rgba32f) image2D tmp)
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

float fDiffuseProfile(float r, float A, float s)
{
	return A * s * ( ( exp(-s * r) + exp(-s * r / 3) ) / (8 * M_PI) );
}
