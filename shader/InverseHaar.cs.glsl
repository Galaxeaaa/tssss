#version 460

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h, tex_w, tex_h;
shared int WorkGroupSize;
shared int size_coef_array;

layout(rgba32f, binding = 0) uniform image2D img;
layout(rgba32f, binding = 1) uniform image2D tmp;
layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

void haar2dInverse();

void main() {
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	if (GlobalInvocationIndex == 0)
	{
		WorkGroupSize = int(gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z);
		size_coef_array = coef_w * coef_h;
	}
	barrier();
	// Black radiance map
	for (int index_texel = GlobalInvocationIndex; index_texel < tex_h * tex_w; index_texel += WorkGroupSize)
	{
		int row = index_texel / tex_w;
		int col = index_texel % tex_w;
		imageStore(img, ivec2(row, col), vec4(0, 0, 0, 0));
	}
	barrier();
	// Copy coefs into image
	for (int index_coef = GlobalInvocationIndex; index_coef < coef_h * coef_w; index_coef += WorkGroupSize)
	{
		int row = index_coef / coef_w;
		int col = index_coef % coef_w;
		imageStore(img, ivec2(row, col), radiance_coef.data[index_coef]);
	}
	barrier();
	// Inversely transform radiance map.
	haar2dInverse();
	barrier();
}

void haar2dInverse()
{
	int GlobalInvocationIndex = int(gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x);
	int i, j, k;
	float s = sqrt(2.0);
	// Copy img into tmp
	for (i = GlobalInvocationIndex; i < tex_w * tex_h; i += WorkGroupSize)
	{
		int row = i / tex_w;
		int col = i % tex_w;
		imageStore(tmp, ivec2(row, col), imageLoad(img, ivec2(row, col)));
	}
	// Inverse transform of all rows.
	k = 1;
	for (i = GlobalInvocationIndex; i < tex_h; i += WorkGroupSize)
	{
		while (k * 2 <= tex_w)
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
	}
	// Inverse transform of all columns.
	k = 1;
	for (j = GlobalInvocationIndex; j < tex_w; j += WorkGroupSize)
	{
		while (k * 2 <= tex_h)
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
	}

	return;
}