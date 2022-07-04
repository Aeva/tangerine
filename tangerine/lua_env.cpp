
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

#include "lua_env.h"
#if EMBED_LUA

#include "tangerine.h"
#include "shape_compiler.h"
#include "lua_sdf.h"
#include <fmt/format.h>


LuaEnvironment::LuaEnvironment()
{
	L = luaL_newstate();
	luaL_openlibs(L);
	luaL_requiref(L, "tangerine", LuaOpenSDF, 1);

	const char* Source = \
		"for key, value in next, tangerine do\n"
		"	_ENV[key] = tangerine[key]\n"
		"end\n";

	int Error = luaL_dostring(L, Source);
	Assert(Error == 0);
}


LuaEnvironment::~LuaEnvironment()
{
	lua_close(L);
}


void LuaEnvironment::LoadLuaModelCommon(int Error)
{
	if (Error)
	{
		std::string ErrorMessage = fmt::format("{}\n", lua_tostring(L, -1));
		PostScriptError(ErrorMessage);
		lua_pop(L, 1);
	}
	else
	{
		lua_getglobal(L, "model");
		void* LuaData = luaL_testudata(L, -1, "tangerine.sdf");
		if (LuaData)
		{
			SDFNode* Model = *(SDFNode**)LuaData;
			CompileEvaluator(Model);
		}
		else
		{
			PostScriptError("Invalid Model");
		}
		lua_pop(L, 1);
	}
}

void LuaEnvironment::LoadFromPath(std::string Path)
{
	auto LoadAndProcess = [&]()
	{
		int Error = luaL_dofile(L, Path.c_str());
		LoadLuaModelCommon(Error);
	};
	LoadModelCommon(LoadAndProcess);
}


void LuaEnvironment::LoadFromString(std::string Source)
{
	auto LoadAndProcess = [&]()
	{
		int Error = luaL_dostring(L, Source.c_str());
		LoadLuaModelCommon(Error);
	};
	LoadModelCommon(LoadAndProcess);
}

#endif //EMBED_LUA
