
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

#include "embedding.h"
#if EMBED_LUA
#include <string>
#include <random>
#include <map>
#include "controller.h"
#include "sdf_model.h"
#include "lua_material.h"

using LuaRandomGeneratorT = std::mt19937;

struct LuaEnvironment : public ScriptEnvironment
{
	struct lua_State* L = nullptr;
	std::string Name = "";
	float MeshingDensityPush = 0.0;
	VertexSequence VertexOrderHint = VertexSequence::Shuffle;
	bool GarbageCollectionRequested = false;
	LuaRandomGeneratorT RandomNumberGenerator = LuaRandomGeneratorT();

	std::shared_ptr<class PaintingSet> GlobalPaintingSet = nullptr;

	LuaEnvironment();

	virtual Language GetLanguage()
	{
		return Language::Lua;
	}

	void MaybeRunGarbageCollection();

	virtual void Advance(double DeltaTimeMs, double ElapsedTimeMs);

	virtual void JoystickConnect(const JoystickInfo& Joystick);
	virtual void JoystickDisconnect(const JoystickInfo& Joystick);
	virtual void JoystickAxis(SDL_JoystickID JoystickID, int Axis, float Value);
	virtual void JoystickButton(SDL_JoystickID JoystickID, int Button, bool Pressed);

	virtual void LoadFromPath(std::string Path);
	virtual void LoadFromString(std::string Source);
	virtual ~LuaEnvironment();

	static LuaEnvironment* GetScriptEnvironment(struct lua_State* L);
	static int LuaSetTitle(struct lua_State* L);
	static int LuaShowDebugMenu(struct lua_State* L);
	static int LuaHideDebugMenu(struct lua_State* L);
	static int LuaSetAdvanceEvent(struct lua_State* L);
	static int LuaSetJoystickConnectEvent(struct lua_State* L);
	static int LuaSetJoystickDisconnectEvent(struct lua_State* L);
	static int LuaSetJoystickAxisEvent(struct lua_State* L);
	static int LuaSetJoystickButtonEvent(struct lua_State* L);
	static int LuaPushMeshingDensity(struct lua_State* L);
	static int LuaSetConvergenceHint(struct lua_State* L);

	bool HandleError(int Error);

	static LuaRandomGeneratorT& GetRandomNumberEngine(struct lua_State* L);

	static MaterialShared GetGenericMaterial(lua_State* L, const ColorPoint Color);

private:
	int AdvanceCallbackRef;
	int JoystickConnectCallbackRef;
	int JoystickDisconnectCallbackRef;
	int JoystickAxisCallbackRef;
	int JoystickButtonCallbackRef;
	SDFModelShared GlobalModel;
	std::map<const ColorPoint, MaterialShared, ColorPointCmp> GenericMaterialVault;

	void LoadLuaModelCommon();
};

#endif //EMBED_LUA
