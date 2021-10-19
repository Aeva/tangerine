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


layout(std430, binding = 1) restrict writeonly buffer SubtreeClipRects
{
	ClipRect ClipRects[];
};


layout(std430, binding = 2) restrict readonly buffer InstanceHeap
{
	mat4 InstanceTransforms[];
};


layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
	// The entire dispatch corresponds to a single unique subtree.  Each lane
	// corresponds to an instance of one of the subtree's AABBs.
	const uint ClusterIndex = gl_GlobalInvocationID.x % ClusterCount;
	if (ClusterIndex < ClusterCount)
	{
		AABB Bounds = ClusterData[ClusterIndex];
		const uint InstanceID = (gl_GlobalInvocationID.x / ClusterCount) + InstanceOffset;
		mat4 LocalToWorld = InstanceTransforms[InstanceID * 2];

		ClipRect Rect;
		BoundingRect(ViewToClip, WorldToView * LocalToWorld, Bounds, Rect.ClipMin, Rect.ClipMax);
		ClipRects[gl_GlobalInvocationID.x] = Rect;
	}
}
