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
#if 1
	if (any(lessThan(Barycenter, vec3(0.05))))
	{
		OutColor = vec4(1.0);
	}
	else
#endif
#if 0
	{
		OutColor = vec4(0.0, 0.0, 0.0, 1.0);
	}
#elif 1
	{
		OutColor = vec4(fract(VertexColor), 1.0);
	}
#else
	{
		OutColor = vec4(VertexColor, 1.0);
	}
#endif
}
