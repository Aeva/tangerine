
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

#include "transform.h"

using namespace glm;


void Transform::Reset()
{
	Rotation = identity<glm::quat>();
	Translation = vec3(0.0f);
	Scalation = 1.0f;
}


void Transform::Move(vec3 OffsetBy)
{
	Translation += OffsetBy;
}


void Transform::Rotate(glm::quat RotateBy)
{
	Translation = rotate(RotateBy, Translation);
	Rotation = RotateBy * Rotation;
}


void Transform::Scale(float ScaleBy)
{
	Scalation *= ScaleBy;
}


Transform Transform::Inverse() const
{
	Transform MyInverse =\
	{
		inverse(Rotation),
		-Translation,
		1.0 / Scalation
	};

	return MyInverse;
}


mat4 Transform::ToMatrix() const
{
	mat4 RotationMatrix = toMat4(Rotation);
	mat4 TranslationMatrix = translate(identity<mat4>(), Translation);
	mat4 ScalationMatrix = scale(identity<mat4>(), vec3(Scalation));
	return TranslationMatrix * RotationMatrix * ScalationMatrix;
}


vec3 Transform::Apply(vec3 Point) const
{
	return rotate(Rotation, Point * Scalation) + Translation;
}


vec3 Transform::ApplyInv(vec3 Point) const
{
	return (rotate(inverse(Rotation), Point - Translation)) / Scalation;
}
