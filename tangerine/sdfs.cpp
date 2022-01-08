
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
#include <fstream>
#include <vector>
#include <map>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include "sdfs.h"


using TreeHandle = void*;
using namespace glm;
using namespace std::placeholders;


// These are to patch over some differences between glsl and glm.
float min(float LHS, float RHS)
{
	return std::fminf(LHS, RHS);
}
float max(float LHS, float RHS)
{
	return std::fmaxf(LHS, RHS);
}
vec2 max(vec2 LHS, float RHS)
{
	return max(LHS, vec2(RHS));
}
vec3 max(vec3 LHS, float RHS)
{
	return max(LHS, vec3(RHS));
}


namespace SDF
{
#define SDF_MATH_ONLY
#include "../shaders/math.glsl"
}


vec3 SDFNode::Gradient(vec3 Point)
{
	float AlmostZero = 0.0001;
	float Dist = Eval(Point);
	return normalize(vec3(
		Eval(vec3(Point.x + AlmostZero, Point.y, Point.z)) - Dist,
		Eval(vec3(Point.x, Point.y + AlmostZero, Point.z)) - Dist,
		Eval(vec3(Point.x, Point.y, Point.z + AlmostZero)) - Dist));
}


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.
struct BrushNode : public SDFNode
{
	using EvalMixin = std::function<float(vec3)>;
	using TransformMixin = std::function<vec3(vec3)>;

	EvalMixin BrushFn;
	TransformMixin* Transform;

	BrushNode(EvalMixin& InBrushFn, void* InTransform)
		: BrushFn(InBrushFn)
		, Transform((TransformMixin*)InTransform)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn((*Transform)(Point));
	}

	virtual ~BrushNode()
	{
		delete Transform;
	}
};


struct SetNode : public SDFNode
{
	using EvalMixin = std::function<float(float, float)>;

	EvalMixin SetFn;
	SDFNode* LHS;
	SDFNode* RHS;

	SetNode(EvalMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS)
		: SetFn(InSetFn)
		, LHS(InLHS)
		, RHS(InRHS)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return SetFn(
			LHS->Eval(Point),
			RHS->Eval(Point));
	}

	virtual ~SetNode()
	{
		delete LHS;
		delete RHS;
	}
};


// The following API functions provide a means for Racket to compose executable
// signed distance functions.  These are intended for tasks like calculating
// voxel membership and mesh generation, where the frequency of evaluating the
// distance field would be prohibetively slow to perform from racket.


// Evaluate a SDF tree.
extern "C" TANGERINE_API float EvalTree(void* Handle, float X, float Y, float Z)
{
	return ((SDFNode*)Handle)->Eval(vec3(X, Y, Z));
}


// Delete a CSG operator tree that was constructed with the functions below.
extern "C" TANGERINE_API void DiscardTree(void* Handle)
{
	delete (SDFNode*)Handle;
}


// The following functions provide transform constructors to be used by brush nodes.
extern "C" TANGERINE_API void* MakeIdentity()
{
	return new BrushNode::TransformMixin(
		[](vec3 Point) -> vec3
		{
			return Point;
		});
}

extern "C" TANGERINE_API void* MakeTranslation(float X, float Y, float Z)
{
	vec3 Offset(X, Y, Z);
	return new BrushNode::TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point - Offset;
		});
}

extern "C" TANGERINE_API void* MakeMatrixTransform(
	float X1, float Y1, float Z1, float W1,
	float X2, float Y2, float Z2, float W2,
	float X3, float Y3, float Z3, float W3,
	float X4, float Y4, float Z4, float W4)
{
	mat4 Matrix(
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4);

	return new BrushNode::TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = Matrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(void* Transform, float Radius)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(void* Transform, float RadipodeX, float RadipodeY, float RadipodeZ)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeBoxBrush(void* Transform, float ExtentX, float ExtentY, float ExtentZ)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeTorusBrush(void* Transform, float MajorRadius, float MinorRadius)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(void* Transform, float Radius, float Extent)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);
	return new BrushNode(Eval, Transform);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothUnionOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothCutOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothIntersectionOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}


struct  Vec3Less
{
	bool operator() (const vec3& LHS, const vec3& RHS) const
	{
		return LHS.x < RHS.x || (LHS.x == RHS.x && LHS.y < RHS.y) || (LHS.x == RHS.x && LHS.y == RHS.y && LHS.z < RHS.z);
	}
};


void Pool(const std::function<void()>& Thunk)
{
	static const int ThreadCount = max(std::thread::hardware_concurrency(), 2);
	std::vector<std::thread> Threads;
	Threads.reserve(ThreadCount);
	for (int i = 0; i < ThreadCount; ++i)
	{
		Threads.push_back(std::thread(Thunk));
	}
	for (auto& Thread : Threads)
	{
		Thread.join();
	}
}


std::atomic_bool ExportActive;
std::atomic_int ExportState(0);
std::atomic_int VoxelCount;
std::atomic_int GenerationProgress;
std::atomic_int VertexCount;
std::atomic_int RefinementProgress;
std::atomic_int QuadCount;
std::atomic_int WriteProgress;
void MeshExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step)
{
	const vec3 Half = Step / vec3(2.0);
	const float Diagonal = length(Step);

	std::vector<vec3> Vertices;
	std::map<vec3, int, Vec3Less> VertMemo;
	std::mutex VerticesCS;

	auto NewVert = [&](vec3 Vertex) -> int
	{
		VerticesCS.lock();
		auto Found = VertMemo.find(Vertex);
		if (Found != VertMemo.end())
		{
			VerticesCS.unlock();
			return Found->second;
		}
		else
		{
			int Next = Vertices.size();
			VertMemo[Vertex] = Next;
			Vertices.push_back(Vertex);
			VerticesCS.unlock();
			return Next;
		}
	};

	std::vector<ivec4> Quads;
	std::mutex QuadsCS;

	{
		const vec3 Start = ModelMin;
		const vec3 Stop = ModelMax + Step;
		const ivec3 Iterations = ivec3(ceil((Stop - Start) / Step));
		const int Slice = Iterations.x * Iterations.y;
		const int TotalCells = Iterations.x * Iterations.y * Iterations.z;
		VoxelCount.store(TotalCells);

		Pool([&]() \
		{
			while (ExportState.load() == 1 && ExportActive.load())
			{
				int i = GenerationProgress.fetch_add(1);
				if (i < TotalCells)
				{
					float Z = float(i / Slice) * Step.z + Start.z;
					float Y = float((i % Slice) / Iterations.x) * Step.y + Start.y;
					float X = float(i % Iterations.x) * Step.x + Start.x;

					vec3 Cursor = vec3(X, Y, Z) + Half;
					float Dist = Evaluator->Eval(Cursor);
					float DistX = Evaluator->Eval(Cursor - vec3(Step.x, 0.0, 0.0));
					float DistY = Evaluator->Eval(Cursor - vec3(0.0, Step.y, 0.0));
					float DistZ = Evaluator->Eval(Cursor - vec3(0.0, 0.0, Step.z));

					if (sign(Dist) != sign(DistX))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, 1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0, 1.0) + Cursor));
						if (sign(Dist) > sign(DistX))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist) != sign(DistY))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, 1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0, 1.0) + Cursor));
						if (sign(Dist) > sign(DistY))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist) != sign(DistZ))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, 1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, -1.0) + Cursor));
						if (sign(Dist) > sign(DistZ))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}
				}
				else
				{
					break;
				}
			}
		});
	}

	ExportState.store(2);
	VertexCount.store(Vertices.size());
	QuadCount.store(Quads.size());

	{
		Pool([&]() \
		{
			while (ExportState.load() == 2 && ExportActive.load())
			{
				int i = RefinementProgress.fetch_add(1);
				if (i < Vertices.size())
				{
					vec3& Vertex = Vertices[i];
					vec3 Cursor = Vertex;
					for (int i = 0; i < 5; ++i)
					{
						vec3 RayDir = Evaluator->Gradient(Cursor);
						float Dist = Evaluator->Eval(Cursor) * -1.0;
						Cursor += RayDir * Dist;
					}
					if (distance(Vertex, Cursor) <= Diagonal)
					{
						Vertex = Cursor;
					}
				}
				else
				{
					break;
				}
			}
		});
	}

	ExportState.store(3);

	{
		std::ofstream OutFile;
		OutFile.open("test.stl", std::ios::out | std::ios::binary);

		// Write 80 bytes for the header.
		for (int i = 0; i < 80; ++i)
		{
			OutFile << '\0';
		}

		uint32_t Triangles = Quads.size() * 2;
		OutFile.write(reinterpret_cast<char*>(&Triangles), 4);

		for (ivec4& Quad : Quads)
		{
			if (ExportState.load() != 3 || !ExportActive.load())
			{
				break;
			}
			WriteProgress.fetch_add(1);
			{
				vec3 Center = (Vertices[Quad.x] + Vertices[Quad.y] + Vertices[Quad.z]) / vec3(3.0);
				vec3 Normal = Evaluator->Gradient(Center);
				OutFile.write(reinterpret_cast<char*>(&Normal), 12);

				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.x]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.y]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.z]), 12);

				uint16_t Attributes = 0;
				OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
			}
			{
				vec3 Center = (Vertices[Quad.x] + Vertices[Quad.z] + Vertices[Quad.w]) / vec3(3.0);
				vec3 Normal = Evaluator->Gradient(Center);
				OutFile.write(reinterpret_cast<char*>(&Normal), 12);

				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.x]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.z]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.w]), 12);

				uint16_t Attributes = 0;
				OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
			}
		}

		// Align to 4 bytes for good luck.
		size_t Written = 84 + 100 * Quads.size();
		for (int i = 0; i < Written % 4; ++i)
		{
			OutFile << '\0';
		}

		OutFile.close();
	}

	ExportState.store(0);
}


ExportProgress GetExportProgress()
{
	ExportProgress Progress;
	Progress.Stage = ExportState.load();
	Progress.Generation = float(GenerationProgress.load() - 1) / float(VoxelCount.load());
	Progress.Refinement = float(RefinementProgress.load() - 1) / float(VertexCount.load());
	Progress.Write = float(WriteProgress.load() - 1) / float(QuadCount.load());
	return Progress;
}


void MeshExport(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step)
{
	if (ExportState.load() == 0)
	{
		ExportActive.store(true);
		ExportState.store(0);
		GenerationProgress.store(0);
		RefinementProgress.store(0);
		WriteProgress.store(0);
		ExportState.store(1);
		std::thread ExportThread(MeshExportThread, Evaluator, ModelMin, ModelMax, Step);
		ExportThread.detach();
	}
}

void CancelExport(bool Halt)
{
	if (Halt)
	{
		ExportActive.store(false);
	}
	else
	{
		ExportState.fetch_add(1);
	}
}
