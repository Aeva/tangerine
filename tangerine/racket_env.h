
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
#if EMBED_RACKET
#include <string>

struct RacketEnvironment : public ScriptEnvironment
{
	RacketEnvironment() {}

	virtual Language GetLanguage()
	{
		return Language::Racket;
	}

	virtual void LoadFromPath(std::string Path);
	virtual void LoadFromString(std::string Source);
	virtual ~RacketEnvironment() {}
};

void BootRacket();

#endif //EMBED_RACKET
