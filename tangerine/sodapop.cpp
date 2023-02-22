
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
#include <vector>
#include <random>
#include <thread>
#include <atomic>
#include <mutex>


static ShaderProgram PaintShader;


struct Bubble
{
	std::default_random_engine RNGesus;

	SDFNode* SDF;
	glm::mat4 LocalToWorld;

	Buffer ModelInfo;
	Buffer IndexBuffer;
	Buffer PositionBuffer;
	Buffer ColorBuffer;

	std::vector<int> Indices;
	std::vector<glm::vec4> Positions;
	std::vector<glm::vec4> Colors;

	std::mutex ColorsCS;

	Bubble(SDFNode* InSDF)
		: SDF(InSDF)
		, LocalToWorld(glm::identity<glm::mat4>())
		, ModelInfo("ModelInfo")
		, PositionBuffer("PositionBuffer")
	{
		SDF->Hold();
		const AABB Bounds = SDF->Bounds();
		const glm::vec3 Extent = Bounds.Extent();
		const int Iterations = 51;

		auto Record = [&](glm::vec4 Position) -> int
		{
			int Index = 0;
			for (; Index < Positions.size(); ++Index)
			{
				if (Positions[Index] == Position)
				{
					return Index;
				}
			}
			Positions.push_back(Position);
			return Index;
		};

		{
			for (int Z = 0; Z < Iterations; ++Z)
			{
				for (int X = 0; X < Iterations; ++X)
				{
					// Y-
					{
						const float Y = Bounds.Min.y;
						const glm::vec2 TileMin = glm::vec2(X, Z);
						const glm::vec2 TileMax = glm::vec2(X + 1, Z + 1);
						const glm::vec2 Step = Extent.xz / glm::vec2(float(Iterations));
						const glm::vec2 Min = Step * TileMin + Bounds.Min.xz;
						const glm::vec2 Max = Step * TileMax + Bounds.Min.xz;

						glm::vec4 Face[4] = {
							glm::vec4(Max.x, Y, Max.y, 1.0),
							glm::vec4(Min.x, Y, Max.y, 1.0),
							glm::vec4(Min.x, Y, Min.y, 1.0),
							glm::vec4(Max.x, Y, Min.y, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}

					// Y+
					{
						const float Y = Bounds.Max.y;
						const glm::vec2 TileMin = glm::vec2(X, Z);
						const glm::vec2 TileMax = glm::vec2(X + 1, Z + 1);
						const glm::vec2 Step = Extent.xz / glm::vec2(float(Iterations)) * glm::vec2(-1, 1);
						const glm::vec2 Min = Step * TileMin + glm::vec2(Bounds.Max.x, Bounds.Min.z);
						const glm::vec2 Max = Step * TileMax + glm::vec2(Bounds.Max.x, Bounds.Min.z);

						glm::vec4 Face[4] = {
							glm::vec4(Max.x, Y, Max.y, 1.0),
							glm::vec4(Min.x, Y, Max.y, 1.0),
							glm::vec4(Min.x, Y, Min.y, 1.0),
							glm::vec4(Max.x, Y, Min.y, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}
				}
			}
			for (int Z = 0; Z < Iterations; ++Z)
			{
				for (int Y = 0; Y < Iterations; ++Y)
				{
					// X-
					{
						const float X = Bounds.Min.x;
						const glm::vec2 TileMin = glm::vec2(Y, Z);
						const glm::vec2 TileMax = glm::vec2(Y + 1, Z + 1);
						const glm::vec2 Step = Extent.yz / glm::vec2(float(Iterations)) * glm::vec2(-1, 1);
						const glm::vec2 Min = Step * TileMin + glm::vec2(Bounds.Max.y, Bounds.Min.z);
						const glm::vec2 Max = Step * TileMax + glm::vec2(Bounds.Max.y, Bounds.Min.z);

						glm::vec4 Face[4] = {
							glm::vec4(X, Max.x, Max.y, 1.0),
							glm::vec4(X, Min.x, Max.y, 1.0),
							glm::vec4(X, Min.x, Min.y, 1.0),
							glm::vec4(X, Max.x, Min.y, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}
					// X+
					{
						const float X = Bounds.Max.x;
						const glm::vec2 TileMin = glm::vec2(Y, Z);
						const glm::vec2 TileMax = glm::vec2(Y + 1, Z + 1);
						const glm::vec2 Step = Extent.yz / glm::vec2(float(Iterations));
						const glm::vec2 Min = Step * TileMin + Bounds.Min.yz;
						const glm::vec2 Max = Step * TileMax + Bounds.Min.yz;

						glm::vec4 Face[4] = {
							glm::vec4(X, Max.x, Max.y, 1.0),
							glm::vec4(X, Min.x, Max.y, 1.0),
							glm::vec4(X, Min.x, Min.y, 1.0),
							glm::vec4(X, Max.x, Min.y, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}
				}
			}
			for (int Y = 0; Y < Iterations; ++Y)
			{
				for (int X = 0; X < Iterations; ++X)
				{
					// Z-
					{
						const float Z = Bounds.Min.z;
						const glm::vec2 TileMin = glm::vec2(X, Y);
						const glm::vec2 TileMax = glm::vec2(X + 1, Y + 1);
						const glm::vec2 Step = Extent.yz / glm::vec2(float(Iterations)) * glm::vec2(1, -1);
						const glm::vec2 Min = Step * TileMin + glm::vec2(Bounds.Min.x, Bounds.Max.y);
						const glm::vec2 Max = Step * TileMax + glm::vec2(Bounds.Min.x, Bounds.Max.y);

						glm::vec4 Face[4] = {
							glm::vec4(Max.x, Max.y, Z, 1.0),
							glm::vec4(Min.x, Max.y, Z, 1.0),
							glm::vec4(Min.x, Min.y, Z, 1.0),
							glm::vec4(Max.x, Min.y, Z, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}
					// Z+
					{
						const float Z = Bounds.Max.z;
						const glm::vec2 TileMin = glm::vec2(X, Y);
						const glm::vec2 TileMax = glm::vec2(X + 1, Y + 1);
						const glm::vec2 Step = Extent.yz / glm::vec2(float(Iterations));
						const glm::vec2 Min = Step * TileMin + Bounds.Min.xy;
						const glm::vec2 Max = Step * TileMax + Bounds.Min.xy;

						glm::vec4 Face[4] = {
							glm::vec4(Max.x, Max.y, Z, 1.0),
							glm::vec4(Min.x, Max.y, Z, 1.0),
							glm::vec4(Min.x, Min.y, Z, 1.0),
							glm::vec4(Max.x, Min.y, Z, 1.0)
						};

						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[1]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[0]));
						Indices.push_back(Record(Face[2]));
						Indices.push_back(Record(Face[3]));
					}
				}
			}
		}

		for (glm::vec4& Position : Positions)
		{
			const glm::vec3 Dir = SDF->Gradient(Position.xyz) * glm::vec3(-1);
			//const glm::vec3 Dir = glm::normalize(glm::vec3(Position.xyz)) * glm::vec3(-1);
			RayHit Result = SDF->RayMarch(Position.xyz, Dir);
			if (Result.Hit)
			{
				Position.xyz = Result.Position;
			}

			//const glm::vec4 Color = glm::vec4(glm::normalize(glm::vec3(Position.xyz)) * glm::vec3(.5) + glm::vec3(.5), 1.0);
			const glm::vec3 Normal = SDF->Gradient(Position.xyz);
			const glm::vec4 Color = glm::vec4(Normal * glm::vec3(.5) + glm::vec3(.5), 1);

			Colors.push_back(Color);
		}

		IndexBuffer.Upload(Indices.data(), sizeof(int) * Indices.size());
		PositionBuffer.Upload(Positions.data(), sizeof(glm::vec4) * Positions.size());
	}

	~Bubble()
	{
		SDF->Release();
	}

	void Retrace(glm::vec3 ViewOrigin)
	{
		std::uniform_real_distribution<double> Roll(0.0, std::nextafter(1.0, DBL_MAX));
		const int Update = int(Roll(RNGesus) * float(Colors.size() - 1));

		const glm::vec3 Position = (LocalToWorld * glm::vec4(Positions[Update].xyz, 1.0)).xyz;
		const glm::vec3 L = glm::normalize(Position - ViewOrigin);
		const glm::vec3 Normal = SDF->Gradient(Position);
		const float A = glm::max(-glm::dot(L, Normal), 0.0f);
		const glm::vec4 NewColor = glm::vec4(Normal * glm::vec3(.5) + glm::vec3(.5), 1) * glm::vec4(glm::vec3(A * A), 1.0);

		ColorsCS.lock();
		Colors[Update] = NewColor;
		ColorsCS.unlock();
	}

	void Refresh()
	{
		ColorsCS.lock();
		ColorBuffer.Upload(Colors.data(), sizeof(glm::vec4) * Colors.size());
		ColorsCS.unlock();

		ModelInfo.Upload((void*)&LocalToWorld, sizeof(LocalToWorld));
	}
};


Bubble* Fnord;

std::atomic_bool Live;
std::vector<std::thread> Threads;
void AsyncTracer();


StatusCode Sodapop::Setup()
{
#if _WIN64
	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("sodapop/yolo.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("sodapop/yolo.fs.glsl", true)} },
		"Outliner Shader"));
#else
	// Idk why cmake(?) is flattening out the collected sources
	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("yolo.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("yolo.fs.glsl", true)} },
		"Outliner Shader"));
#endif

	{
		SDFNode* Box = SDF::Box(2, 2, 2);
		SDFNode* Torus = SDF::Torus(3, 1.5);
		Box->Hold();
		Torus->Hold();
		SDFNode* Model = SDF::Diff(Box, Torus);
		Model->Hold();
		Fnord = new Bubble(Model);
		Model->Release();
	}

	Live.store(true);
	//const int ThreadCount = glm::max(int(std::thread::hardware_concurrency()), 2);
	const int ThreadCount = 1;
	Threads.reserve(ThreadCount);
	for (int i = 0; i < ThreadCount; ++i)
	{
		Threads.push_back(std::thread(AsyncTracer));
	}

	return StatusCode::PASS;
}


void Sodapop::Teardown()
{
	Live.store(false);
	for (auto& Thread : Threads)
	{
		Thread.join();
	}
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


std::mutex Viewtex;
glm::vec3 ViewOrigin(0, 0, 0);
void AsyncTracer()
{
	while (Live.load())
	{
		Viewtex.lock();
		glm::vec3 CurrentOrigin = ViewOrigin;
		Viewtex.unlock();

		Fnord->Retrace(CurrentOrigin);

		// This isn't strictly necessary since the lock above on Viewtex, and the other
		// lock in Bubble::Retrace will periodically block this thread.  However, this yield
		// will hopefully ensure the main thread is prioritized more consistently, along with
		// other work on the system.  This is expected to improve the overall responsiveness
		// of this program and the system it runs on at the expense of a marginal reduction
		// to async tracing throughput.
		std::this_thread::yield();
	}
}


void Sodapop::RenderFrame(int ScreenWidth, int ScreenHeight, double CurrentTime, glm::quat Orientation)
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

	Viewtex.lock();
	ViewOrigin = glm::vec3(0.0, -10.0, 0.0);
	const glm::vec3 FocalPoint(0.0, 0.0, 0.0);
	const glm::vec3 WorldUp(0.0, 0.0, 1.0);

#if 1
	const glm::quat WorldRotation = glm::angleAxis(float(glm::radians(CurrentTime / 50.0)), WorldUp);
	ViewOrigin = glm::rotate(WorldRotation, ViewOrigin);
	const glm::mat4 WorldToView = glm::lookAt(ViewOrigin, FocalPoint, WorldUp);
#else
	const glm::mat4 WorldToView = glm::lookAt(ViewOrigin, FocalPoint, WorldUp);

	Fnord->LocalToWorld = glm::toMat4(Orientation);
#endif
	Viewtex.unlock();

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

	Fnord->Refresh();

	Fnord->ModelInfo.Bind(GL_UNIFORM_BUFFER, 1);
	Fnord->IndexBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
	Fnord->PositionBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 3);
	Fnord->ColorBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 4);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);

	PaintShader.Activate();

	glDrawArrays(GL_TRIANGLES, 0, Fnord->Indices.size());
}
