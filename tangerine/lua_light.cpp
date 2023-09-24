
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

#include "lua_light.h"

#if EMBED_LUA
#include "lua_vec.h"


LightShared* CheckLuaLight(lua_State* L, int Arg)
{
	return (LightShared*)luaL_checkudata(L, Arg, "tangerine.light");
}


LightShared* TestLuaLight(lua_State* L, int Arg)
{
	return (LightShared*)luaL_testudata(L, Arg, "tangerine.light");
}


static int WrapLight(lua_State* L, LightShared& NewLight)
{
	LightShared* Wrapper = (LightShared*)lua_newuserdata(L, sizeof(LightShared));
	luaL_getmetatable(L, "tangerine.light");
	lua_setmetatable(L, -2);
	new (Wrapper) LightShared(NewLight);
	return 1;
}


const luaL_Reg LuaLightType[] = \
{
	{ NULL, NULL }
};


static int IndexLight(lua_State* L)
{
	LightShared& Light = *CheckLuaLight(L, 1);
	const char* Key = luaL_checklstring(L, 2, nullptr);

	for (const luaL_Reg& Reg : LuaLightType)
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


static int LightGC(lua_State* L)
{
	LightShared& Light = *CheckLuaLight(L, 1);
	Light.reset();
	return 0;
}


const luaL_Reg LuaLightMeta[] = \
{
	{ "__index", IndexLight },
	{ "__gc", LightGC },
	{ NULL, NULL }
};


static int CreateLuaPointLight(lua_State* L)
{
	int NextArg = 1;
	glm::vec3 Point = GetVec3(L, NextArg);

	LightShared NewLight = PointLight::Create(Point);
	return WrapLight(L, NewLight);
}


const luaL_Reg LuaLightReg[] = \
{
	{ "point_light", CreateLuaPointLight },
	{ NULL, NULL }
};


int LuaOpenLight(struct lua_State* L)
{
	luaL_newmetatable(L, "tangerine.light");
	luaL_setfuncs(L, LuaLightMeta, 0);
	luaL_newlib(L, LuaLightReg);
	return 1;
}


#endif
