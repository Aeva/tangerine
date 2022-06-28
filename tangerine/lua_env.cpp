
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

lua_State* LuaStack = nullptr;


void CreateLuaEnv()
{
	LuaStack = luaL_newstate();
	luaL_openlibs(LuaStack);
	luaL_requiref(LuaStack, "tangerine", LuaOpenSDF, 1);

	const char* Source = \
		"for key, value in next, tangerine do\n"
		"	_ENV[key] = tangerine[key]\n"
		"end\n";

	int Error = luaL_dostring(LuaStack, Source);
	Assert(Error == 0);
}


void ResetLuaEnv()
{
	lua_close(LuaStack);
	CreateLuaEnv();
}


void LoadLuaModelCommon(int Error)
{
	if (Error)
	{
		std::string ErrorMessage = fmt::format("{}\n", lua_tostring(LuaStack, -1));
		PostScriptError(ErrorMessage);
		lua_pop(LuaStack, 1);
	}
	else
	{
		lua_getglobal(LuaStack, "model");
		void* LuaData = luaL_testudata(LuaStack, -1, "tangerine.sdf");
		if (LuaData)
		{
			SDFNode* Model = *(SDFNode**)LuaData;
			CompileEvaluator(Model);
		}
		else
		{
			PostScriptError("Invalid Model");
		}
		lua_pop(LuaStack, 1);
	}
}

void LoadLuaFromPath(std::string Path)
{
	auto LoadAndProcess = [&]()
	{
		ResetLuaEnv();
		int Error = luaL_dofile(LuaStack, Path.c_str());
		LoadLuaModelCommon(Error);
	};
	LoadModelCommon(LoadAndProcess);
}


void LoadLuaFromString(std::string Source)
{
	auto LoadAndProcess = [&]()
	{
		ResetLuaEnv();
		int Error = luaL_dostring(LuaStack, Source.c_str());
		LoadLuaModelCommon(Error);
	};
	LoadModelCommon(LoadAndProcess);
}


void LuaShutdown()
{
	lua_close(LuaStack);
}

#endif //EMBED_LUA
