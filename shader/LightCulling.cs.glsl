#version 450

#define MAX_NLIGHTS_PER_TILE 2048
#define TILE_SIZE 16

struct Light {
	vec4 Position;
	vec4 Color;
	float Radius;
};

struct VisibleIndex {
	int index;
};

struct DebugStruct {
	int nlight;
	float mindepth;
	float maxdepth;
};

layout(std430, binding = 10) buffer LightBuffer {
	Light data[];
} lightBuffer;

layout(std430, binding = 1) writeonly buffer VisibleLightIndicesBuffer {
	VisibleIndex data[];
} visibleLightIndicesBuffer;

layout(std430, binding = 2) buffer DebugBuffer {
	DebugStruct data[];
} debugBuffer;

// layout(r32f, binding = 0) uniform image2D dbgMap;

// Uniforms
uniform sampler2D depthMap;
uniform mat4 view;
uniform mat4 projection;
uniform ivec2 screenSize;
uniform int N_LIGHTS;

// Shared values between all the threads in the group
shared uint minDepthInt;
shared uint maxDepthInt;
shared vec4 frustumPlanes[6];
shared uint visibleLightCount;
shared int visibleLightIndices[MAX_NLIGHTS_PER_TILE];
shared mat4 viewProjection;

layout(local_size_x = TILE_SIZE, local_size_y = TILE_SIZE, local_size_z = 1) in;
void main() {
	ivec2 location = ivec2(gl_GlobalInvocationID.xy);
	ivec2 itemID = ivec2(gl_LocalInvocationID.xy);
	ivec2 tileID = ivec2(gl_WorkGroupID.xy);
	ivec2 tileNumber = ivec2(gl_NumWorkGroups.xy);
	uint index = tileID.y * tileNumber.x + tileID.x;

	if (gl_LocalInvocationIndex == 0) {
		minDepthInt = 0xFFFFFFFF;
		maxDepthInt = 0;
		visibleLightCount = 0;
		viewProjection = transpose(projection * view);
	}

	barrier();

	vec2 text = vec2(location) / vec2(screenSize);
	float depth = texture(depthMap, text).r;
	// imageStore(dbgMap, location, vec4(depth, 0, 0, 1));

	uint depthInt = floatBitsToUint(depth);
	atomicMin(minDepthInt, depthInt);
	atomicMax(maxDepthInt, depthInt);

	barrier();

	if (gl_LocalInvocationIndex == 0) {
		float maxDepth, minDepth;
		minDepth = uintBitsToFloat(minDepthInt);
		maxDepth = uintBitsToFloat(maxDepthInt);
		debugBuffer.data[index].mindepth = minDepth;
		debugBuffer.data[index].maxdepth = maxDepth;
		minDepth = minDepth * 2 - 1;
		maxDepth = maxDepth * 2 - 1;

		vec2 negativeStep = (2.0 * vec2(tileID)) / vec2(tileNumber);
		vec2 positiveStep = (2.0 * vec2(tileID + ivec2(1, 1))) / vec2(tileNumber);

		frustumPlanes[0] = vec4(1.0, 0.0, 0.0, 1.0 - negativeStep.x); // Left
		frustumPlanes[1] = vec4(-1.0, 0.0, 0.0, -1.0 + positiveStep.x); // Right
		frustumPlanes[2] = vec4(0.0, 1.0, 0.0, 1.0 - negativeStep.y); // Bottom
		frustumPlanes[3] = vec4(0.0, -1.0, 0.0, -1.0 + positiveStep.y); // Top
		// frustumPlanes[4] = vec4(0.0, 0.0, -1.0, -minDepth); // Near
		// frustumPlanes[5] = vec4(0.0, 0.0, 1.0, maxDepth); // Far
		frustumPlanes[4] = vec4(0.0, 0.0, 1.0, -minDepth); // Near
		frustumPlanes[5] = vec4(0.0, 0.0, -1.0, maxDepth); // Far

		for (uint i = 0; i < 6; i++) {
			frustumPlanes[i] = viewProjection * frustumPlanes[i];
			frustumPlanes[i] /= length(frustumPlanes[i].xyz);
		}
	}

	barrier();

	uint threadCount = TILE_SIZE * TILE_SIZE;
	for (uint i = gl_LocalInvocationIndex; i < N_LIGHTS; i += threadCount) {
		vec4 position = lightBuffer.data[i].Position;
		float radius = lightBuffer.data[i].Radius;
		// float radius = 2.0;
		// vec4 position = vec4(lights[i].Position, 1);
		// float radius = lights[i].Radius;

		float d = 0.0;
		for (uint j = 0; j < 6; j++) {
			d = dot(position, frustumPlanes[j]) + radius;
			if (d <= 0.0) {
				break;
			}
		}

		if (d > 0.0) {
			uint offset = atomicAdd(visibleLightCount, 1);
			visibleLightIndices[offset] = int(i);
		}
	}

	barrier();

	if (gl_LocalInvocationIndex == 0) {
		uint offset = index * MAX_NLIGHTS_PER_TILE;
		for (uint i = 0; i < visibleLightCount; i++) {
			visibleLightIndicesBuffer.data[offset + i].index = visibleLightIndices[i];
		}

		if (visibleLightCount != MAX_NLIGHTS_PER_TILE) {
			visibleLightIndicesBuffer.data[offset + visibleLightCount].index = -1;
		}
		debugBuffer.data[index].nlight = int(visibleLightCount);
	}
}
