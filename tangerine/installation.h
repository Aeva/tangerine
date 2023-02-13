
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

#pragma once
#include <filesystem>
#include "embedding.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
# define TANGERINE_WINDOWS
#else
# define TANGERINE_UNIX
#endif

std::filesystem::path tangerine_get_self_exe_path(char* argv0);

struct TangerineInstallation
{
        char* argv0;
        std::filesystem::path SelfExePath = tangerine_get_self_exe_path(argv0);
        std::filesystem::path ExecutableDir = SelfExePath.parent_path();
        std::filesystem::path PkgDataDir {
#ifdef TANGERINE_PKGDATADIR_FROM_BINDIR
# define AS_a_STR_HELPER(x) #x
# define AS_a_STR(x) AS_a_STR_HELPER(x)
# define TPFB TANGERINE_PKGDATADIR_FROM_BINDIR
		ExecutableDir / std::filesystem::path(AS_a_STR(TPFB))
# undef TPFB
# undef AS_a_STR
# undef AS_a_STR_HELPER
#else
		ExecutableDir
#endif
	};
        std::filesystem::path ShadersDir =
		PkgDataDir / std::filesystem::path("shaders");
        std::filesystem::path ModelsDir =
		PkgDataDir / std::filesystem::path("models");
};
