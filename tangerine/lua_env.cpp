
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

#include "lua_env.h"
#if EMBED_LUA

#include "errors.h"
#include "tangerine.h"
#include "sodapop.h"
#include "painting_set.h"
#include "units.h"
#include "lua_painting_set.h"
#include "lua_material.h"
#include "lua_sdf.h"
#include "lua_vec.h"
#include "lua_color.h"
#include "lua_light.h"
#include <fmt/format.h>
#include <filesystem>


LuaEnvironment* LuaEnvironment::GetScriptEnvironment(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "tangerine.env");
	LuaEnvironment* Env = (LuaEnvironment*)lua_touserdata(L, lua_gettop(L));
	lua_pop(L, 1);
	return Env;
}


LuaRandomGeneratorT& LuaEnvironment::GetRandomNumberEngine(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);
	Assert(Env != nullptr);
	return Env->RandomNumberGenerator;
}


MaterialShared LuaEnvironment::GetGenericMaterial(lua_State* L, const ColorPoint Color)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);
	Assert(Env != nullptr);

	MaterialShared& Found = Env->GenericMaterialVault[Color];
	if (!Found)
	{
		Found = MaterialShared(new MaterialPBRBR(Color));
	}
	return Found;
}


int LuaEnvironment::LuaSetTitle(lua_State* L)
{
	const char* Title = luaL_checklstring(L, 1, nullptr);
	SetWindowTitle(Title);
	return 0;
}


int LuaEnvironment::LuaShowDebugMenu(lua_State* L)
{
	ShowDebugMenu();
	return 0;
}


int LuaEnvironment::LuaHideDebugMenu(lua_State* L)
{
	HideDebugMenu();
	return 0;
}


int LuaEnvironment::LuaSetAdvanceEvent(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);

	luaL_unref(L, LUA_REGISTRYINDEX, Env->AdvanceCallbackRef);

	if (lua_isnil(L, 1))
	{
		Env->CanAdvance = false;
		Env->AdvanceCallbackRef = LUA_REFNIL;
	}
	else
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		Env->CanAdvance = true;
		Env->AdvanceCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


int LuaEnvironment::LuaSetJoystickConnectEvent(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);

	luaL_unref(L, LUA_REGISTRYINDEX, Env->JoystickConnectCallbackRef);

	if (lua_isnil(L, 1))
	{
		Env->JoystickConnectCallbackRef = LUA_REFNIL;
	}
	else
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		Env->JoystickConnectCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


int LuaEnvironment::LuaSetJoystickDisconnectEvent(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);

	luaL_unref(L, LUA_REGISTRYINDEX, Env->JoystickDisconnectCallbackRef);

	if (lua_isnil(L, 1))
	{
		Env->JoystickDisconnectCallbackRef = LUA_REFNIL;
	}
	else
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		Env->JoystickDisconnectCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


int LuaEnvironment::LuaSetJoystickAxisEvent(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);

	luaL_unref(L, LUA_REGISTRYINDEX, Env->JoystickAxisCallbackRef);

	if (lua_isnil(L, 1))
	{
		Env->JoystickAxisCallbackRef = LUA_REFNIL;
	}
	else
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		Env->JoystickAxisCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


int LuaEnvironment::LuaSetJoystickButtonEvent(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);

	luaL_unref(L, LUA_REGISTRYINDEX, Env->JoystickButtonCallbackRef);

	if (lua_isnil(L, 1))
	{
		Env->JoystickButtonCallbackRef = LUA_REFNIL;
	}
	else
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		Env->JoystickButtonCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


int LuaEnvironment::LuaPushMeshingDensity(lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);
	Env->MeshingDensityPush = (float)luaL_checknumber(L, 1);
	return 0;
}


int LuaEnvironment::LuaSetConvergenceHint(struct lua_State* L)
{
	LuaEnvironment* Env = GetScriptEnvironment(L);
	const char* Hint = luaL_checklstring(L, 1, nullptr);

	if (strcmp(Hint, "serendipity") == 0 || strcmp(Hint, "fastest") == 0)
	{
		Env->VertexOrderHint = VertexSequence::Serendipity;
	}
	else if (strcmp(Hint, "shuffle") == 0 || strcmp(Hint, "diffuse") == 0)
	{
		Env->VertexOrderHint = VertexSequence::Shuffle;
	}

	return 0;
}


int LuaSetInternalExportGrid(lua_State* L)
{
	double Multiplier = (double)luaL_checknumber(L, 1);
	const char* UnitString = luaL_checklstring(L, 2, nullptr);

	bool Success = false;
	if (UnitString != nullptr)
	{
		Success = ExportGrid::SetInternalScale(Multiplier, UnitString);
	}

	return 0;
}


int LuaSetExternalExportGrid(lua_State* L)
{
	double Multiplier = (double)luaL_checknumber(L, 1);
	const char* UnitString = luaL_checklstring(L, 2, nullptr);

	bool Success = false;
	if (UnitString != nullptr)
	{
		Success = ExportGrid::SetExternalScale(Multiplier, UnitString);
	}

	return 0;
}


const luaL_Reg LuaEnvReg[] = \
{
	{ "set_title", LuaEnvironment::LuaSetTitle },
	{ "show_debug_menu", LuaEnvironment::LuaShowDebugMenu },
	{ "hide_debug_menu", LuaEnvironment::LuaHideDebugMenu },
	{ "set_advance_event", LuaEnvironment::LuaSetAdvanceEvent },
	{ "set_joystick_connect_event", LuaEnvironment::LuaSetJoystickConnectEvent },
	{ "set_joystick_disconnect_event", LuaEnvironment::LuaSetJoystickDisconnectEvent },
	{ "set_joystick_axis_event", LuaEnvironment::LuaSetJoystickAxisEvent },
	{ "set_joystick_button_event", LuaEnvironment::LuaSetJoystickButtonEvent },
	{ "push_meshing_density", LuaEnvironment::LuaPushMeshingDensity },
	{ "set_convergence_hint", LuaEnvironment::LuaSetConvergenceHint },
	{ "set_internal_export_grid", LuaSetInternalExportGrid },
	{ "set_external_export_grid", LuaSetExternalExportGrid },
	{ NULL, NULL }
};


int LuaOpenEnv(lua_State* L)
{
	luaL_newlib(L, LuaEnvReg);
	return 1;
}


LuaEnvironment::LuaEnvironment()
{
	GlobalPaintingSet = PaintingSet::Create();
	PaintingSets.push_back(GlobalPaintingSet);

	GlobalModel.reset();

	AdvanceCallbackRef = LUA_REFNIL;
	JoystickConnectCallbackRef = LUA_REFNIL;
	JoystickDisconnectCallbackRef = LUA_REFNIL;
	JoystickAxisCallbackRef = LUA_REFNIL;
	JoystickButtonCallbackRef = LUA_REFNIL;
	L = luaL_newstate();
	luaL_openlibs(L);

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_REGISTRYINDEX, "tangerine.env");

	luaL_requiref(L, "tangerine_sdf", LuaOpenSDF, 1);
	luaL_requiref(L, "tangerine_env", LuaOpenEnv, 1);
	luaL_requiref(L, "tangerine_mat", LuaOpenMaterial, 1);
	luaL_requiref(L, "more_math", LuaOpenVec, 1);
	luaL_requiref(L, "tangerine_color", LuaOpenColor, 1);
	luaL_requiref(L, "tangerine_light", LuaOpenLight, 1);
	luaL_requiref(L, "tangerine_painting_set", LuaOpenPaintingSet, 1);

	const char* Source = \
		"tangerine = {}\n"
		"for key, value in next, tangerine_sdf do\n"
		"	_ENV[key] = tangerine_sdf[key]\n"
		"	tangerine[key] = tangerine_sdf[key]\n"
		"end\n"
		"for key, value in next, tangerine_env do\n"
		"	_ENV[key] = tangerine_env[key]\n"
		"	tangerine[key] = tangerine_env[key]\n"
		"end\n"
		"for key, value in next, tangerine_mat do\n"
		"	_ENV[key] = tangerine_mat[key]\n"
		"	tangerine[key] = tangerine_mat[key]\n"
		"end\n"
		"for key, value in next, more_math do\n"
		"	_ENV[key] = more_math[key]\n"
		"end\n"
		"for key, value in next, tangerine_color do\n"
		"	_ENV[key] = tangerine_color[key]\n"
		"	tangerine[key] = tangerine_color[key]\n"
		"end\n"
		"for key, value in next, tangerine_light do\n"
		"	_ENV[key] = tangerine_light[key]\n"
		"	tangerine[key] = tangerine_light[key]\n"
		"end\n"
		"for key, value in next, tangerine_painting_set do\n"
		"	_ENV[key] = tangerine_painting_set[key]\n"
		"	tangerine[key] = tangerine_painting_set[key]\n"
		"end\n"
		"tangerine_sdf = nil\n"
		"tangerine_env = nil\n"
		"tangerine_mat = nil\n"
		"tangerine_color = nil\n"
		"tangerine_light = nil\n"
		"tangerine_painting_set = nil\n";

	int Error = luaL_dostring(L, Source);
	Assert(Error == 0);
}


LuaEnvironment::~LuaEnvironment()
{
	PaintingSets.clear();
	lua_gc(L, LUA_GCCOLLECT);
	lua_close(L);
}


void LuaEnvironment::MaybeRunGarbageCollection()
{
	if (GarbageCollectionRequested)
	{
		GarbageCollectionRequested = false;
		lua_gc(L, LUA_GCCOLLECT);
	}
}


void LuaEnvironment::Advance(double DeltaTimeMs, double ElapsedTimeMs)
{
	if (AdvanceCallbackRef != LUA_REFNIL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, AdvanceCallbackRef);
		lua_pushnumber(L, DeltaTimeMs);
		lua_pushnumber(L, ElapsedTimeMs);
		int Error = lua_pcall(L, 2, 0, 0);
		if (!HandleError(Error))
		{
			luaL_unref(L, LUA_REGISTRYINDEX, AdvanceCallbackRef);
			AdvanceCallbackRef = LUA_REFNIL;
		}
		else
		{
			MaybeRunGarbageCollection();
		}
	}
}


void LuaEnvironment::JoystickConnect(const JoystickInfo& Joystick)
{
	if (JoystickConnectCallbackRef != LUA_REFNIL)
	{
		static_assert(sizeof(lua_Integer) >= sizeof(SDL_JoystickID));

		lua_rawgeti(L, LUA_REGISTRYINDEX, JoystickConnectCallbackRef);
		lua_pushinteger(L, Joystick.InstanceID);
		lua_pushstring(L, Joystick.Name.c_str());

		int Error = lua_pcall(L, 2, 0, 0);
		if (!HandleError(Error))
		{
			luaL_unref(L, LUA_REGISTRYINDEX, JoystickConnectCallbackRef);
			JoystickConnectCallbackRef = LUA_REFNIL;
		}
		else
		{
			MaybeRunGarbageCollection();
		}
	}
}


void LuaEnvironment::JoystickDisconnect(const struct JoystickInfo& Joystick)
{
	if (JoystickDisconnectCallbackRef != LUA_REFNIL)
	{
		static_assert(sizeof(lua_Integer) >= sizeof(SDL_JoystickID));

		lua_rawgeti(L, LUA_REGISTRYINDEX, JoystickDisconnectCallbackRef);
		lua_pushinteger(L, Joystick.InstanceID);
		lua_pushstring(L, Joystick.Name.c_str());

		int Error = lua_pcall(L, 2, 0, 0);
		if (!HandleError(Error))
		{
			luaL_unref(L, LUA_REGISTRYINDEX, JoystickDisconnectCallbackRef);
			JoystickDisconnectCallbackRef = LUA_REFNIL;
		}
		else
		{
			MaybeRunGarbageCollection();
		}
	}
}


void LuaEnvironment::JoystickAxis(SDL_JoystickID JoystickID, int Axis, float Value)
{
	if (JoystickAxisCallbackRef != LUA_REFNIL)
	{
		static_assert(sizeof(lua_Integer) >= sizeof(SDL_JoystickID));

		lua_rawgeti(L, LUA_REGISTRYINDEX, JoystickAxisCallbackRef);
		lua_pushinteger(L, JoystickID);
		lua_pushinteger(L, Axis);
		lua_pushnumber(L, Value);

		int Error = lua_pcall(L, 3, 0, 0);
		if (!HandleError(Error))
		{
			luaL_unref(L, LUA_REGISTRYINDEX, JoystickAxisCallbackRef);
			JoystickAxisCallbackRef = LUA_REFNIL;
		}
		else
		{
			MaybeRunGarbageCollection();
		}
	}
}


void LuaEnvironment::JoystickButton(SDL_JoystickID JoystickID, int Button, bool Pressed)
{
	if (JoystickButtonCallbackRef != LUA_REFNIL)
	{
		static_assert(sizeof(lua_Integer) >= sizeof(SDL_JoystickID));

		lua_rawgeti(L, LUA_REGISTRYINDEX, JoystickButtonCallbackRef);
		lua_pushinteger(L, JoystickID);
		lua_pushinteger(L, Button);
		lua_pushboolean(L, Pressed);

		int Error = lua_pcall(L, 3, 0, 0);
		if (!HandleError(Error))
		{
			luaL_unref(L, LUA_REGISTRYINDEX, JoystickButtonCallbackRef);
			JoystickButtonCallbackRef = LUA_REFNIL;
		}
		else
		{
			MaybeRunGarbageCollection();
		}
	}
}


bool LuaEnvironment::HandleError(int Error)
{
	if (Error != LUA_OK)
	{
		std::string ErrorMessage = fmt::format("{}\n", lua_tostring(L, -1));
		PostScriptError(ErrorMessage);
		lua_pop(L, 1);
		return false;
	}
	else
	{
		return true;
	}
}


void LuaEnvironment::LoadLuaModelCommon()
{
	MaybeRunGarbageCollection();
	lua_getglobal(L, "model");
	void* LuaData = luaL_testudata(L, -1, "tangerine.sdf");
	if (LuaData)
	{
		SDFNodeShared& Evaluator = *static_cast<SDFNodeShared*>(LuaData);
		GlobalModel = SDFModel::Create(GlobalPaintingSet, Evaluator, Name, .25, MeshingDensityPush);
	}
	lua_pop(L, 1);

	if (GlobalModel)
	{
		SetTreeEvaluator(GlobalModel->Evaluator);
	}
	else
	{
		std::function<bool(SDFModelShared)> QueryThunk = [&](SDFModelShared Model)
		{
			SetTreeEvaluator(Model->Evaluator);
			return true;
		};
		SDFModelShared Found = PaintingSet::GlobalSelect(QueryThunk);
	}
}

void LuaEnvironment::LoadFromPath(std::string Path)
{
	auto LoadAndProcess = [&]()
	{
		std::filesystem::path FilePath(Path);
		std::filesystem::path DirPath = FilePath.parent_path();
		Name = FilePath.filename().string();
		DirPath /= "?.lua";

		lua_getglobal(L, "package");
		lua_getfield(L, -1, "path");
		std::string SearchPath = lua_tostring(L, -1);

		SearchPath = fmt::format("{};{}", SearchPath, DirPath.string());
		lua_pushstring(L, SearchPath.c_str());
		lua_setfield(L, -3, "path");
		lua_pop(L, 2);

		int Error = luaL_dofile(L, Path.c_str());
		if (HandleError(Error))
		{
			LoadLuaModelCommon();
		}
	};
	LoadModelCommon(LoadAndProcess);
}


void LuaEnvironment::LoadFromString(std::string Source)
{
	auto LoadAndProcess = [&]()
	{
		int Error = luaL_dostring(L, Source.c_str());
		if (HandleError(Error))
		{
			LoadLuaModelCommon();
		}
	};
	LoadModelCommon(LoadAndProcess);
}

#endif //EMBED_LUA
