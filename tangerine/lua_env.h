
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

#pragma once

#include "embedding.h"
#if EMBED_LUA
#include <string>

struct LuaEnvironment : public ScriptEnvironment
{
	struct lua_State* L = nullptr;

	LuaEnvironment();

	virtual Language GetLanguage()
	{
		return Language::Lua;
	}

	virtual void Advance(double DeltaTimeMs, double ElapsedTimeMs);

	virtual void LoadFromPath(std::string Path);
	virtual void LoadFromString(std::string Source);
	virtual ~LuaEnvironment();

	static LuaEnvironment* GetScriptEnvironment(struct lua_State* L);
	static int LuaSetAdvanceEvent(struct lua_State* L);

private:
	int AdvanceCallbackRef;

	void LoadLuaModelCommon(int Error);
};

#endif //EMBED_LUA
