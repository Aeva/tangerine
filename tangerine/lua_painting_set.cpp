
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

#include "lua_painting_set.h"
#if EMBED_LUA
#include "lua_env.h"
#include "lua_sdf.h"


PaintingSetShared* CheckLuaPaintingSet(lua_State* L, int Arg)
{
	return (PaintingSetShared*)luaL_checkudata(L, Arg, "tangerine.painting_set");
}


PaintingSetShared* TestLuaPaintingSet(lua_State* L, int Arg)
{
	return (PaintingSetShared*)luaL_testudata(L, Arg, "tangerine.painting_set");
}


static int WrapPaintingSet(lua_State* L, PaintingSetShared& NewPaintingSet)
{
	PaintingSetShared* Wrapper = (PaintingSetShared*)lua_newuserdata(L, sizeof(PaintingSetShared));
	luaL_getmetatable(L, "tangerine.painting_set");
	lua_setmetatable(L, -2);
	new (Wrapper) PaintingSetShared(NewPaintingSet);
	return 1;
}


static int LuaPaintingSetInstanceModel(lua_State* L)
{
	PaintingSetShared& Self = *CheckLuaPaintingSet(L, 1);
	SDFNodeShared& Evaluator = *GetSDFNode(L, 2);

	std::string Name = "";
	if (lua_gettop(L) == 3)
	{
		const char* NameString = luaL_checklstring(L, 3, nullptr);
		Name = std::string(NameString);
		lua_pop(L, 1);
	}
	else
	{
		LuaEnvironment* Env = LuaEnvironment::GetScriptEnvironment(L);
		if (Env->Name.size() > 0)
		{
			Name = Env->Name;
		}
	}

	LuaModelShared NewModel = LuaModel::Create(L, Self, Evaluator, Name, 0.25);

	LuaModelShared* Wrapper = (LuaModelShared*)lua_newuserdata(L, sizeof(LuaModelShared));
	luaL_getmetatable(L, "tangerine.model");
	lua_setmetatable(L, -2);
	new (Wrapper) LuaModelShared(NewModel);
	return 1;
}


static const luaL_Reg LuaPaintingSetType[] = \
{
	{ "instance", LuaPaintingSetInstanceModel },

	{ NULL, NULL }
};


static int LuaPaintingSetIndex(lua_State* L)
{
	PaintingSetShared& Self = *CheckLuaPaintingSet(L, 1);
	const char* Key = luaL_checklstring(L, 2, nullptr);

	for (const luaL_Reg& Reg : LuaPaintingSetType)
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


static int LuaPaintingSetGC(lua_State* L)
{
	PaintingSetShared& Self = *CheckLuaPaintingSet(L, 1);
	{
		LuaEnvironment* Env = LuaEnvironment::GetScriptEnvironment(L);
		Assert(Env != nullptr);

		for (auto Iterator = Env->PaintingSets.begin(); Iterator != Env->PaintingSets.end(); ++Iterator)
		{
			if (*Iterator == Self)
			{
				Env->PaintingSets.erase(Iterator);
				break;
			}
		}
	}
	Self.reset();
	return 0;
}


const luaL_Reg LuaPaintingSetMeta[] = \
{
	{ "__index", LuaPaintingSetIndex },
	{ "__gc", LuaPaintingSetGC },
	{ NULL, NULL }
};


static int LuaPaintingSet(lua_State* L)
{
	LuaEnvironment* Env = LuaEnvironment::GetScriptEnvironment(L);
	Assert(Env != nullptr);

	PaintingSetShared NewPaintingSet = PaintingSet::Create();
	Env->PaintingSets.push_back(NewPaintingSet);

	return WrapPaintingSet(L, NewPaintingSet);
}


const luaL_Reg LuaPaintingSetReg[] = \
{
	{ "painting_set", LuaPaintingSet },

	{ NULL, NULL }
};


int LuaOpenPaintingSet(struct lua_State* L)
{
	luaL_newmetatable(L, "tangerine.painting_set");
	luaL_setfuncs(L, LuaPaintingSetMeta, 0);
	luaL_newlib(L, LuaPaintingSetReg);
	return 1;
}


#endif // EMBED_LUA
