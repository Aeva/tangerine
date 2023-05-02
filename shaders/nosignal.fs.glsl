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


// This shader is a modest replacement for noise.fs.glsl, because GLES does
// not support all the math ops used by the original shader's PRNG.

varying vec2 UV;

void main()
{
	//gl_FragColor = vec4(UV, 0.5, 1);
	vec3 Low = vec3(0.0, 0.0, 0.05);
	vec3 High = vec3(0.12, 0.1, 0.1);
	float AlphaH = abs(UV.x - .5) * 2.0;

	float AlphaV = mix(UV.y * UV.y, UV.y, AlphaH * AlphaH);

	gl_FragColor = vec4(mix(High, Low, AlphaV), 1.0);
}
