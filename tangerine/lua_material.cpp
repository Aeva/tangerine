
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

#include "lua_material.h"
#if EMBED_LUA
#include "lua_vec.h"
#include "material.h"
#include "colors.h"
#include <string>


MaterialShared* CheckLuaMaterial(lua_State* L, int Arg)
{
	return (MaterialShared*)luaL_checkudata(L, Arg, "tangerine.material");
}


MaterialShared* TestLuaMaterial(lua_State* L, int Arg)
{
	return (MaterialShared*)luaL_testudata(L, Arg, "tangerine.material");
}


int WrapMaterial(lua_State* L, MaterialShared& NewMaterial)
{
	MaterialShared* Wrapper = (MaterialShared*)lua_newuserdata(L, sizeof(MaterialShared));
	luaL_getmetatable(L, "tangerine.material");
	lua_setmetatable(L, -2);
	new (Wrapper) MaterialShared(NewMaterial);
	return 1;
}


const luaL_Reg LuaMaterialType[] = \
{
	{ NULL, NULL }
};


int IndexMaterial(lua_State* L)
{
	MaterialShared& Self = *CheckLuaMaterial(L, 1);
	const char* Key = luaL_checklstring(L, 2, nullptr);

	for (const luaL_Reg& Reg : LuaMaterialType)
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


int MaterialGC(lua_State* L)
{
	MaterialShared& Material = *CheckLuaMaterial(L, 1);
	Material.reset();
	return 0;
}


const luaL_Reg LuaMaterialMeta[] = \
{
	{ "__index", IndexMaterial },
	{ "__gc", MaterialGC },
	{ NULL, NULL }
};


int LuaMaterialSolidColor(lua_State* L)
{
	glm::vec3 BaseColor;
	if (!lua_isnumber(L, 1) && lua_isstring(L, 1))
	{
		const char* ColorString = luaL_checklstring(L, 1, nullptr);
		StatusCode Result = ParseColor(ColorString, BaseColor);
	}
	else
	{
		int NextArg = 1;
		BaseColor = GetVec3(L, NextArg);
	}

	MaterialShared Material = MaterialShared(new MaterialSolidColor(BaseColor));
	return WrapMaterial(L, Material);
}


int LuaMaterialPBRBR(lua_State* L)
{
	glm::vec3 BaseColor;
	if (!lua_isnumber(L, 1) && lua_isstring(L, 1))
	{
		const char* ColorString = luaL_checklstring(L, 1, nullptr);
		StatusCode Result = ParseColor(ColorString, BaseColor);
	}
	else
	{
		int NextArg = 1;
		BaseColor = GetVec3(L, NextArg);
	}

	MaterialShared Material = MaterialShared(new MaterialPBRBR(BaseColor));
	return WrapMaterial(L, Material);
}


int LuaMaterialDebugNormals(lua_State* L)
{
	MaterialShared Material = MaterialShared(new MaterialDebugNormals());
	return WrapMaterial(L, Material);
}


const luaL_Reg LuaMaterialReg[] = \
{
	{ "solid_material", LuaMaterialSolidColor },
	{ "pbrbr_material", LuaMaterialPBRBR },
	{ "normal_debug_material", LuaMaterialDebugNormals },

	{ NULL, NULL }
};


int LuaOpenMaterial(struct lua_State* L)
{
	luaL_newmetatable(L, "tangerine.material");
	luaL_setfuncs(L, LuaMaterialMeta, 0);
	luaL_newlib(L, LuaMaterialReg);
	return 1;
}

#endif // EMBED_LUA
