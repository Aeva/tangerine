
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

void BootRacket()
{
	std::cout << "Setting up Racket CS... ";
	racket_boot_arguments_t BootArgs;
	memset(&BootArgs, 0, sizeof(BootArgs));
	BootArgs.boot1_path = "./racket/petite.boot";
	BootArgs.boot2_path = "./racket/scheme.boot";
	BootArgs.boot3_path = "./racket/racket.boot";
	BootArgs.exec_file = "tangerine.exe";
	BootArgs.collects_dir = "./racket/collects";
	BootArgs.config_dir = "./racket/etc";
	racket_boot(&BootArgs);
	std::cout << "Done!\n";
}
#endif //EMBED_RACKET
