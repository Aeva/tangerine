
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

#include "lua_color.h"

#if EMBED_LUA
#include <string>
#include <fmt/format.h>


ColorPoint* GetLuaColorPoint(lua_State* L, int Arg)
{
	return (ColorPoint*)luaL_checkudata(L, Arg, "tangerine.color_point");
}


ColorPoint* TestLuaColorPoint(lua_State* L, int Arg)
{
	return (ColorPoint*)luaL_testudata(L, Arg, "tangerine.color_point");
}


template<typename... ArgTypes>
static ColorPoint* CreateColorPoint(lua_State* L, ArgTypes... ConstructorArgs)
{
	ColorPoint* NewColor = (ColorPoint*)lua_newuserdata(L, sizeof(ColorPoint));
	luaL_getmetatable(L, "tangerine.color_point");
	lua_setmetatable(L, -2);
	new (NewColor) ColorPoint(ConstructorArgs...);
	return NewColor;
}


ColorRamp* GetLuaColorRamp(lua_State* L, int Arg)
{
	return (ColorRamp*)luaL_checkudata(L, Arg, "tangerine.color_ramp");
}


ColorRamp* TestLuaColorRamp(lua_State* L, int Arg)
{
	return (ColorRamp*)luaL_testudata(L, Arg, "tangerine.color_ramp");
}


template<typename... ArgTypes>
static ColorRamp* CreateColorRamp(lua_State* L, ArgTypes... ConstructorArgs)
{
	ColorRamp* NewRamp = (ColorRamp*)lua_newuserdata(L, sizeof(ColorRamp));
	luaL_getmetatable(L, "tangerine.color_ramp");
	lua_setmetatable(L, -2);
	new (NewRamp) ColorRamp(ConstructorArgs...);
	return NewRamp;
}


ColorPoint GetAnyColorPoint(lua_State* L, int& NextArg)
{
	if (ColorPoint* ColorPointArg = TestLuaColorPoint(L, NextArg))
	{
		++NextArg;
		return *ColorPointArg;
	}
	else if (lua_type(L, NextArg) == LUA_TSTRING)
	{
		const char* ColorString = luaL_checklstring(L, NextArg++, nullptr);
		return ParseColor(ColorString);
	}
	else
	{
		return ColorPoint(GetVec3(L, NextArg));
	}
}


static int CreateLuaColorPoint(lua_State* L)
{
	ColorPoint* NewColor;
	ColorPoint* OldColor = TestLuaColorPoint(L, 1);

	int Args = lua_gettop(L);
	if (Args == 1 && OldColor)
	{
		NewColor = CreateColorPoint(L, OldColor->Encoding, OldColor->Channels);
		return 1;
	}
	else if (Args == 1 && lua_type(L, 1) == LUA_TSTRING)
	{
		const char* ColorString = luaL_checklstring(L, 1, nullptr);
		ColorPoint ParsedColor = ParseColor(ColorString);
		NewColor = CreateColorPoint(L, ParsedColor.Encoding, ParsedColor.Channels);
		return 1;
	}
	else if (Args > 1 && lua_type(L, 1) == LUA_TSTRING)
	{
		ColorSpace Encoding = ColorSpace::sRGB;
		const char* EncodingString = luaL_checklstring(L, 1, nullptr);
		if (!FindColorSpace(std::string(EncodingString), Encoding))
		{
			std::string Error = fmt::format("Invalid encoding name: {}\n", EncodingString);
			lua_pushstring(L, Error.c_str());
			lua_error(L);
			lua_pushnil(L);
			return 1;
		}
		int NextArg = 2;
		glm::vec3 Channels = GetVec3(L, NextArg);
		NewColor = CreateColorPoint(L, Encoding, glm::vec3(Channels));
		return 1;
	}
	else if (Args == 1 && lua_isnumber(L, 1))
	{
		float Fill = lua_tonumber(L, 1);
		NewColor = CreateColorPoint(L, glm::vec3(Fill));
		return 1;
	}
	else
	{
		int NextArg = 1;
		glm::vec3 Channels = GetVec3(L, NextArg);
		NewColor = CreateColorPoint(L, glm::vec3(Channels));
		return 1;
	}
}


const luaL_Reg LuaColorPointType[] = \
{
	{ NULL, NULL }
};


static int IndexColor(lua_State* L)
{
	ColorPoint* Self = GetLuaColorPoint(L, 1);

	if (lua_isinteger(L, 2))
	{
		int Lane = lua_tointeger(L, 2) - 1;
		if (Lane >= 0 && Lane < 3)
		{
			lua_pushnumber(L, Self->Channels[Lane]);
			return 1;
		}
	}
	else
	{
		const char* Key = luaL_checklstring(L, 2, nullptr);

		if (strcmp(Key, "encoding") == 0)
		{
			std::string EncodingName = ColorSpaceName(Self->Encoding);
			lua_pushstring(L, EncodingName.c_str());
			return 1;
		}
		else if (strcmp(Key, "channels") == 0)
		{
			LuaVec* NewVec = CreateVec(L, Self->Channels);
			return 1;
		}
		else if (strcmp(Key, "sRGB") == 0)
		{
			ColorPoint* NewColor = CreateColorPoint(L, ColorSpace::sRGB, *Self);
			return 1;
		}
		else if (strcmp(Key, "OkLAB") == 0)
		{
			ColorPoint* NewColor = CreateColorPoint(L, ColorSpace::OkLAB, *Self);
			return 1;
		}
		else if (strcmp(Key, "OkLCH") == 0)
		{
			ColorPoint* NewColor = CreateColorPoint(L, ColorSpace::OkLCH, *Self);
			return 1;
		}
		else if (strcmp(Key, "HSL") == 0)
		{
			ColorPoint* NewColor = CreateColorPoint(L, ColorSpace::HSL, *Self);
			return 1;
		}
		else if (strcmp(Key, "LinearRGB") == 0)
		{
			ColorPoint* NewColor = CreateColorPoint(L, ColorSpace::LinearRGB, *Self);
			return 1;
		}
		else
		{
			std::array<int, 4> Swizzle;
			int Lanes;

			if (ReadSwizzle(Key, Lanes, Swizzle))
			{
				glm::vec4 Channels = glm::vec4(Self->Eval(ColorSpace::sRGB), 1.0f);

				if (Lanes == 1)
				{
					lua_pushnumber(L, Channels[Swizzle[0]]);
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
						NewVec->Vector[Cursor] = Channels[Swizzle[Cursor]];
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
				for (const luaL_Reg& Reg : LuaColorPointType)
				{
					if (Reg.name && strcmp(Reg.name, Key) == 0)
					{
						lua_pushcfunction(L, Reg.func);
						return 1;
					}
				}
			}
		}
	}

	lua_pushnil(L);
	return 1;
}


static int NewIndexColor(lua_State* L)
{
	ColorPoint* Self = GetLuaColorPoint(L, 1);
	std::string Error = "An unknown error occurred.\n";

	if (lua_isinteger(L, 2))
	{
		int Lane = lua_tointeger(L, 2) - 1;
		if (Lane >= 0 && Lane < 3)
		{
			float Value = luaL_checknumber(L, 3);
			Self->Channels[Lane] = Value;
			lua_pushnumber(L, Value);
			return 1;
		}
	}
	else
	{
		const char* Key = luaL_checklstring(L, 2, nullptr);

		if (strcmp(Key, "encoding") == 0)
		{
			const char* EncodingString = luaL_checklstring(L, 3, nullptr);
			ColorSpace NewEncoding;
			if (FindColorSpace(std::string(EncodingString), NewEncoding))
			{
				Self->MutateEncoding(NewEncoding);
			}
			else
			{
				Error = fmt::format("Invalid encoding name: {}\n", EncodingString);
				goto handle_error;
			}
			return 1;
		}
		else if (strcmp(Key, "channels") == 0)
		{
			LuaVec* NewChannels = GetLuaVec(L, 3);
			Self->MutateChannels(NewChannels->Vector.rgb());
			return 1;
		}
		else
		{
			const char* Key = luaL_checklstring(L, 2, nullptr);
			std::array<int, 4> Swizzle;
			int Lanes;

			if (ReadSwizzle(Key, Lanes, Swizzle))
			{
				Self->MutateEncoding(ColorSpace::sRGB);
				glm::vec4 Channels = glm::vec4(Self->Eval(ColorSpace::sRGB), 1.0f);

				if (Lanes == 1)
				{
					float Value = luaL_checknumber(L, 3);
					Channels[Swizzle[0]] = Value;
					Self->Channels = Channels.xyz();
					lua_pushnumber(L, Value);
					return 1;
				}
				else
				{
					for (int Cursor = 0; Cursor < Lanes; ++Cursor)
					{
						lua_geti(L, 3, Cursor + 1);
						Channels[Swizzle[Cursor]] = lua_tonumber(L, 4);
						lua_pop(L, 1);
					}
					Self->Channels = Channels.xyz();
					lua_getfield(L, 1, Key);
					return 1;
				}
			}
			else
			{
				Error = fmt::format("Invalid color property: {}\n", Key);
				goto handle_error;
			}
		}
	}

handle_error:
	lua_pushstring(L, Error.c_str());
	lua_error(L);

	lua_pushnil(L);
	return 1;
}


static int ColorToString(lua_State* L)
{
	ColorPoint* Self = GetLuaColorPoint(L, 1);
	glm::vec3 Color = Self->Eval(ColorSpace::sRGB);
	std::string Repr = fmt::format("color({}, {}, {})", Color[0], Color[1], Color[2]);
	lua_pushstring(L, Repr.c_str());
	return 1;
}


static int ColorSize(lua_State* L)
{
	ColorPoint* Self = GetLuaColorPoint(L, 1);
	lua_pushinteger(L, 3);
	return 1;
}


const luaL_Reg LuaColorPointMeta[] = \
{
	{ "__index", IndexColor },
	{ "__newindex", NewIndexColor },
	{ "__tostring", ColorToString },

	{ "__len", ColorSize },

	{ NULL, NULL }
};


static int CreateLuaColorRamp(lua_State* L)
{
	int NextArg = 1;
	int Args = lua_gettop(L);
	std::vector<ColorPoint> Stops;
	Stops.reserve(Args);

	ColorSpace Encoding = ColorSpace::OkLAB;

	if (lua_type(L, 1) == LUA_TSTRING)
	{
		const char* EncodingString = luaL_checklstring(L, 1, nullptr);
		if (!FindColorSpace(std::string(EncodingString), Encoding))
		{
			// TODO: If CreateLuaColorRamp were to be switched over to only accept "tangerine.color_point"
			// userdata objects, then it would make sense to throw an error here, but since this function
			// instead accepts color-like inputs, the inputs are too ambiguous to throw anything here.
#if 0
			std::string Error = fmt::format("Invalid encoding name: {}\n", EncodingString);
			lua_pushstring(L, Error.c_str());
			lua_error(L);
			lua_pushnil(L);
			return 1;
#endif
		}
	}

	for (; NextArg <= Args;)
	{
		Stops.push_back(GetAnyColorPoint(L, NextArg));
	}

	ColorRamp* Ramp = CreateColorRamp(L, Stops, Encoding);
	return 1;
}


static int EvalRamp(lua_State* L)
{
	ColorRamp* Ramp = GetLuaColorRamp(L, 1);
	float Alpha = luaL_checknumber(L, 2);

	ColorSpace Encoding = Ramp->Encoding;
	glm::vec3 Channels = Ramp->Eval(Encoding, Alpha);

	ColorPoint* NewColor = CreateColorPoint(L, Encoding, Channels);
	return 1;
}


const luaL_Reg LuaColorRampType[] = \
{
	{ "eval", EvalRamp },
	{ NULL, NULL }
};


static int IndexRamp(lua_State* L)
{
	ColorRamp* Self = GetLuaColorRamp(L, 1);
	const char* Key = luaL_checklstring(L, 2, nullptr);

	for (const luaL_Reg& Reg : LuaColorRampType)
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


const luaL_Reg LuaColorRampMeta[] = \
{
	{ "__index", IndexRamp },
	{ NULL, NULL }
};


const luaL_Reg LuaColorLibrary[] = \
{
	{ "color", CreateLuaColorPoint },
	{ "color_point", CreateLuaColorPoint },

	{ "ramp", CreateLuaColorRamp },
	{ "color_ramp", CreateLuaColorRamp },

	{ NULL, NULL }
};


int LuaOpenColor(lua_State* L)
{
	luaL_newmetatable(L, "tangerine.color_point");
	luaL_setfuncs(L, LuaColorPointMeta, 0);

	luaL_newmetatable(L, "tangerine.color_ramp");
	luaL_setfuncs(L, LuaColorRampMeta, 0);

	luaL_newlib(L, LuaColorLibrary);
	return 1;
}


#endif
