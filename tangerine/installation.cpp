
// Copyright 2023 Philip McGrath
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

#include "installation.h"
#include "whereami.h"
#include <iostream>


StatusCode TangerinePaths::PopulateInstallationPaths()
{
	{
		int Length = wai_getExecutablePath(NULL, 0, NULL);
		if (Length > -1)
		{
			char* Path = (char*)malloc(Length + 1);
			wai_getExecutablePath(Path, Length, NULL);
			Path[Length] = '\0';
			ExecutablePath = std::filesystem::path(Path);
			free(Path);
		}
		else
		{
#if !_WIN64
			// TODO: Right now our platform coverage is "Windows" and "not Windows == Linux".  More nuance may be needed here in the future.
			if (!std::filesystem::exists("/proc"))
			{
				std::cout << "Proc filesystem not found.\n";
			}
#endif
			std::cout << "Failed to determine Tangerine's filesystem path.\n";
			return StatusCode::FAIL;
		}
	}

	ExecutableDir = ExecutablePath.parent_path();

#ifdef TANGERINE_PKGDATADIR_FROM_BINDIR
#define STRINGIFY(x) #x
#define EXPAND_AS_STR(x) STRINGIFY(x)
	PkgDataDir = ExecutableDir / std::filesystem::path(EXPAND_AS_STR(TANGERINE_PKGDATADIR_FROM_BINDIR));
#undef EXPAND_AS_STR
#undef STRINGIFY
#else
	PkgDataDir = ExecutableDir;
#endif

	ShadersDir = PkgDataDir / std::filesystem::path("shaders");
	ModelsDir = PkgDataDir / std::filesystem::path("models");

	return StatusCode::PASS;
}
