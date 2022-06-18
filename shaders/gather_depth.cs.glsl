prepend: shaders/defines.h
--------------------------------------------------------------------------------

// Copyright 2022 Aeva Palecek
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


layout(std140, binding = 2)
uniform DepthLevelBlock
{
	ivec2 Size;
	int Level; // Mip levels to produce
};


layout(binding = 3) uniform sampler2D DepthBuffer;
layout(binding = 4, r32f) uniform readonly image2D LastSlice;
layout(binding = 5, r32f) uniform writeonly image2D NextSlice;


layout(local_size_x = TILE_SIZE_X, local_size_y = TILE_SIZE_Y, local_size_z = 1) in;
void main()
{
	// Lanes correspond to pixels in the output buffer.
	ivec2 Position = ivec2(gl_GlobalInvocationID.xy);
	ivec2 Texel = Position;

	float Depth = 0.0;
	if (Level == 0)
	{
		Depth = texelFetch(DepthBuffer, Texel, 0).r;
	}
	else
	{
		Texel *= 2;
		Depth = 1.0 / 0.0;
		ivec2 Span = ivec2(2) + ivec2(Size % 2);
		for (int y = 0; y < Span.y; ++y)
		{
			for (int x = 0; x < Span.x; ++x)
			{
				Depth = min(Depth, imageLoad(LastSlice, Texel + ivec2(x, y)).r);
			}
		}
	}
	if (all(lessThan(Position, Size)))
	{
		imageStore(NextSlice, Position, vec4(Depth));
	}
}
