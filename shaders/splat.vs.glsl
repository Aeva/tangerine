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


#if GL_ES
// GLES doesn't allow bitwise operators or gl_VertexID, so we need to provide the coordinates on the CPU.

attribute vec2 Clip;
varying vec2 UV;

void main()
{
	UV = Clip * vec2(0.5) + vec2(0.5);
	gl_Position = vec4(Clip, 0.0, 1.0);
}


#else
out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[];
};

void main()
{
	gl_Position = vec4(-1.0 + float((gl_VertexID & 1) << 2), -1.0 + float((gl_VertexID & 2) << 1), 0, 1);
}
#endif