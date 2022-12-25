
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


LuaVec* CreateVec(lua_State* L, int Size)
{
	LuaVec* NewVec = (LuaVec*)lua_newuserdata(L, sizeof(LuaVec));
	luaL_getmetatable(L, "more_math.vec");
	lua_setmetatable(L, -2);
	NewVec->Size = Size;
	NewVec->Vector = { 0.0, 0.0, 0.0, 0.0 };
	return NewVec;
}


template<int Size>
int CreateVec(lua_State* L)
{
	LuaVec* NewVec = (LuaVec*)lua_newuserdata(L, sizeof(LuaVec));
	luaL_getmetatable(L, "more_math.vec");
	lua_setmetatable(L, -2);
	int Cursor = 0;
	int Args = lua_gettop(L) - 1;

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
		for (int Arg = 1; Arg <= Args; ++Arg)
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


int VecAdd(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	NewVec->Vector = LHS->Vector + RHS->Vector;
	return 1;
}


int VecSub(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	NewVec->Vector = LHS->Vector - RHS->Vector;
	return 1;
}


int VecMul(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	NewVec->Vector = LHS->Vector * RHS->Vector;
	return 1;
}


template<int Mode>
int VecDiv(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	if (Mode == 1)
	{
		// Regular division
		NewVec->Vector = LHS->Vector / RHS->Vector;
	}
	else if (Mode == 1)
	{
		// Floor division
		NewVec->Vector = floor(LHS->Vector / RHS->Vector);
	}
	else if (Mode == 3)
	{
		// Modulo https://www.lua.org/manual/5.4/manual.html#3.4.1
		NewVec->Vector = LHS->Vector - floor(LHS->Vector / RHS->Vector) * RHS->Vector;
	}
	for (int Inactive = NewVec->Size; Inactive < 4; ++Inactive)
	{
		// Zero out the dead lanes to ensure non-NaN and non-inf values.
		NewVec->Vector[Inactive] = 0.0;
	}
	return 1;
}


int VecPow(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	NewVec->Vector = pow(LHS->Vector, RHS->Vector);
	return 1;
}


int VecNegate(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);
	LuaVec* NewVec = CreateVec(L, Self->Size);
	NewVec->Vector = -(Self->Vector);
	return 1;
}


int VecSize(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);
	lua_pushinteger(L, Self->Size);
	return 1;
}


float VecDot(LuaVec* LHS, LuaVec* RHS)
{
	int Size = min(LHS->Size, RHS->Size);
	float Total = 0.0;
	for (int Lane = 0; Lane < Size; ++Lane)
	{
		Total += LHS->Vector[Lane] * RHS->Vector[Lane];
	}
	return Total;
}


int VecDot(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	lua_pushnumber(L, VecDot(LHS, RHS));
	return 1;
}


float VecLength(LuaVec* Vec)
{
	float LengthSquared = VecDot(Vec, Vec);
	if (isinf(LengthSquared))
	{
		return LengthSquared;
	}
	else
	{
		return sqrt(LengthSquared);
	}
}


int VecLength(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);
	lua_pushnumber(L, VecLength(Self));
	return 1;
}


int VecDistance(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	LuaVec Delta;
	Delta.Size = min(LHS->Size, RHS->Size);
	Delta.Vector = RHS->Vector - LHS->Vector;
	lua_pushnumber(L, VecLength(&Delta));
	return 1;
}


int VecNormalize(lua_State* L)
{
	LuaVec* Self = GetLuaVec(L, 1);

	float Length = VecLength(Self);
	LuaVec* Normal = CreateVec(L, Self->Size);
	Normal->Vector = Self->Vector / Length;

	for (int Lane = Normal->Size; Lane < 4; ++Lane)
	{
		Normal->Vector[Lane] = 0.0;
	}

	bool Abnormal = any(isinf(Normal->Vector)) || any(isnan(Normal->Vector));
	lua_pushboolean(L, Abnormal);

	return 2;
}


int VecCross(lua_State* L)
{
	LuaVec* LHS = GetLuaVec(L, 1);
	LuaVec* RHS = GetLuaVec(L, 2);
	int Size = min(LHS->Size, RHS->Size);
	if (Size != 3)
	{
		lua_pushstring(L, "Attempted to take the cross product of two vectors that aren't both size 3.");
		lua_error(L);
		lua_pushnil(L);
		return 1;
	}
	LuaVec* NewVec = CreateVec(L, min(LHS->Size, RHS->Size));
	NewVec->Size = 3;
	NewVec->Vector = vec4(cross(vec3(LHS->Vector.xyz), vec3(RHS->Vector.xyz)), 0.0);
	return 1;
}


int VecLerp(lua_State* L)
{
	if (lua_isnumber(L, 1) && lua_isnumber(L, 2))
	{
		float LHS = lua_tonumber(L, 1);
		float RHS = lua_tonumber(L, 2);
		float Alpha = luaL_checknumber(L, 3);
		lua_pushnumber(L, mix(LHS, RHS, Alpha));
		return 1;
	}
	else
	{
		int Size;
		glm::vec4 Vec1;
		glm::vec4 Vec2;
		glm::vec4 Alpha;
		if (lua_isnumber(L, 1))
		{
			float LHS = lua_tonumber(L, 1);
			Vec1 = glm::vec4(LHS);

			LuaVec* RHS = GetLuaVec(L, 2);
			Size = RHS->Size;
			Vec2 = RHS->Vector;
		}
		else if (lua_isnumber(L, 2))
		{
			LuaVec* LHS = GetLuaVec(L, 1);
			Size = LHS->Size;
			Vec1 = LHS->Vector;

			float RHS = lua_tonumber(L, 2);
			Vec2 = glm::vec4(RHS);
		}
		else
		{
			LuaVec* LHS = GetLuaVec(L, 1);
			LuaVec* RHS = GetLuaVec(L, 2);
			Size = min(LHS->Size, RHS->Size);
			Vec1 = LHS->Vector;
			Vec2 = RHS->Vector;
		}
		if (lua_isnumber(L, 3))
		{
			float Other = lua_tonumber(L, 3);
			Alpha = glm::vec4(Other);
		}
		else
		{
			LuaVec* Other = GetLuaVec(L, 3);
			Size = min(Size, Other->Size);
			Alpha = Other->Vector;
		}
		LuaVec* NewVec = CreateVec(L, Size);
		NewVec->Vector = mix(Vec1, Vec2, Alpha);
		return 1;
	}
}


const luaL_Reg LuaVecType[] = \
{
	{ "vec2", CreateVec<2> },
	{ "vec3", CreateVec<3> },
	{ "vec4", CreateVec<4> },
	{ "dot", VecDot },
	{ "length", VecLength },
	{ "distance", VecDistance },
	{ "normalize", VecNormalize },
	{ "cross", VecCross },
	{ "lerp", VecLerp },
	{ "mix", VecLerp },

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

	{ "__add", VecAdd },
	{ "__sub", VecSub },
	{ "__mul", VecMul },
	{ "__div", VecDiv<1> },
	{ "__idiv", VecDiv<2> },
	{ "__mod", VecDiv<3> },
	{ "__pow", VecPow },
	{ "__unm", VecNegate },

	{ "__len", VecSize },

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
