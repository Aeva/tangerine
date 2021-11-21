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


layout(binding = 1) uniform sampler2D DepthBuffer;
layout(binding = 2) uniform usampler2D MaterialBuffer;


void main()
{
	ivec2 Texel = ivec2(gl_FragCoord.xy);
	float DepthMask = texelFetch(DepthBuffer, Texel, 0).r;
	if (DepthMask == 0.0)
	{
		gl_FragDepth = 1.0;
	}
	else
	{
		uint Stencil = 1 + texelFetch(MaterialBuffer, Texel, 0).r;
		gl_FragDepth = 1.0 - 1.0/float(Stencil);
	}
}
