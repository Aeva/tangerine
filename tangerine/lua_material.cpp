
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
#include <vector>


SDFNodeShared* GetSDFNode(lua_State* L, int Arg);


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


template<typename MaterialT>
int SetBaseColor(lua_State* L)
{
	MaterialShared& GenericSelf = *CheckLuaMaterial(L, 1);
	std::shared_ptr<MaterialT> Self = std::dynamic_pointer_cast<MaterialT>(GenericSelf);
	if (Self)
	{
		ColorPoint BaseColor;
		if (!lua_isnumber(L, 1) && lua_isstring(L, 2))
		{
			const char* ColorString = luaL_checklstring(L, 2, nullptr);
			StatusCode Result = ParseColor(ColorString, BaseColor);
		}
		else
		{
			int NextArg = 2;
			BaseColor = ColorPoint(GetVec3(L, NextArg));
		}

		Self->BaseColor = BaseColor;
	}

	{
		// Pop everything but the first arg off the stack, so that the material is returned.
		lua_pop(L, lua_gettop(L) - 1);
		return 1;
	}
}


std::vector<std::vector<luaL_Reg>> GenerateMaterialIndexIndex()
{
	std::vector<std::vector<luaL_Reg>> IndexIndex;
	IndexIndex.resize(size_t(MaterialType::Count));

	{
		std::vector<luaL_Reg>& Index = IndexIndex[size_t(MaterialType::SolidColor)];
		Index.push_back({ "set_color", SetBaseColor<MaterialSolidColor> });
	}

	{
		std::vector<luaL_Reg>& Index = IndexIndex[size_t(MaterialType::PBRBR)];
		Index.push_back({ "set_color", SetBaseColor<MaterialPBRBR> });
	}

	for (std::vector<luaL_Reg>& Index : IndexIndex)
	{
		Index.push_back({ NULL, NULL });
	}
	return IndexIndex;
}
const std::vector<std::vector<luaL_Reg>> LuaMaterialTypes = GenerateMaterialIndexIndex();


int IndexMaterial(lua_State* L)
{
	MaterialShared& Self = *CheckLuaMaterial(L, 1);
	const char* Key = luaL_checklstring(L, 2, nullptr);

	const std::vector<luaL_Reg>& LuaMaterialType = LuaMaterialTypes[size_t(Self->Type)];

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
	ColorPoint BaseColor;
	if (!lua_isnumber(L, 1) && lua_isstring(L, 1))
	{
		const char* ColorString = luaL_checklstring(L, 1, nullptr);
		StatusCode Result = ParseColor(ColorString, BaseColor);
	}
	else
	{
		int NextArg = 1;
		BaseColor = ColorPoint(GetVec3(L, NextArg));
	}

	MaterialShared Material = MaterialShared(new MaterialSolidColor(BaseColor));
	return WrapMaterial(L, Material);
}


int LuaMaterialPBRBR(lua_State* L)
{
	ColorPoint BaseColor;
	if (!lua_isnumber(L, 1) && lua_isstring(L, 1))
	{
		const char* ColorString = luaL_checklstring(L, 1, nullptr);
		StatusCode Result = ParseColor(ColorString, BaseColor);
	}
	else
	{
		int NextArg = 1;
		BaseColor = ColorPoint(GetVec3(L, NextArg));
	}

	MaterialShared Material = MaterialShared(new MaterialPBRBR(BaseColor));
	return WrapMaterial(L, Material);
}


int LuaMaterialDebugNormals(lua_State* L)
{
	MaterialShared Material = MaterialShared(new MaterialDebugNormals());
	return WrapMaterial(L, Material);
}


int LuaMaterialDebugGradient(lua_State* L)
{
	SDFNodeShared& Node = *GetSDFNode(L, 1);

	float Interval = (float)luaL_checknumber(L, 2);

	int NextArg = 3;
	std::vector<ColorPoint> Stops;
	while (NextArg <= lua_gettop(L))
	{
		if (lua_isstring(L, NextArg))
		{
			const char* ColorString = luaL_checklstring(L, NextArg, nullptr);
			++NextArg;
			ColorPoint Color;

			StatusCode Result = ParseColor(ColorString, Color);
			Stops.push_back(Color);
		}
		else
		{
			ColorPoint Color = ColorPoint(GetVec3(L, NextArg));
			Stops.push_back(Color);
		}
	}

	MaterialShared Material = MaterialShared(new MaterialDebugGradient(Node, Interval, ColorRamp(Stops)));
	return WrapMaterial(L, Material);
}


const luaL_Reg LuaMaterialReg[] = \
{
	{ "solid_material", LuaMaterialSolidColor },
	{ "pbrbr_material", LuaMaterialPBRBR },
	{ "normal_debug_material", LuaMaterialDebugNormals },
	{ "gradient_debug_material", LuaMaterialDebugGradient },

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
