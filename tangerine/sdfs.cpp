
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
#include <string>
#include <nfd.h>
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


using BrushMixin = std::function<float(vec3)>;
using TransformMixin = std::function<vec3(vec3)>;
using SetMixin = std::function<float(float, float)>;


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.


struct TransformNode : public SDFNode
{
	TransformMixin TransformFn;
	SDFNode* Child;

	TransformNode(TransformMixin& InTransformFn, SDFNode* InChild)
		: TransformFn(InTransformFn)
		, Child(InChild)
	{
	}

	virtual vec3 Transform(vec3 Point)
	{
		return Point;
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(TransformFn(Point));
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		SDFNode* NewChild = Child->Clip(TransformFn(Point), Radius);
		if (NewChild)
		{
			return new TransformNode(TransformFn, NewChild);
		}
		else
		{
			return nullptr;
		}
	}

	virtual ~TransformNode()
	{
		delete Child;
	}
};


struct BrushNode : public SDFNode
{
	BrushMixin BrushFn;

	BrushNode(BrushMixin& InBrushFn)
		: BrushFn(InBrushFn)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn(Point);
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (Eval(Point) <= Radius)
		{
			return new BrushNode(BrushFn);
		}
		else
		{
			return nullptr;
		}
	}
};


enum class SetFamily
{
	Union,
	Diff,
	Inter
};


template<SetFamily Family, bool BlendMode>
struct SetNode : public SDFNode
{
	SetMixin SetFn;
	SDFNode* LHS;
	SDFNode* RHS;
	float Threshold;

	SetNode(SetMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS, float InThreshold = 0.0)
		: SetFn(InSetFn)
		, LHS(InLHS)
		, RHS(InRHS)
		, Threshold(InThreshold)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return SetFn(
			LHS->Eval(Point),
			RHS->Eval(Point));
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (BlendMode)
		{
			// If both of these clip tests pass, then the point should be in the blending region
			// for all blending set operator types.  If one of these returns nullptr, the other
			// should be deleted.  If we don't return a new blending set node here, fail through
			// to the regular set operator behavior to return an operand, when applicable.

			SDFNode* NewLHS = LHS->Clip(Point, Radius + Threshold);
			SDFNode* NewRHS = RHS->Clip(Point, Radius + Threshold);
			if (NewLHS && NewRHS)
			{
				return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS);
			}
			else if (NewLHS)
			{
				delete NewLHS;
			}
			else if (NewRHS)
			{
				delete NewRHS;
			}
			if (Family == SetFamily::Inter)
			{
				return nullptr;
			}
		}

		SDFNode* NewLHS = LHS->Clip(Point, Radius);
		SDFNode* NewRHS = RHS->Clip(Point, Radius);

		if (NewLHS && NewRHS)
		{
			// Note, this shouldn't be possible to hit when BlendMode == true.
			return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS);
		}
		else if (Family == SetFamily::Union)
		{
			// Return whichever operand matched or nullptr.
			return NewLHS != nullptr ? NewLHS : NewRHS;
		}
		else if (Family == SetFamily::Diff)
		{
			// We can only return the LHS side, which may be nullptr.
			if (NewRHS)
			{
				delete NewRHS;
			}
			return NewLHS;
		}
		else if (Family == SetFamily::Inter)
		{
			// Neither operand is valid.
			if (NewLHS)
			{
				delete NewLHS;
			}
			else if (NewRHS)
			{
				delete NewRHS;
			}
			return nullptr;
		}
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
	return new TransformMixin(
		[](vec3 Point) -> vec3
		{
			return Point;
		});
}


extern "C" TANGERINE_API void* MakeTranslation(float X, float Y, float Z, void* Child)
{
	vec3 Offset(X, Y, Z);
	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point - Offset;
		});

	return new TransformNode(Eval, (SDFNode*)Child);
}


extern "C" TANGERINE_API void* MakeMatrixTransform(
	float X1, float Y1, float Z1, float W1,
	float X2, float Y2, float Z2, float W2,
	float X3, float Y3, float Z3, float W3,
	float X4, float Y4, float Z4, float W4,
	void* Child)
{
	mat4 Matrix(
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4);

	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = Matrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});

	return new TransformNode(Eval, (SDFNode*)Child);
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(float Radius)
{
	BrushMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(float RadipodeX, float RadipodeY, float RadipodeZ)
{
	BrushMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeBoxBrush(float ExtentX, float ExtentY, float ExtentZ)
{
	BrushMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeTorusBrush(float MajorRadius, float MinorRadius)
{
	float Radius = MajorRadius + MinorRadius;
	BrushMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(float Radius, float Extent)
{
	BrushMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);
	return new BrushNode(Eval);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode<SetFamily::Union, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode<SetFamily::Diff, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode<SetFamily::Inter, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothUnionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Union, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothCutOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Diff, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothIntersectionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Inter, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
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
void MeshExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, std::string Path)
{
	const vec3 Half = Step / vec3(2.0);
	const float Diagonal = length(Half);

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

					vec4 Dist;
					{
						SDFNode* Region = Evaluator->Clip(vec3(X, Y, Z), Diagonal * 2.0);
						if (Region == nullptr)
						{
							continue;
						}
						Dist.x = Region->Eval(Cursor - vec3(Step.x, 0.0, 0.0));
						Dist.y = Region->Eval(Cursor - vec3(0.0, Step.y, 0.0));
						Dist.z = Region->Eval(Cursor - vec3(0.0, 0.0, Step.z));
						Dist.w = Region->Eval(Cursor);
						delete Region;
					}

					if (sign(Dist.w) != sign(Dist.x))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0,  1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0,  1.0,  1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0,  1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.x))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist.w) != sign(Dist.y))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, 1.0) + Cursor),
							NewVert(Half * vec3( 1.0, -1.0, 1.0) + Cursor),
							NewVert(Half * vec3( 1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.y))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist.w) != sign(Dist.z))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3( 1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3( 1.0,  1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0,  1.0, -1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.z))
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

	if (RefineIterations > 0)
	{
		Pool([&]() \
		{
			while (ExportState.load() == 2 && ExportActive.load())
			{
				int i = RefinementProgress.fetch_add(1);
				if (i < Vertices.size())
				{
					vec3& Vertex = Vertices[i];
					vec3 Low = Vertex - vec3(Half);
					vec3 High = Vertex + vec3(Half);

					SDFNode* Subtree = Evaluator->Clip(Vertex, Diagonal);
					if (!Subtree)
					{
						continue;
					}

					vec3 Cursor = Vertex;
					for (int r = 0; r < RefineIterations; ++r)
					{
						vec3 RayDir = Subtree->Gradient(Cursor);
						float Dist = Subtree->Eval(Cursor) * -1.0;
						Cursor += RayDir * Dist;
					}
					delete Subtree;
					Cursor = clamp(Cursor, Low, High);

					if (distance(Cursor, Vertex) <= Diagonal)
					{
						// TODO: despite the above clamp, some times the Cursor will end up on 0,0,0 when it would be
						// well outside a half voxel distance.  This branch should at least prevent that, but there is
						// probably a problem with the Gradient function that is causing this.
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
		OutFile.open(Path, std::ios::out | std::ios::binary);

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


void MeshExport(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations)
{
	if (ExportState.load() == 0)
	{
		nfdchar_t* Path = nullptr;
		nfdresult_t Result = NFD_SaveDialog("stl", "model.stl", &Path);
		if (Result == NFD_OKAY)
		{
			ExportActive.store(true);
			ExportState.store(0);
			GenerationProgress.store(0);
			RefinementProgress.store(0);
			WriteProgress.store(0);
			ExportState.store(1);

			std::thread ExportThread(MeshExportThread, Evaluator, ModelMin, ModelMax, Step, RefineIterations, std::string(Path));
			ExportThread.detach();
		}
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
