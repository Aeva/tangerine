
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


//#ifdef PROFILING
#if 1

#include <stddef.h>
#include "Remotery.h"


extern Remotery* RemoteryInstance;


void StartProfiling();


void StopProfiling();


#define BeginEvent(EventName) rmt_BeginCPUSample(EventName, 0)


#define EndEvent() rmt_EndCPUSample()


#define ScopedEvent(EventName) rmt_ScopedCPUSample(EventName, 0)


#define RootScopeEvent(EventName) rmt_ScopedCPUSample(EventName, RMTSF_Root)


#else
#define StartProfiling(...)
#define StopProfiling(...)
#define BeginEvent(...)
#define EndEvent(...)
#define ScopeEvent(...)
#define RootScopeEvent(...)
#endif
