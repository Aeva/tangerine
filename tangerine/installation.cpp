
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

#if !_WIN64
#include <unistd.h>
#endif

namespace fs = std::filesystem;

TangerinePaths::TangerinePaths(int argc, char* argv[])
{
#if _WIN64
	// TODO: This is fine for standalone builds, but we will want to do something
	// else for future library builds.  Maybe GetModuleFileNameW?
	ExecutablePath = fs::absolute(argv[0]);

#elif EMBED_RACKET
	ExecutablePath = fs::path(racket_get_self_exe_path(argv[0]));

#else
	{
		char* Path = nullptr;
		ssize_t PathLength = 0;
		ssize_t Allocation = 256;

retry:
		Path = (char*)malloc(Allocation);
		PathLength = readlink("/proc/self/exe", Path, Allocation - 1);
		if (PathLength == (Allocation - 1))
		{
			free(Path);
			Allocation *= 2;
			goto retry;
		}

		if (PathLength < 0)
		{
			// Possibly in a chroot environment where "/proc" is not available, so fall back to generic approach.
			ExecutablePath = fs::absolute(argv[0]);
		}
		else
		{
			Path[PathLength] = 0;
			ExecutablePath = fs::path(Path);
		}

		free(Path);
	}
#endif

	ExecutableDir = ExecutablePath.parent_path();

#ifdef TANGERINE_PKGDATADIR_FROM_BINDIR
#define STRINGIFY(x) #x
#define EXPAND_AS_STR(x) STRINGIFY(x)
	PkgDataDir = ExecutableDir / fs::path(EXPAND_AS_STR(TANGERINE_PKGDATADIR_FROM_BINDIR));
#undef EXPAND_AS_STR
#undef STRINGIFY
#else
	PkgDataDir = ExecutableDir;
#endif

	ShadersDir = PkgDataDir / fs::path("shaders");
	ModelsDir = PkgDataDir / fs::path("models");
}
