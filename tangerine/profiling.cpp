
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

#include "profiling.h"

#if _WIN64

#if ENABLE_PROFILING

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#define PROFILE
#include "pix3.h"

/*
 * NOTE:
 * To use the profiling mode on Windows, you need to install the PIX event
 * runtime into this project via Nuget, as described in this link:
 * https://devblogs.microsoft.com/pix/winpixeventruntime/
 *
 * You also need to have PIX for Windows installed to do anything useful with
 * this.
 */

#endif // ENABLE_PROFILING

#define TANGERINE_API __declspec(dllexport)
#elif defined(__GNUC__)
#define TANGERINE_API __attribute__ ((visibility ("default")))

#endif


void BeginEvent(const char* EventName)
{
#ifdef USE_PIX
	PIXBeginEvent(PIX_COLOR(0x00, 0x00, 0x80), EventName);
#endif
}


void EndEvent()
{
#ifdef USE_PIX
	PIXEndEvent();
#endif
}


ProfileScope::ProfileScope(const char* EventName)
{
	BeginEvent(EventName);
}


ProfileScope::~ProfileScope()
{
	EndEvent();
}


#ifdef USE_PIX
extern "C" TANGERINE_API void BeginRacketEvent(const char* EventName)
{
	PIXBeginEvent(PIX_COLOR(0xFF, 0x00, 0x00), EventName);
}


extern "C" TANGERINE_API void EndRacketEvent()
{
	PIXEndEvent();
}
#endif