
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

#include "racket_env.h"
#if EMBED_RACKET

#include "tangerine.h"
#include "extern.h"
#include <chezscheme.h>
#include <racketcs.h>
#include <iostream>

extern "C" TANGERINE_API void RacketErrorCallback(const char* ErrorMessage)
{
	PostScriptError(ErrorMessage);
}

void RacketEnvironment::LoadFromPath(std::string Path)
{
	auto LoadAndProcess = [&]()
	{
		Sactivate_thread();
		ptr ModuleSymbol = Sstring_to_symbol("tangerine");
		ptr ProcSymbol = Sstring_to_symbol("renderer-load-and-process-model");
		ptr Proc = Scar(racket_dynamic_require(ModuleSymbol, ProcSymbol));
		ptr Args = Scons(Sstring(Path.c_str()), Snil);
		racket_apply(Proc, Args);
		Sdeactivate_thread();
	};
	LoadModelCommon(LoadAndProcess);
}

void RacketEnvironment::LoadFromString(std::string Source)
{
	auto LoadAndProcess = [&]()
	{
		Sactivate_thread();
		ptr ModuleSymbol = Sstring_to_symbol("tangerine");
		ptr ProcSymbol = Sstring_to_symbol("renderer-load-untrusted-model");
		ptr Proc = Scar(racket_dynamic_require(ModuleSymbol, ProcSymbol));
		ptr Args = Scons(Sstring_utf8(Source.c_str(), Source.size()), Snil);
		racket_apply(Proc, Args);
		Sdeactivate_thread();
	};
	LoadModelCommon(LoadAndProcess);
}

#if !defined(TANGERINE_RACKET_PETITE_BOOT)
#define TANGERINE_RACKET_PETITE_BOOT ./racket/petite.boot
#endif
#if !defined(TANGERINE_RACKET_SCHEME_BOOT)
#define TANGERINE_RACKET_SCHEME_BOOT ./racket/scheme.boot
#endif
#if !defined(TANGERINE_RACKET_RACKET_BOOT)
#define TANGERINE_RACKET_RACKET_BOOT ./racket/racket.boot
#endif
#if !defined(TANGERINE_RACKET_COLLECTS_DIR)
#define TANGERINE_RACKET_COLLECTS_DIR ./racket/collects
#endif
#if !defined(TANGERINE_RACKET_CONFIG_DIR)
#define TANGERINE_RACKET_CONFIG_DIR ./racket/etc
#endif

void BootRacket(int argc, char* argv[])
{
	std::cout << "Setting up Racket CS... ";
	racket_boot_arguments_t BootArgs;
	memset(&BootArgs, 0, sizeof(BootArgs));
	BootArgs.exec_file = argv[0];
#define STRINGIFY(x) #x
#if !defined(TANGERINE_USE_SYSTEM_RACKET)
	char *SelfExe = racket_get_self_exe_path(BootArgs.exec_file);
#define RESOLVE(sym) racket_path_replace_filename(SelfExe,STRINGIFY(sym))
#else
#define RESOLVE(sym) STRINGIFY(sym)
#endif
	BootArgs.boot1_path = RESOLVE(TANGERINE_RACKET_PETITE_BOOT);
	BootArgs.boot2_path = RESOLVE(TANGERINE_RACKET_SCHEME_BOOT);
	BootArgs.boot3_path = RESOLVE(TANGERINE_RACKET_RACKET_BOOT);
	BootArgs.collects_dir = RESOLVE(TANGERINE_RACKET_COLLECTS_DIR);
	BootArgs.config_dir = RESOLVE(TANGERINE_RACKET_CONFIG_DIR);
#undef RESOLVE
#undef STRINGIFY
	racket_boot(&BootArgs);

	std::cout << "Done!\n";
#if !defined(TANGERINE_USE_SYSTEM_RACKET)
	free(SelfExe);
	free(BootArgs.boot1_path);
	free(BootArgs.boot2_path);
	free(BootArgs.boot3_path);
	free(BootArgs.collects_dir);
	free(BootArgs.config_dir);
#endif
}
#endif //EMBED_RACKET
