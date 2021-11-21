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


layout(binding = 1) uniform sampler2D InputBuffer;
layout(location = 0) out vec4 OutColor;


void main()
{
	ivec2 Texel = ivec2(gl_FragCoord.xy);
	vec3 Color = texelFetch(InputBuffer, Texel, 0).rgb;
	OutColor = vec4(Color, 1.0);
}
