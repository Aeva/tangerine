
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

#include "painting_set.h"
#include "tangerine.h"
#include "profiling.h"
#include "gl_boilerplate.h"
#include "gl_init.h"
#include <map>

extern ShaderProgram BgShader;
extern ShaderProgram ResolveOutputShader;
extern ShaderProgram SodapopShader;

extern Buffer ViewInfo;

extern GLuint ColorPass;
extern GLuint ForwardPass;
extern GLuint FinalPass;

extern GLuint ColorBuffer;

extern TimingQuery DepthTimeQuery;
extern TimingQuery GridBgTimeQuery;

extern glm::vec3 BackgroundColor;

extern double TotalDrawTimeMS;


static uint64_t NextPaintingSetToken = 1;
static std::map<uint64_t, PaintingSetWeakRef> AllPaintingSets;


PaintingSet::PaintingSet()
	: UniqueToken(++NextPaintingSetToken)
{
	Assert(UniqueToken > 0);
}


PaintingSetShared PaintingSet::Create()
{
	PaintingSetShared NewPaintingSet(new PaintingSet());
	auto Result = AllPaintingSets.insert_or_assign(NewPaintingSet->UniqueToken, NewPaintingSet);
	Assert(Result.second);
	return NewPaintingSet;
}


PaintingSet::~PaintingSet()
{
	AllPaintingSets.erase(UniqueToken);
}


void PaintingSet::Apply(std::function<void(SDFModelShared)>& Thunk)
{
	for (SDFModelWeakRef& WeakRef : Models)
	{
		SDFModelShared Model = WeakRef.lock();
		if (Model)
		{
			Thunk(Model);
		}
	}
}


SDFModelShared PaintingSet::Select(std::function<bool(SDFModelShared)>& Thunk)
{
	for (SDFModelWeakRef& WeakRef : Models)
	{
		SDFModelShared Model = WeakRef.lock();
		if (Model && Thunk(Model))
		{
			return Model;
		}
	}
	return nullptr;
}


void PaintingSet::Filter(std::vector<SDFModelShared>& Results, std::function<bool(SDFModelShared)>& Thunk)
{
	for (SDFModelWeakRef& WeakRef : Models)
	{
		SDFModelShared Model = WeakRef.lock();
		if (Model && Thunk(Model))
		{
			Results.push_back(Model);
		}
	}
}


void PaintingSet::GlobalApply(std::function<void(SDFModelShared)>& Thunk)
{
	for (auto Registration : AllPaintingSets)
	{
		PaintingSetShared Zone = Registration.second.lock();
		if (Zone)
		{
			Zone->Apply(Thunk);
		}
	}
}


SDFModelShared PaintingSet::GlobalSelect(std::function<bool(SDFModelShared)>& Thunk)
{
	SDFModelShared Found = nullptr;

	for (auto Registration : AllPaintingSets)
	{
		PaintingSetShared Zone = Registration.second.lock();
		if (Zone)
		{
			Found = Zone->Select(Thunk);
			if (Found)
			{
				break;
			}
		}
	}

	return Found;
}


void PaintingSet::GlobalFilter(std::vector<SDFModelShared>& Results, std::function<bool(SDFModelShared)>& Thunk)
{
	Results.clear();

	for (auto Registration : AllPaintingSets)
	{
		PaintingSetShared Zone = Registration.second.lock();
		if (Zone)
		{
			Zone->Filter(Results, Thunk);
		}
	}
}


void PaintingSet::GatherModelStats(int& IncompleteCount, int& RenderableCount)
{
	IncompleteCount = 0;
	RenderableCount = 0;
	for (auto Registration : AllPaintingSets)
	{
		PaintingSetShared Zone = Registration.second.lock();
		if (Zone)
		{
			for (SDFModelWeakRef WeakRef : Zone->Models)
			{
				SDFModelShared Model = WeakRef.lock();
				if (Model && Model->Painter)
				{
					if (Model->Painter->MeshAvailable)
					{
						++RenderableCount;
					}
					else
					{
						++IncompleteCount;
					}
				}
			}
		}
	}
}


void PaintingSet::RenderFrameGL4(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView)
{
	glDisable(GL_FRAMEBUFFER_SRGB);

	ViewInfo.Upload((void*)&UploadedView, sizeof(UploadedView));
	ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);

	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Background");
		glBindFramebuffer(GL_FRAMEBUFFER, ColorPass);
		GridBgTimeQuery.Start();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_EQUAL);
		switch (GetBackgroundMode())
		{
		case 1:
			BgShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			break;
		case 0:
		default:
			glClearColor(BackgroundColor.r, BackgroundColor.g, BackgroundColor.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		GridBgTimeQuery.Stop();
		glPopDebugGroup();
	}
	{
		ProfilingTimePoint SodapopStartTime = ProfilingClock::now();

		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Sodapop");
		glBindFramebuffer(GL_FRAMEBUFFER, ForwardPass);
		DepthTimeQuery.Start();
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GREATER);
#ifdef ENABLE_RMLUI
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#else
		glClear(GL_DEPTH_BUFFER_BIT);
#endif

		SodapopShader.Activate();

		std::function<void(SDFModelShared)> DrawThunk = [&](SDFModelShared Model)
		{
			if (Model->Painter && Model->Painter->MeshAvailable)
			{
				Model->DrawGL4(UploadedView.CameraOrigin.xyz());
			}
		};
		Apply(DrawThunk);

		DepthTimeQuery.Stop();
		glPopDebugGroup();

		std::chrono::duration<double, std::milli> DrawDelta = ProfilingClock::now() - SodapopStartTime;
		TotalDrawTimeMS = DrawDelta.count();
	}
	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Resolve Output");
		glDisable(GL_DEPTH_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
		glBindTextureUnit(1, ColorBuffer);
		ResolveOutputShader.Activate();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glPopDebugGroup();
	}
}


void PaintingSet::RenderFrameES2(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView)
{
	glDisable(GL_FRAMEBUFFER_SRGB);
	glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);

	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Background");
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_EQUAL);
		switch (GetBackgroundMode())
		{
		case 1:
			// TODO port the bg shader to ES2
			Assert(false); // GetBackgroundMode() is meant to enforce this being unreachable.
#if 0
			BgShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
#endif
			break;
		case 0:
		default:
			glClearColor(BackgroundColor.r, BackgroundColor.g, BackgroundColor.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glPopDebugGroup();
	}
	{
		ProfilingTimePoint SodapopStartTime = ProfilingClock::now();

		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Sodapop");
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GREATER);
#ifdef ENABLE_RMLUI
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#else
		glClear(GL_DEPTH_BUFFER_BIT);
#endif

		SodapopShader.Activate();

		auto UploadMatrix = [&](const char* Name, const glm::mat4* Value)
		{
			const GLint Location = glGetUniformLocation(SodapopShader.ProgramID, Name);
			glUniformMatrix4fv(Location, 1, false, (const GLfloat*)Value);
		};

		UploadMatrix("WorldToView", &UploadedView.WorldToView);
		UploadMatrix("ViewToClip", &UploadedView.ViewToClip);

		const GLint LocalToWorldBinding = glGetUniformLocation(SodapopShader.ProgramID, "LocalToWorld");

		const GLint PositionBinding = glGetAttribLocation(SodapopShader.ProgramID, "LocalPosition");
		glEnableVertexAttribArray(PositionBinding);

		const GLint ColorBinding = glGetAttribLocation(SodapopShader.ProgramID, "VertexColor");
		glEnableVertexAttribArray(ColorBinding);

		std::function<void(SDFModelShared)> DrawThunk = [&](SDFModelShared Model)
		{
			if (Model->Painter && Model->Painter->MeshAvailable)
			{
				Model->DrawES2(UploadedView.CameraOrigin.xyz(), LocalToWorldBinding, PositionBinding, ColorBinding);
			}
		};
		Apply(DrawThunk);

		glPopDebugGroup();

		std::chrono::duration<double, std::milli> DrawDelta = ProfilingClock::now() - SodapopStartTime;
		TotalDrawTimeMS = DrawDelta.count();
	}
}


bool PaintingSet::CanRender()
{
	std::function<bool(SDFModelShared)> QueryThunk = [&](SDFModelShared Model)
	{
		return Model->Painter && Model->Painter->MeshAvailable;
	};
	return Select(QueryThunk) != nullptr;
}


void PaintingSet::RenderFrame(const int ScreenWidth, const int ScreenHeight, const ViewInfoUpload& UploadedView)
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		RenderFrameGL4(ScreenWidth, ScreenHeight, UploadedView);
	}
	else if (GraphicsBackend == GraphicsAPI::OpenGLES2)
	{
		RenderFrameES2(ScreenWidth, ScreenHeight, UploadedView);
	}
}


void PaintingSet::RegisterModel(SDFModelShared& NewModel)
{
	Models.push_back(NewModel);
}
