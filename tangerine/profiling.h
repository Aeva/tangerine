
// Copyright 2023 Aeva Palecek
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

#ifdef MINIMAL_DLL
#define ENABLE_PROFILING 0
#endif

#ifndef ENABLE_PROFILING
#define ENABLE_PROFILING 0
#endif

#include <chrono>
#include <string>


using ProfilingClock = std::chrono::steady_clock;
using ProfilingTimePoint = ProfilingClock::time_point;

#pragma region
// If any of these assertions fail, consider adding a preprocessor condition
// to use a different clock for ProfilingClock.

static_assert(ProfilingClock::is_steady,
	"The global clock must be monotonic.");

static_assert(ProfilingClock::period::num == 1,
	"The global clock must be able to provide submillisecond timepoints.");

static_assert(ProfilingClock::period::den > std::milli::den,
	"The global clock must be able to provide submillisecond timepoints.");

#pragma endregion

void BeginEvent(const char* EventName);
void BeginEvent(std::string EventName);

void EndEvent();

struct ProfileScope
{
	ProfileScope(const char* EventName);
	ProfileScope(std::string EventName);
	~ProfileScope();
};
