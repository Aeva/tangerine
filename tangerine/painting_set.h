
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

#include "sdf_model.h"
#include <functional>


class PaintingSet;
using PaintingSetShared = std::shared_ptr<PaintingSet>;
using PaintingSetWeakRef = std::weak_ptr<PaintingSet>;


struct ViewInfoUpload
{
	glm::mat4 WorldToView = glm::identity<glm::mat4>();
	glm::mat4 ViewToWorld = glm::identity<glm::mat4>();
	glm::mat4 ViewToClip = glm::identity<glm::mat4>();
	glm::mat4 ClipToView = glm::identity<glm::mat4>();
	glm::vec4 CameraOrigin = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ScreenSize = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ModelMin = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ModelMax = glm::vec4(0.0, 0.0, 0.0, 0.0);
	float CurrentTime = -1.0;
	bool Perspective = true;
	float Padding[2] = { 0 };
};


class PaintingSet
{
	uint64_t UniqueToken;

	std::vector<SDFModelWeakRef> Models;

	PaintingSet();

	void RenderFrameGL4(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView);

	void RenderFrameES2(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView);

public:

	static PaintingSetShared Create();

	~PaintingSet();

	bool CanRender();

	void RenderFrame(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView);

	void RegisterModel(SDFModelShared& NewModel);

	void Apply(std::function<void(SDFModelShared)>& Thunk);

	SDFModelShared Select(std::function<bool(SDFModelShared)>& Thunk);

	void Filter(std::vector<SDFModelShared>& Results, std::function<bool(SDFModelShared)>& Thunk);

	static void GlobalApply(std::function<void(SDFModelShared)>& Thunk);

	static SDFModelShared GlobalSelect(std::function<bool(SDFModelShared)>& Thunk);

	static void GlobalFilter(std::vector<SDFModelShared>& Results, std::function<bool(SDFModelShared)>& Thunk);

	static void GatherModelStats(int& IncompleteCount, int& RenderableCount);
};
