
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

#pragma once

#include "embedding.h"
#if EMBED_LUA
#include "painting_set.h"
#include <lua/lua.hpp>

PaintingSetShared* CheckLuaPaintingSet(lua_State* L, int Arg);
PaintingSetShared* TestLuaPaintingSet(lua_State* L, int Arg);
int LuaOpenPaintingSet(struct lua_State* L);

#endif // EMBED_LUA
