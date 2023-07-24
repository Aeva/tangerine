
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

#pragma once
#include "sdf_model.h"

enum class ExportFormat
{
	STL,
	PLY,
	VOX,
	Unknown,
};

struct ExportProgress
{
	int Stage;
	float Generation;
	float Refinement;
	float Secondary;
	float Write;
};

void MeshExport(std::string Path, bool UseBaseColor, float Scale = 1.0);
void MeshExport(SDFNodeShared Evaluator, std::string Path, glm::vec3 ModelMin, glm::vec3 ModelMax, glm::vec3 Step, int RefineIterations, ExportFormat Format, bool ExportPointCloud, float Scale = 1.0);
void CancelExport(bool Halt);
ExportProgress GetExportProgress();
