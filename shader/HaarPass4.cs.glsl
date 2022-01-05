#version 450

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

uniform int coef_w, coef_h;
uniform int tex_w, tex_h;

layout(rgba32f, binding = 0) uniform image2D radiance_map_after_sss;

layout(std430, binding = 0) buffer RadianceCoef
{
	vec4 data[];
} radiance_coef;

layout(std430, binding = 1) buffer KernelCoef
{
	vec4 data[];
} kernel_coef;

void main() {
	uint size_coef_array = coef_w * coef_h;
	uint GlobalInvocationIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
	uint row = GlobalInvocationIndex;
	for (uint col = 0; col < tex_w; col++)
	{
		vec3 sum = vec3(0, 0, 0);
		// for (int i = 0; i < coef_w * coef_h; i++)
		// {
			// sum += vec3(radiance_coef.data[i]) * kernel_coef.data[(row * tex_w + col) * size_coef_array + i].r;
		// }
		sum += vec3(radiance_coef.data[0]) * kernel_coef.data[(row * tex_w + col) * size_coef_array + 0].r;
		imageStore(radiance_map_after_sss, ivec2(row, col), vec4(sum, 1));
	}
	barrier();
}
