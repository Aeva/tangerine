
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

#pragma once

#include "embedding.h"
#if EMBED_LUA
#include <lua/lua.hpp>
#include "glm_common.h"


struct LuaVec
{
	glm::vec4 Vector;
	int Size;
};


LuaVec* GetLuaVec(lua_State* L, int Arg);
glm::vec3 GetVec3(lua_State* L, int& NextArg);
LuaVec* CreateVec(lua_State* L, int Size);
LuaVec* CreateVec(lua_State* L, glm::vec2);
LuaVec* CreateVec(lua_State* L, glm::vec3);
LuaVec* CreateVec(lua_State* L, glm::vec4);

int LuaOpenVec(struct lua_State* L);


#endif // EMBED_LUA
