
// Copyright 2022 Aeva Palecek
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

#include <functional>
#include <vector>
#include <atomic>
#include <mutex>

#include "threadpool.h"
#include "extern.h"
#include "magica.h"
#include "VoxWriter.h"


void VoxExport(SDFNode* Evaluator, std::string& Path, float GridSize, int ColorIndex)
{
	AABB Bounds = Evaluator->Bounds();
	glm::ivec3 Size = glm::ivec3(glm::ceil(Bounds.Max - Bounds.Min) * GridSize);

	float Radius;
	{
		glm::vec3 Alpha = glm::vec3(.5, .5, .5) / glm::vec3(Size);
		Radius = glm::distance(Bounds.Min, glm::mix(Bounds.Min, Bounds.Max, Alpha));
	}

	const int Slice = Size.x * Size.y;
	const int TotalCells = Size.x * Size.y * Size.z;
	std::atomic_int Progress;

	vox::VoxWriter Writer(Size.x, Size.y, Size.z);

	std::mutex MagicCS;

	Pool([&]()
	{
		while(true)
		{
			const int i = Progress.fetch_add(1);
			if (i >= TotalCells)
			{
				break;
			}
			int z = i / Slice;
			int y = (i % Slice) / Size.x;
			int x = i % Size.x;

			glm::vec3 Alpha = glm::vec3(x + .5, y + .5, z + .5) / glm::vec3(Size);
			glm::vec3 Point = glm::mix(Bounds.Min, Bounds.Max, Alpha);
			float Dist = Evaluator->Eval(Point);
			if (std::abs(Dist) <= Radius)
			{
				MagicCS.lock();
				Writer.AddVoxel(x, y, z, abs(ColorIndex) % 255 + 1);
				MagicCS.unlock();
			}
		}
	});

	Writer.SaveToFile(Path);
}


// This is for compatibility with the old "miniscule" interface, assuming it still works.
extern "C" TANGERINE_API void ExportMagicaVoxel(SDFNode * Evaluator, float GridSize, int ColorIndex, const char* Path)
{
	std::string PathStr = std::string(Path);
	VoxExport(Evaluator, PathStr, GridSize, ColorIndex);
}
