
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

#pragma once

#include "glm_common.h"


struct Transform
{
	glm::quat Rotation = glm::identity<glm::quat>();
	glm::vec3 Translation = glm::vec3(0.0f);
	float Scalation = 1.0;

	void Reset();
	void Move(glm::vec3 OffsetBy);
	void Rotate(glm::quat RotateBy);
	void Scale(float ScaleBy);

	Transform Inverse() const;

	glm::mat4 ToMatrix() const;

	glm::vec3 Apply(glm::vec3 Point) const;
	glm::vec3 ApplyInv(glm::vec3 Point) const;

	bool operator==(Transform& Other);
	bool operator==(Transform& Other) const;
};
