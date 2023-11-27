
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

#include "aabb.h"


bool AABB::Degenerate() const
{
	const bool AnyInf = glm::any(glm::isinf(Min)) || glm::any(glm::isinf(Max));
	const bool AnyNan = glm::any(glm::isnan(Min)) || glm::any(glm::isnan(Max));
	const bool NotWellFormed = glm::any(glm::lessThanEqual(Max, Min));
	return AnyInf || AnyNan || NotWellFormed;
}


glm::vec3 AABB::Extent() const
{
	if (Degenerate())
	{
		return glm::vec3(0.0, 0.0, 0.0);
	}
	else
	{
		return Max - Min;
	}
}


glm::vec3 AABB::Center() const
{
	if (Degenerate())
	{
		return glm::vec3(0.0, 0.0, 0.0);
	}
	else
	{
		return Extent() * glm::vec3(0.5) + Min;
	}
}


float AABB::Volume() const
{
	if (Degenerate())
	{
		return 0.0;
	}
	else
	{
		const glm::vec3 MyExtent = Extent();
		return MyExtent.x * MyExtent.y * MyExtent.z;
	}
}


AABB AABB::BoundingCube() const
{
	if (Degenerate())
	{
		glm::vec3 Zeros = glm::vec3(0.0);
		return { Zeros, Zeros };
	}
	else
	{
		glm::vec3 MyExtent = Extent();
		float Longest = max(max(MyExtent.x, MyExtent.y), MyExtent.z);
		glm::vec3 Padding = (glm::vec3(Longest) - MyExtent) * glm::vec3(0.5);
		return {
			Min - Padding,
			Max + Padding
		};
	}
}


AABB AABB::operator+(float Margin) const
{
	if (Degenerate())
	{
		glm::vec3 Zeros = glm::vec3(0.0);
		return { Zeros, Zeros };
	}
	else
	{
		return {
			Min - glm::vec3(Margin),
			Max + glm::vec3(Margin)
		};
	}
}
