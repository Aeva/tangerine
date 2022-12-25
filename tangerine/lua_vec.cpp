
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

#include "lua_vec.h"

#if EMBED_LUA
#include <array>
#include <string>
#include <fmt/format.h>

using namespace glm;


LuaVec* GetLuaVec(lua_State* L, int Arg)
{
	LuaVec* Vec = (LuaVec*)luaL_checkudata(L, Arg, "more_math.vec");
	return Vec;
}


int CreateVec(lua_State* L, int Size)
{
	LuaVec* NewVec = (LuaVec*)lua_newuserdata(L, sizeof(LuaVec));
	luaL_getmetatable(L, "more_math.vec");
	lua_setmetatable(L, -2);
	int Cursor = 0;
	int Args = lua_gettop(L);

	NewVec->Size = Size;

	if (Args == 1 && lua_isnumber(L, 1))
	{
		float Fill = lua_tonumber(L, 1);
		for (; Cursor < Size; ++Cursor)
		{
			NewVec->Vector[Cursor] = Fill;
		}
	}
	else
	{
		for (int Arg = 1; Arg < Args; ++Arg)
		{
			if (lua_isnumber(L, 1))
			{
				NewVec->Vector[Cursor++] = lua_tonumber(L, Arg);
				continue;
			}
			else
			{
				lua_pushstring(L, "TODO: Non-integer construction args\n");
				lua_error(L);
				break;
			}
		}
	}

	// Zero out the remainder of the vector.
	for (; Cursor < 4; ++Cursor)
	{
		NewVec->Vector[Cursor] = 0.0;
	}
	return 1;
}


template<int Size>
int CreateVec(lua_State* L)
{
	return CreateVec(L, Size);
}


const luaL_Reg LuaVecType[] = \
{
	{ "vec2", CreateVec<2> },
	{ "vec3", CreateVec<3> },
	{ "vec4", CreateVec<4> },

	{ NULL, NULL }
};


bool ReadSwizzle(const char* Key, int& Lanes, std::array<int, 4>& Swizzle)
{
	Lanes = strlen(Key);
	Swizzle = { 0, 0, 0, 0 };
	if (Lanes >= 1 && Lanes <= 4)
	{
		for (int Lane = 0; Lane < Lanes; ++Lane)
		{
			switch (Key[Lane])
			{
			case 'r':
			case 'x':
				Swizzle[Lane] = 0;
				break;
			case 'g':
			case 'y':
				Swizzle[Lane] = 1;
				break;
			case 'b':
			case 'z':
				Swizzle[Lane] = 2;
				break;
			case 'a':
			case 'w':
				Swizzle[Lane] = 3;
				break;
			default:
				return false;
			}
		}
		return true;
	}
	return false;
};


int IndexVec(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);

	if (lua_isinteger(L, 2))
	{
		int Lane = lua_tointeger(L, 2) - 1;
		if (Lane >= 0 && Lane < Self->Size)
		{
			lua_pushnumber(L, Self->Vector[Lane]);
			return 1;
		}
	}
	else
	{
		const char* Key = luaL_checklstring(L, 2, nullptr);
		std::array<int, 4> Swizzle;
		int Lanes;

		if (ReadSwizzle(Key, Lanes, Swizzle))
		{
			if (Lanes == 1)
			{
				lua_pushnumber(L, Self->Vector[Swizzle[0]]);
			}
			else
			{
				LuaVec* NewVec = (LuaVec*)lua_newuserdata(L, sizeof(LuaVec));
				luaL_getmetatable(L, "more_math.vec");
				lua_setmetatable(L, -2);
				NewVec->Size = Lanes;
				int Cursor = 0;
				for (; Cursor < Lanes; ++Cursor)
				{
					NewVec->Vector[Cursor] = Self->Vector[Swizzle[Cursor]];
				}
				for (; Cursor < 4; ++Cursor)
				{
					NewVec->Vector[Cursor] = 0.0;
				}
			}
			return 1;
		}
		else
		{
			for (const luaL_Reg& Reg : LuaVecType)
			{
				if (Reg.name && strcmp(Reg.name, Key) == 0)
				{
					lua_pushcfunction(L, Reg.func);
					return 1;
				}
			}
		}
	}

	lua_pushnil(L);
	return 1;
}


int NewIndexVec(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);

	if (lua_isinteger(L, 2))
	{
		int Lane = lua_tointeger(L, 2) - 1;
		if (Lane >= 0 && Lane < Self->Size)
		{
			float Value = luaL_checknumber(L, 3);
			Self->Vector[Lane] = Value;
			lua_pushnumber(L, Value);
			return 1;
		}
	}
	else
	{
		const char* Key = luaL_checklstring(L, 2, nullptr);
		std::array<int, 4> Swizzle;
		int Lanes;

		if (ReadSwizzle(Key, Lanes, Swizzle))
		{
			if (Lanes == 1)
			{
				float Value = luaL_checknumber(L, 3);
				Self->Vector[Swizzle[0]] = Value;
				lua_pushnumber(L, Value);
				return 1;
			}
			else
			{
				for (int Cursor = 0; Cursor < Lanes; ++Cursor)
				{
					lua_geti(L, 3, Cursor + 1);
					Self->Vector[Swizzle[Cursor]] = lua_tonumber(L, 4);
					lua_pop(L, 1);
				}
				lua_getfield(L, 1, Key);
				return 1;
			}
		}
	}

	lua_pushstring(L, "Attempted to set an invalid vector index.\n");
	lua_error(L);

	lua_pushnil(L);
	return 1;
}


int VecToString(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);
	std::string Repr;

	switch (Self->Size)
	{
	case 2:
		Repr = fmt::format("vec2({}, {})", Self->Vector[0], Self->Vector[1]);
		break;
	case 3:
		Repr = fmt::format("vec3({}, {}, {})", Self->Vector[0], Self->Vector[1], Self->Vector[2]);
		break;
	case 4:
		Repr = fmt::format("vec4({}, {}, {}, {})", Self->Vector[0], Self->Vector[1], Self->Vector[2], Self->Vector[3]);
		break;
	default:
		Repr = "{ Invalid Vector?! }";
	}

	lua_pushstring(L, Repr.c_str());
	return 1;
}


const luaL_Reg LuaVecMeta[] = \
{
	{ "__index", IndexVec },
	{ "__newindex", NewIndexVec },
	{ "__tostring", VecToString },

	{ NULL, NULL }
};


int LuaOpenVec(lua_State* L)
{
	luaL_newmetatable(L, "more_math.vec");
	luaL_setfuncs(L, LuaVecMeta, 0);
	luaL_newlib(L, LuaVecType);
	return 1;
}


#endif // EMBED_LUA
