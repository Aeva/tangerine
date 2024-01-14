
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


struct AABB
{
	glm::vec3 Min;
	glm::vec3 Max;

	bool Degenerate() const;

	// Returns true if the other AABB touches this one at all.
	bool Overlaps(AABB& Other) const;

	// Returns true if the sphere touches the AABB at all.
	bool Overlaps(glm::vec3 SphereCenter, float SphereRadius) const;

	// Returns true if the point is fully within the AABB.
	bool Contains(glm::vec3 Point) const;

	// Returns true if the sphere is fully within the AABB.
	bool Contains(glm::vec3 SphereCenter, float SphereRadius) const;

	glm::vec3 Extent() const;

	glm::vec3 Center() const;

	float Volume() const;

	AABB BoundingCube() const;

	AABB operator+(float Margin) const;

	AABB operator+(glm::vec3 Margin) const;
};
