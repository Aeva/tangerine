
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

//#ifdef PROFILING
#if 1


#if _WIN64
#define TANGERINE_API __declspec(dllexport)
#elif defined(__GNUC__)
#define TANGERINE_API __attribute__ ((visibility ("default")))
#endif


Remotery* RemoteryInstance = nullptr;


void StartProfiling()
{
	rmt_CreateGlobalInstance(&RemoteryInstance);
}


void StopProfiling()
{
	rmt_DestroyGlobalInstance(RemoteryInstance);
	RemoteryInstance = nullptr;
}


#endif


extern "C" TANGERINE_API void BeginRacketEvent(const char* EventName)
{
	rmt_BeginCPUSampleDynamic(EventName, 0);
}


extern "C" TANGERINE_API void EndRacketEvent()
{
	EndEvent();
}
