
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

#include "sdf_model.h"
#include "sdf_rendering.h"
#include "perf.h"
#include "sodapop.h"
#include "scheduler.h"
#include <fmt/format.h>


std::vector<SDFModelWeakRef> LiveModels;


// FIXME
// Originally this was meant to have a wrapped evaluator for the key type, with
// the idea being that equivalent evaluators would be matched as equal.  This
// required having the key type hold and release references to the evaluator.
// Anyways, std::vector resizes were triggering the destructor inappropriately,
// resulting in a premature call to release on the wrapped evaluator!  This
// would cause evalutors to seemingly randomly be freed early, segfaults, and
// so on.
// To work around this, this cache just takes a pointer disguised as a size_t.
// This allows instances created from the same SDFNode to be deduplicated, but
// nothing else.
// I should probably make all of the refcounted stuff immobile to prevent these
// kinds of issues.
std::vector<std::pair<size_t, DrawableWeakRef>> DrawableCache;
std::mutex DrawableCacheCS;


std::vector<SDFModelWeakRef>& GetLiveModels()
{
	return LiveModels;
}


std::vector<std::pair<size_t, DrawableWeakRef>>& GetDrawableCache()
{
	return DrawableCache;
}


void PruneStaleDrawableFromCache()
{
	for (auto Iterator = DrawableCache.begin(); Iterator != DrawableCache.end(); ++Iterator)
	{
		if (Iterator->second.expired())
		{
			DrawableCache.erase(Iterator);
			break;
		}
	}
}


void UnloadAllModels()
{
	LiveModels.clear();
}


void GetIncompleteModels(std::vector<SDFModelWeakRef>& Incomplete)
{
	Incomplete.clear();
	Incomplete.reserve(LiveModels.size());

#if RENDERER_COMPILER
	if (CurrentRenderer == Renderer::ShapeCompiler)
	{
		for (SDFModelWeakRef WeakRef : LiveModels)
		{
			SDFModelShared Model = WeakRef.lock();
			if (Model)
			{
				VoxelDrawableShared Painter = std::static_pointer_cast<VoxelDrawable>(Model->Painter);
				if (Painter->HasPendingShaders())
				{
					Incomplete.push_back(Model);
				}
			}
		}
	}
#endif // RENDERER_COMPILER
#if RENDERER_SODAPOP
	if (CurrentRenderer == Renderer::Sodapop)
	{
		for (SDFModelWeakRef WeakRef : LiveModels)
		{
			SDFModelShared Model = WeakRef.lock();
			if (Model)
			{
				SodapopDrawableShared Painter = std::static_pointer_cast<SodapopDrawable>(Model->Painter);
				if (!Painter->MeshReady.load())
				{
					Incomplete.push_back(Model);
				}
			}
		}
	}
#endif // RENDERER_SODAPOP
}


void GetRenderableModels(std::vector<SDFModelWeakRef>& Renderable)
{
	Renderable.clear();
	Renderable.reserve(LiveModels.size());

#if RENDERER_COMPILER
	if (CurrentRenderer == Renderer::ShapeCompiler)
	{
		for (SDFModelWeakRef WeakRef : LiveModels)
		{
			SDFModelShared Model = WeakRef.lock();
			if (Model)
			{
				VoxelDrawableShared Painter = std::static_pointer_cast<VoxelDrawable>(Model->Painter);
				if (Painter->HasCompleteShaders())
				{
					Renderable.push_back(Model);
				}
			}
		}
	}
#endif // RENDERER_COMPILER
#if RENDERER_SODAPOP
	if (CurrentRenderer == Renderer::Sodapop)
	{
		for (SDFModelWeakRef WeakRef : LiveModels)
		{
			SDFModelShared Model = WeakRef.lock();
			if (Model)
			{
				SodapopDrawableShared Painter = std::static_pointer_cast<SodapopDrawable>(Model->Painter);
				if (Painter->MeshReady.load())
				{
					Renderable.push_back(Model);
				}
			}
		}
	}
#endif // RENDERER_SODAPOP
}


bool MatchEvent(SDFModel& Model, MouseEvent& Event)
{
	return (Model.MouseListenFlags & MOUSE_FLAG(Event.Type)) == MOUSE_FLAG(Event.Type);
}


bool DeliverMouseMove(glm::vec3 Origin, glm::vec3 RayDir, int MouseX, int MouseY)
{
	bool ReturnToSender = true;

	// TODO

	return ReturnToSender;
}


bool DeliverMouseButton(MouseEvent Event)
{
	bool ReturnToSender = true;

	float Nearest = std::numeric_limits<float>::infinity();
	SDFModelShared NearestMatch = nullptr;
	std::vector<SDFModelShared> MouseUpRecipients;

	const bool Press = Event.Type == MOUSE_DOWN;
	const bool Release = Event.Type == MOUSE_UP;

	for (SDFModelWeakRef& WeakRef : LiveModels)
	{
		// TODO:
		// I'm unsure if this is how I want to handle mouse button event routing.
		// Obviously the most useful form is an env registers a mouse down event on
		// one of its models to find when the model is clicked, double clicked, or to
		// start listening to mouse move.
		// I'm pretty sure listening for a global mouse up is also useful, because if
		// the down event us used to start some interaction state machine, we probably
		// want to be able to use the up event to terminate the machine even if the
		// model is occluded.  This part is fine.
		// Quesiton is, is it useful for models to register a global mouse down event?
		// I'm guessing "maybe yes maybe" - for example you click on a model in a pallet,
		// then click on the board to place an instance of it, like a paint program.
		// Likewise it would be useful for some models to opt out of blocking the ray
		// queries, and for others to be able to block the ray queries without registering
		// a handler.
		// This is probably fine at least until the events can be forwarded back to
		// the script envs.

		SDFModelShared Model = WeakRef.lock();
		if (Model)
		{
			if (MatchEvent(*Model, Event))
			{
				if (Release)
				{
					MouseUpRecipients.push_back(Model);
				}
				if (Model->Visible)
				{
					RayHit Query = Model->RayMarch(Event.RayOrigin, Event.RayDir);
					if (Query.Hit && Query.Travel < Nearest)
					{
						Nearest = Query.Travel;
						NearestMatch = Model;
						Event.AnyHit = true;
						Event.Cursor = Query.Position;
					}
				}
			}
		}
	}

	if (Press && NearestMatch)
	{
		ReturnToSender = false;
		NearestMatch->OnMouseEvent(Event, true);
	}

	if (MouseUpRecipients.size() > 0)
	{
		ReturnToSender = false;
		for (SDFModelShared& Recipient : MouseUpRecipients)
		{
			Recipient->OnMouseEvent(Event, Recipient == NearestMatch);
		}
	}

	return ReturnToSender;
}


bool DeliverMouseScroll(glm::vec3 Origin, glm::vec3 RayDir, int ScrollX, int ScrollY)
{
	bool ReturnToSender = true;

	// TODO

	return ReturnToSender;
}


#if RENDERER_COMPILER
bool VoxelDrawable::HasPendingShaders()
{
	return PendingShaders.size() > 0;
}


bool VoxelDrawable::HasCompleteShaders()
{
	return CompiledTemplates.size() > 0;
}


void VoxelDrawable::CompileNextShader()
{
	BeginEvent("Compile Shader");

	size_t TemplateIndex = PendingShaders.back();
	PendingShaders.pop_back();

	ProgramTemplate& ProgramFamily = ProgramTemplates[TemplateIndex];
	ProgramFamily.StartCompile();
	if (ProgramFamily.ProgramVariants.size() > 0)
	{
		CompiledTemplates.push_back(&ProgramFamily);
	}

	EndEvent();
}


VoxelDrawable::~VoxelDrawable()
{
	Scheduler::EnqueueDelete(PruneStaleDrawableFromCache);

	for (ProgramTemplate& ProgramFamily : ProgramTemplates)
	{
		ProgramFamily.Release();
	}

	ProgramTemplates.clear();
	ProgramTemplateSourceMap.clear();
	PendingShaders.clear();
	CompiledTemplates.clear();
}
#endif // RENDERER_COMPILER


#if RENDERER_SODAPOP
SodapopDrawable::SodapopDrawable(const std::string& InName, SDFNodeShared& InEvaluator)
{
	ReadyDelay = std::chrono::duration<double, std::milli>::zero();
	Name = InName;
	Evaluator = InEvaluator;
	MeshReady.store(false);

	{
		SDFNode::MaterialWalkCallback WalkCallback = [this](MaterialShared Material)
		{
			size_t NextIndex = MaterialSlots.size();
			auto [Iterator, MaterialIsNew] = this->SlotLookup.insert(std::pair{ Material, NextIndex });
			if (MaterialIsNew)
			{
				MaterialSlots.emplace_back(Material);
			}
		};
		Evaluator->WalkMaterials(WalkCallback);
	}
}


SodapopDrawable::~SodapopDrawable()
{
	if (Scratch != nullptr)
	{
		Sodapop::DeleteMeshingScratch(Scratch);
		Scratch = nullptr;
	}
	Evaluator.reset();
	MaterialSlots.clear();
	SlotLookup.clear();
	Scheduler::EnqueueDelete(PruneStaleDrawableFromCache);
}
#endif // RENDERER_SODAPOP


RayHit SDFModel::RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations, float Epsilon)
{
	glm::vec3 RelativeOrigin = Transform.ApplyInverse(RayStart);
	glm::mat3 Rotation = (glm::mat3)Transform.LastFoldInverse;
	glm::vec3 RelativeRayDir = Rotation * RayDir;
	return Evaluator->RayMarch(RelativeOrigin, RelativeRayDir, 1000);
}


SDFModel::SDFModel(SDFNodeShared& InEvaluator, const std::string& InName, const float VoxelSize, const float MeshingDensityPush)
{
	Evaluator = nullptr;

	size_t Key = (size_t)(InEvaluator.get());
	if (InName.size() > 0)
	{
		Name = fmt::format("{} : {}", InName, (void*)(&InEvaluator));
	}
	else
	{
		Name = fmt::format("{}", (void*)(&InEvaluator));
	}

	{
		std::scoped_lock CacheLock(DrawableCacheCS);

		for (auto& Entry : DrawableCache)
		{
			if (Entry.first == Key)
			{
				Painter = Entry.second.lock();
				if (Painter)
				{
					break;
				}
			}
		}

		if (!Painter)
		{
			// TODO:  This copy ensures that any parallel work on the evaluator gets an evaluator with
			// all of its transforms folded, and no branches in common with another model.  As this new
			// evaluator is still mutable, it would be best to replace it with something that provides
			// stronger thread safety guarantees.
			Evaluator = InEvaluator->Copy(true);

#if RENDERER_COMPILER
			if (CurrentRenderer == Renderer::ShapeCompiler)
			{
				VoxelDrawableShared VoxelPainter = std::make_shared<VoxelDrawable>(Name, Evaluator);
				VoxelPainter->Compile(VoxelSize);
				Painter = std::static_pointer_cast<Drawable>(VoxelPainter);
				DrawableCache.emplace_back(Key, Painter);
			}
#endif // RENDERER_COMPILER
#if RENDERER_SODAPOP
			if (CurrentRenderer == Renderer::Sodapop)
			{
				SodapopDrawableShared MeshPainter = std::make_shared<SodapopDrawable>(Name, Evaluator);
				Painter = std::static_pointer_cast<Drawable>(MeshPainter);
				DrawableCache.emplace_back(Key, Painter);
				Sodapop::Populate(MeshPainter, MeshingDensityPush);
			}
#endif // RENDERER_SODAPOP
		}
		else
		{
			Evaluator = Painter->Evaluator;
		}
	}

	TransformBuffer.DebugName = "Instance Transforms Buffer";
}


void SDFModel::RegisterNewModel(SDFModelShared& NewModel)
{
	LiveModels.emplace_back(NewModel);
	Sodapop::Attach(NewModel);
}


SDFModelShared SDFModel::Create(SDFNodeShared& InEvaluator, const std::string& InName, const float VoxelSize, const float MeshingDensityOffsetRequest)
{
	SDFModelShared NewModel(new SDFModel(InEvaluator, InName, VoxelSize, MeshingDensityOffsetRequest));
	SDFModel::RegisterNewModel(NewModel);
	return NewModel;
}


SDFModel::~SDFModel()
{
	TransformBuffer.Release();

	for (int i = 0; i < LiveModels.size(); ++i)
	{
		if (LiveModels[i].expired())
		{
			LiveModels.erase(LiveModels.begin() + i);
			break;
		}
	}

	Evaluator.reset();
	Painter.reset();
}
