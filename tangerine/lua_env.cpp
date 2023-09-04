
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

#include "tangerine.h"
#include "shape_compiler.h"
#include "sodapop.h"
#include "units.h"
#include "lua_material.h"
#include "lua_sdf.h"
#include "lua_vec.h"
#include <fmt/format.h>
#include <filesystem>


LuaEnvironment* LuaEnvironment::GetScriptEnvironment(struct lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "tangerine.env");
	LuaEnvironment* Env = (LuaEnvironment*)lua_touserdata(L, 2);
	lua_pop(L, 1);
	return Env;
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


int LuaEnvironment::LuaPushMeshingDensity(lua_State* L)
{
#if RENDERER_SODAPOP
	LuaEnvironment* Env = GetScriptEnvironment(L);
	Env->MeshingDensityPush = luaL_checknumber(L, 1);
#endif
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
	{ "set_advance_event", LuaEnvironment::LuaSetAdvanceEvent },
	{ "push_meshing_density", LuaEnvironment::LuaPushMeshingDensity },
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
	GlobalModel.reset();

	AdvanceCallbackRef = LUA_REFNIL;
	L = luaL_newstate();
	luaL_openlibs(L);

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_REGISTRYINDEX, "tangerine.env");

	luaL_requiref(L, "tangerine_sdf", LuaOpenSDF, 1);
	luaL_requiref(L, "tangerine_env", LuaOpenEnv, 1);
	luaL_requiref(L, "tangerine_mat", LuaOpenMaterial, 1);
	luaL_requiref(L, "more_math", LuaOpenVec, 1);

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
		"tangerine_sdf = nil\n"
		"tangerine_env = nil\n"
		"tangerine_mat = nil\n";

	int Error = luaL_dostring(L, Source);
	Assert(Error == 0);
}


LuaEnvironment::~LuaEnvironment()
{
	lua_close(L);
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
	lua_getglobal(L, "model");
	void* LuaData = luaL_testudata(L, -1, "tangerine.sdf");
	if (LuaData)
	{
		SDFNodeShared& Evaluator = *static_cast<SDFNodeShared*>(LuaData);
		GlobalModel = SDFModel::Create(Evaluator, Name, .25, MeshingDensityPush);
	}
	lua_pop(L, 1);

	if (GlobalModel)
	{
		SetTreeEvaluator(GlobalModel->Evaluator);
	}
	else
	{
		std::vector<SDFModelWeakRef> LiveModels = GetLiveModels();
		for (SDFModelWeakRef& WeakRef : LiveModels)
		{
			SDFModelShared LiveModel = WeakRef.lock();
			if (LiveModel)
			{
				SetTreeEvaluator(LiveModel->Evaluator);
				break;
			}
		}
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
