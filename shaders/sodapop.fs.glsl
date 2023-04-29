--------------------------------------------------------------------------------

// Copyright 2023 Aeva Palecek
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


in vec3 VertexColor;
in vec3 Barycenter;

layout(location = 0) out vec4 OutColor;


void main()
{
	OutColor = vec4(VertexColor, 1.0);
#if 0
	if (any(lessThan(Barycenter, vec3(0.05))))
	{
		OutColor.xyz = vec3(1.0) - step(vec3(0.5), VertexColor);
	}
#endif
}
