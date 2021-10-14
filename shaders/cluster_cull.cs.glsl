prepend: shaders/tile_heap.glsl
--------------------------------------------------------------------------------

// Copyright 2021 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


layout(std140, binding = 0)
uniform ViewInfoBlock
{
	mat4 WorldToView;
	mat4 ViewToWorld;
	mat4 ViewToClip;
	mat4 ClipToView;
	vec4 CameraOrigin;
	vec4 ScreenSize;
	vec4 ModelMin;
	vec4 ModelMax;
	float CurrentTime;
};


layout(std140, binding = 1)
uniform InstanceInfoBlock
{
	uint InstanceOffset;
};


layout(std430, binding = 0) restrict writeonly buffer TileHeap
{
	TileHeapEntry Heap[];
};


layout(std140, binding = 1) buffer TileHeapInfo
{
	uint HeapSize;
	uint SegmentStart;
	uint StackPtr;
};


layout(std430, binding = 2) restrict readonly buffer InstanceHeap
{
	mat4 InstanceTransforms[];
};


layout(local_size_x = TILE_SIZE_X, local_size_y = TILE_SIZE_Y, local_size_z = 1) in;
void main()
{
	// Each lane in this dispatch corresponds to a screen space tile.
	// The work group size of this dispatch is not significant.
	vec2 TileSize = vec2(float(TILE_SIZE_X), float(TILE_SIZE_Y));
	vec2 ScreenMin = gl_GlobalInvocationID.xy * TileSize;
	if (all(lessThanEqual(ScreenMin, ScreenSize.xy)))
	{
		vec2 ScreenMax = min(ScreenMin + TileSize, ScreenSize.xy);
		vec4 TileClip = vec4(ScreenMin, ScreenMax) * ScreenSize.zwzw * 2.0 - 1.0;

		{
			const uint ClusterIndex = gl_GlobalInvocationID.z % ClusterCount;
			const uint InstanceID = (gl_GlobalInvocationID.z / ClusterCount) + InstanceOffset;
			AABB Bounds = ClusterData[ClusterIndex];
			mat4 LocalToWorld = InstanceTransforms[InstanceID * 2];
			if (ClipTest(ViewToClip * WorldToView * LocalToWorld, TileClip, Bounds))
			{
#if 0
				vec3 WorldMin = Bounds.Center - Bounds.Extent;
				vec3 WorldMax = Bounds.Center + Bounds.Extent;

				vec4 Clip = vec4((TileClip.xy + TileClip.zw) * 0.5, -1.0, 1.0);
				vec4 View = ClipToView * Clip;
				View /= View.w;
				vec4 World = ViewToWorld * View;
				World /= World.w;
				vec3 EyeRay = normalize(World.xyz - CameraOrigin.xyz);
				vec3 RayStart = EyeRay * BoxBrush(CameraOrigin.xyz - Bounds.Center, Bounds.Extent) + CameraOrigin.xyz;
				vec3 SearchMin = min(RayStart, WorldMin);
				vec3 SearchMax = max(RayStart, WorldMax);

				bool RayEscaped = false;
				{
					float Travel = 0;
					for (int i = 0; i < 10; ++i)
					{
						vec3 Position = EyeRay * Travel + RayStart;
						if (any(lessThan(Position, SearchMin)) || any(greaterThan(Position, SearchMax)))
						{
							RayEscaped = true;
							break;
						}
						else
						{
							float Dist = ClusterDist(Position);
							if (Dist <= 0.1)
							{
								RayEscaped = false;
								break;
							}
							else
							{
								Travel += Dist;
							}
						}
					}
				}
				if (!RayEscaped)
#endif
				{
					uint Ptr = atomicAdd(StackPtr, 1);
					TileHeapEntry Tile;
					Tile.TileID = ((gl_GlobalInvocationID.y & 0xFFFF) << 16) | (gl_GlobalInvocationID.x & 0xFFFF);
					Tile.ClusterID = ClusterIndex;
					Tile.InstanceID = InstanceID;
					Heap[Ptr] = Tile;
				}
			}
		}
	}
}
