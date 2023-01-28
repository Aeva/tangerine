
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

#include "sodapop.h"
#include "gl_boilerplate.h"
#include "sdf_evaluator.h"
#include <vector>


static ShaderProgram PaintShader;


struct Bubble
{
	SDFNode* SDF;
	glm::mat4 LocalToWorld;

	Buffer ModelInfo;
	Buffer PositionBuffer;
	Buffer ColorBuffer;

	Bubble(SDFNode* InSDF)
		: SDF(InSDF)
		, LocalToWorld(glm::identity<glm::mat4>())
		, ModelInfo("ModelInfo")
		, PositionBuffer("PositionBuffer")
	{
		AABB Bounds = SDF->Bounds();

		std::vector<glm::vec4> Vertices;
		std::vector<glm::vec4> Colors;

		// This is meant to be a quick and dirty meshing algorithm where a subdivided bounding
		// box is then gradient marched to shrink wrap it to the levelset of whichever SDF.
		// However, instead it just displays a colorful triangle right now.

		Vertices.emplace_back(2.5, 0, -2.5, 1);
		Vertices.emplace_back(0, 0, 2.5, 1);
		Vertices.emplace_back(-2.5, 0, -2.5, 1);

		Colors.emplace_back(0, 1, 1, 1);
		Colors.emplace_back(1, 0, 1, 1);
		Colors.emplace_back(1, 1, 0, 1);

		PositionBuffer.Upload(Vertices.data(), sizeof(glm::vec4) * Vertices.size());
		ColorBuffer.Upload(Colors.data(), sizeof(glm::vec4) * Colors.size());
		ModelInfo.Upload((void*)&LocalToWorld, sizeof(LocalToWorld));
	}
};


Bubble* Fnord;


StatusCode SetupSodapop()
{
	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("sodapop/yolo.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("sodapop/yolo.fs.glsl", true)} },
		"Outliner Shader"));

	SDFNode* Sphere = SDF::Sphere(2);
	Fnord = new Bubble(Sphere);

	delete Sphere;

	return StatusCode::PASS;
}


static GLuint FinalPass = 0;


static void AllocateRenderTargets(int ScreenWidth, int ScreenHeight)
{
	static bool Initialized = false;
	if (Initialized)
	{
	}
}


struct ViewInfoUpload
{
	glm::mat4 WorldToView = glm::identity<glm::mat4>();
	glm::mat4 ViewToClip = glm::identity<glm::mat4>();
};


static Buffer ViewInfo("ViewInfo Buffer");


void RenderFrame(int ScreenWidth, int ScreenHeight)
{
	static int Width = 0;
	static int Height = 0;

	if (Width != ScreenWidth || Height != ScreenHeight)
	{
		Width = ScreenWidth;
		Height = ScreenHeight;
		glViewport(0, 0, Width, Height);
		AllocateRenderTargets(Width, Height);
	}

	const glm::vec3 ViewOrigin(0.0, -10.0, 0.0);
	const glm::vec3 FocalPoint(0.0, 0.0, 0.0);
	const glm::vec3 WorldUp(0.0, 0.0, 1.0);
	const glm::mat4 WorldToView = glm::lookAt(ViewOrigin, FocalPoint, WorldUp);

	const float CameraFov = 45.0;
	const float CameraNear = 0.1;
	const float AspectRatio = float(Width) / float(Height);
	const glm::mat4 ViewToClip = glm::infinitePerspective(glm::radians(CameraFov), AspectRatio, CameraNear);

	{
		ViewInfoUpload BufferData = {
			WorldToView,
			ViewToClip
		};
		ViewInfo.Upload((void*)&BufferData, sizeof(BufferData));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);
	}

	Fnord->ModelInfo.Bind(GL_UNIFORM_BUFFER, 1);
	Fnord->PositionBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
	Fnord->ColorBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 3);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);

	PaintShader.Activate();

	glDrawArrays(GL_TRIANGLES, 0, 3);
}
