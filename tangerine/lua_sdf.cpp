
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

#include "lua_sdf.h"
#if EMBED_LUA

#include <string.h>
#include <random>
#include <algorithm>

#include "sdfs.h"
#include "colors.h"
#include "tangerine.h"


std::default_random_engine RNGesus;


SDFNode* GetSDFNode(lua_State* L, int Arg)
{
	SDFNode** Node = (SDFNode**)luaL_checkudata(L, Arg, "tangerine.sdf");
	return *Node;
}


int WrapSDFNode(lua_State* L, SDFNode* NewNode)
{
	SDFNode** Wrapper = (SDFNode**)lua_newuserdata(L, sizeof(SDFNode*));
	luaL_getmetatable(L, "tangerine.sdf");
	lua_setmetatable(L, -2);
	*Wrapper = NewNode;
	NewNode->Hold();
	return 1;
}


int LuaMoveInner(lua_State* L, float X, float Y, float Z)
{
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	NewNode->Move(glm::vec3(X, Y, Z));
	return WrapSDFNode(L, NewNode);
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
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	NewNode->Move(glm::vec3(X, Y, Z));
	return WrapSDFNode(L, NewNode);
}


int LuaAlign(lua_State* L)
{
	glm::vec3 Anchors(
		(float)luaL_checknumber(L, 2),
		(float)luaL_checknumber(L, 3),
		(float)luaL_checknumber(L, 4));
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	SDF::Align(NewNode, Anchors);
	return WrapSDFNode(L, NewNode);
}


int LuaRotateInner(lua_State* L, float X, float Y, float Z, float W = 1.0)
{
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	NewNode->Rotate(glm::quat(W, X, Y, Z));
	return WrapSDFNode(L, NewNode);
}


int LuaRotateX(lua_State* L)
{
	float Degrees = (float)luaL_checknumber(L, 2);
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	SDF::RotateX(NewNode, Degrees);
	return WrapSDFNode(L, NewNode);
}


int LuaRotateY(lua_State* L)
{
	float Degrees = (float)luaL_checknumber(L, 2);
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	SDF::RotateY(NewNode, Degrees);
	return WrapSDFNode(L, NewNode);
}


int LuaRotateZ(lua_State* L)
{
	float Degrees = (float)luaL_checknumber(L, 2);
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	SDF::RotateZ(NewNode, Degrees);
	return WrapSDFNode(L, NewNode);
}


template <bool Force>
int LuaPaint(lua_State* L)
{
	const char* ColorString = luaL_checklstring(L, 2, nullptr);
	glm::vec3 Color;
	StatusCode Result = ParseColor(ColorString, Color);
	SDFNode* Node = GetSDFNode(L, 1);
	SDFNode* NewNode = Node->Copy();
	NewNode->ApplyMaterial(Color, Force);
	return WrapSDFNode(L, NewNode);
}


int LuaSphere(lua_State* L)
{
	float Radius = luaL_checknumber(L, 1) * .5;
	SDFNode* NewNode = SDF::Sphere(Radius);
	return WrapSDFNode(L, NewNode);
}


int LuaEllipsoid(lua_State* L)
{
	float RadipodeX = luaL_checknumber(L, 1) * .5;
	float RadipodeY = luaL_checknumber(L, 2) * .5;
	float RadipodeZ = luaL_checknumber(L, 3) * .5;
	SDFNode* NewNode = SDF::Ellipsoid(RadipodeX, RadipodeY, RadipodeZ);
	return WrapSDFNode(L, NewNode);
}


int LuaBox(lua_State* L)
{
	float ExtentX = luaL_checknumber(L, 1) * .5;
	float ExtentY = luaL_checknumber(L, 2) * .5;
	float ExtentZ = luaL_checknumber(L, 3) * .5;
	SDFNode* NewNode = SDF::Box(ExtentX, ExtentY, ExtentZ);
	return WrapSDFNode(L, NewNode);
}


int LuaCube(lua_State* L)
{
	float Extent = luaL_checknumber(L, 1) * .5;
	SDFNode* NewNode = SDF::Box(Extent, Extent, Extent);
	return WrapSDFNode(L, NewNode);
}


int LuaTorus(lua_State* L)
{
	float MajorRadius = luaL_checknumber(L, 1) * .5;
	float MinorRadius = luaL_checknumber(L, 2) * .5;
	SDFNode* NewNode = SDF::Torus(MajorRadius - MinorRadius, MinorRadius);
	return WrapSDFNode(L, NewNode);
}


int LuaCylinder(lua_State* L)
{
	float Radius = luaL_checknumber(L, 1) * .5;
	float Extent = luaL_checknumber(L, 2) * .5;
	SDFNode* NewNode = SDF::Cylinder(Radius, Extent);
	return WrapSDFNode(L, NewNode);
}


int LuaPlane(lua_State* L)
{
	float NormalX = luaL_checknumber(L, 1);
	float NormalY = luaL_checknumber(L, 2);
	float NormalZ = luaL_checknumber(L, 3);
	SDFNode* NewNode = SDF::Plane(NormalX, NormalY, NormalZ);
	return WrapSDFNode(L, NewNode);
}


enum class SetFamily : uint32_t
{
	Union,
	Inter,
	Diff
};


template<SetFamily Family>
SDFNode* Operator(SDFNode* LHS, SDFNode* RHS)
{
	if (Family == SetFamily::Union)
	{
		return SDF::Union(LHS, RHS);
	}
	else if (Family == SetFamily::Inter)
	{
		return SDF::Inter(LHS, RHS);
	}
	else
	{
		return SDF::Diff(LHS, RHS);
	}
}


template<SetFamily Family>
SDFNode* Operator(float Threshold, SDFNode* LHS, SDFNode* RHS)
{
	if (Family == SetFamily::Union)
	{
		return SDF::BlendUnion(Threshold, LHS, RHS);
	}
	else if (Family == SetFamily::Inter)
	{
		return SDF::BlendInter(Threshold, LHS, RHS);
	}
	else
	{
		return SDF::BlendDiff(Threshold, LHS, RHS);
	}
}


template<SetFamily Family>
int LuaOperator(lua_State* L)
{
	SDFNode** LHS = (SDFNode**)luaL_checkudata(L, 1, "tangerine.sdf");
	SDFNode** RHS = (SDFNode**)luaL_checkudata(L, 2, "tangerine.sdf");
	SDFNode* NewNode = Operator<Family>(*LHS, *RHS);

	int Args = lua_gettop(L);
	for (int i = 3; i <= Args; ++i)
	{
		SDFNode** Next = (SDFNode**)luaL_checkudata(L, i, "tangerine.sdf");
		NewNode = Operator<Family>(NewNode, *Next);
	}

	return WrapSDFNode(L, NewNode);
}


template<SetFamily Family>
int LuaBlendOperator(lua_State* L)
{
	float Threshold = (float)luaL_checknumber(L, -1);
	lua_pop(L, 1);

	SDFNode** LHS = (SDFNode**)luaL_checkudata(L, 1, "tangerine.sdf");
	SDFNode** RHS = (SDFNode**)luaL_checkudata(L, 2, "tangerine.sdf");
	SDFNode* NewNode = Operator<Family>(Threshold, *LHS, *RHS);

	int Args = lua_gettop(L);
	for (int i = 3; i <= Args; ++i)
	{
		SDFNode** Next = (SDFNode**)luaL_checkudata(L, i, "tangerine.sdf");
		NewNode = Operator<Family>(Threshold, NewNode, *Next);
	}

	return WrapSDFNode(L, NewNode);
}


int LuaClearColor(lua_State* L)
{
	const char* ColorString = luaL_checklstring(L, 1, nullptr);
	glm::vec3 Color;
	StatusCode Result = ParseColor(ColorString, Color);
	SetClearColor(Color);
	return 0;
}


int LuaFixedCamera(lua_State* L)
{
	glm::vec3 Origin(
		luaL_checknumber(L, 1),
		luaL_checknumber(L, 2),
		luaL_checknumber(L, 3));
	glm::vec3 Center(
		luaL_checknumber(L, 4),
		luaL_checknumber(L, 5),
		luaL_checknumber(L, 6));
	glm::vec3 Up(
		luaL_checknumber(L, 7),
		luaL_checknumber(L, 8),
		luaL_checknumber(L, 9));
	SetFixedCamera(Origin, Center, Up);
	return 0;
}


int LuaRandomSeed(lua_State* L)
{
	lua_Integer Seed = luaL_checkinteger(L, 1);
	RNGesus.seed(Seed);
	return 0;
}


int LuaRandom(lua_State* L)
{
	if (lua_gettop(L) == 0)
	{
		// Return a random float between 0.0 and 1.0, inclusive.
		std::uniform_real_distribution<double> Dist(0.0, std::nextafter(1.0, DBL_MAX));
		lua_Number Random = (lua_Number)Dist(RNGesus);
		lua_pushnumber(L, Random);
		return 1;
	}
	else if (lua_gettop(L) >= 2 && lua_isinteger(L, 1) && lua_isinteger(L, 2))
	{
		// Return a random integer between Low and High, inclusive.
		lua_Integer Low = luaL_checkinteger(L, 1);
		lua_Integer High = luaL_checkinteger(L, 2);
		std::uniform_int_distribution<lua_Integer> Dist(Low, High);
		lua_pushinteger(L, Dist(RNGesus));
		return 1;
	}
	else
	{
		// Return a random float between Low and High, inclusive.
		lua_Number Low = luaL_checknumber(L, 1);
		lua_Number High = luaL_checknumber(L, 2);
		std::uniform_real_distribution<double> Dist(Low, std::nextafter(High, DBL_MAX));
		lua_Number Random = (lua_Number)Dist(RNGesus);
		lua_pushnumber(L, Random);
		return 1;
	}
}


int LuaShuffleSequence(lua_State* L)
{
	lua_Integer Count = luaL_checkinteger(L, 1);
	if (Count > 0)
	{
		std::vector<int> Deck;
		Deck.reserve(Count);
		for (int i = 1; i <= Count; ++i)
		{
			Deck.push_back(i);
		}
		std::shuffle(Deck.begin(), Deck.end(), RNGesus);
		lua_createtable(L, Count, 0);
		int i = 1;
		for (int& Index : Deck)
		{
			lua_pushnumber(L, Index);
			lua_rawseti(L, -2, i++);
		}
		return 1;
	}
	return 0;
}


const luaL_Reg LuaSDFType[] = \
{
	{ "move", LuaMove },
	{ "move_x", LuaMoveX },
	{ "move_y", LuaMoveY },
	{ "move_z", LuaMoveZ },

	{ "align", LuaAlign },

	{ "rotate_x", LuaRotateX },
	{ "rotate_y", LuaRotateY },
	{ "rotate_z", LuaRotateZ },

	{ "paint", LuaPaint<false> },
	{ "paint_over", LuaPaint<true> },

	{ "sphere", LuaSphere },
	{ "ellipsoid", LuaEllipsoid },
	{ "box", LuaBox },
	{ "cube", LuaCube },
	{ "torus", LuaTorus },
	{ "cylinder", LuaCylinder },
	{ "plane", LuaPlane },

	{ "union", LuaOperator<SetFamily::Union> },
	{ "inter", LuaOperator<SetFamily::Inter> },
	{ "diff", LuaOperator<SetFamily::Diff> },

	{ "blend_union", LuaBlendOperator<SetFamily::Union> },
	{ "blend_inter", LuaBlendOperator<SetFamily::Inter> },
	{ "blend_diff", LuaBlendOperator<SetFamily::Diff> },

	{ "set_bg", LuaClearColor },
	{ "set_fixed_camera", LuaFixedCamera },

	{ "random_seed", LuaRandomSeed },
	{ "random", LuaRandom },
	{ "shuffle_sequence", LuaShuffleSequence },

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
		(*Self)->Release();
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

#endif // EMBED_LUA
