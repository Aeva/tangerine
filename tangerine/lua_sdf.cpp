
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


#include <string.h>
#include <lua/lua.hpp>
#include "lua_sdf.h"
#include "sdfs.h"


SDFNode* GetSDFNode(lua_State* L, int Arg)
{
	SDFNode** Node = (SDFNode**)luaL_checkudata(L, Arg, "tangerine.sdf");
	return *Node;
}


SDFNode** NewSDFNode(lua_State* L)
{
	void* NewNode = lua_newuserdata(L, sizeof(SDFNode*));
	luaL_getmetatable(L, "tangerine.sdf");
	lua_setmetatable(L, -2);
	return (SDFNode**)NewNode;
}


int LuaMoveInner(lua_State* L, float X, float Y, float Z)
{
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode** NewNode = NewSDFNode(L);
	*NewNode = Node->Copy();
	(*NewNode)->Move(glm::vec3(X, Y, Z));
	return 1;
}


int LuaMoveX(lua_State* L)
{
	float X = (float)luaL_checknumber(L, 2);
	return LuaMoveInner(L, X, 0, 0);
}


int LuaMoveY(lua_State* L)
{
	float Y = (float)luaL_checknumber(L, 2);
	return LuaMoveInner(L, 0, Y, 0);
}


int LuaMoveZ(lua_State* L)
{
	float Z = (float)luaL_checknumber(L, 2);
	return LuaMoveInner(L, 0, 0, Z);
}


int LuaMove(lua_State* L)
{
	float X = (float)luaL_checknumber(L, 2);
	float Y = (float)luaL_checknumber(L, 3);
	float Z = (float)luaL_checknumber(L, 4);
	return LuaMoveInner(L, X, Y, Z);
}


int LuaSphere(lua_State* L)
{
	lua_Number Radius = luaL_checknumber(L, 1);
	SDFNode** NewNode = NewSDFNode(L);
	*NewNode = SDF::Sphere(Radius);
	return 1;
}


const luaL_Reg LuaSDFType[] = \
{
	{ "sphere", LuaSphere },
	{ "move", LuaMove },
	{ "move_x", LuaMoveX },
	{ "move_y", LuaMoveY },
	{ "move_z", LuaMoveZ },
	{ NULL, NULL }
};


int IndexSDFNode(lua_State* L)
{
	SDFNode** Self = (SDFNode**)luaL_checkudata(L, 1, "tangerine.sdf");
	const char* Key = luaL_checklstring(L, 2, nullptr);

	for (const luaL_Reg& Reg : LuaSDFType)
	{
		if (Reg.name && strcmp(Reg.name, Key) == 0)
		{
			lua_pushcfunction(L, Reg.func);
			break;
		}
		else if (!Reg.name)
		{
			lua_pushnil(L);
		}
	}

	return 1;
}


int SDFNodeGC(lua_State* L)
{
	SDFNode** Self = (SDFNode**)luaL_checkudata(L, 1, "tangerine.sdf");
	if (Self)
	{
		delete* Self;
	}
	return 0;
}


const luaL_Reg LuaSDFMeta[] = \
{
	{ "__index", IndexSDFNode },
	{ "__gc", SDFNodeGC },
	{ NULL, NULL }
};


int LuaOpenSDF(lua_State* L)
{
	luaL_newmetatable(L, "tangerine.sdf");
	luaL_setfuncs(L, LuaSDFMeta, 0);
	luaL_newlib(L, LuaSDFType);
	return 1;
}
