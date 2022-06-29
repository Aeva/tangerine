
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


enum class StatusCode
{
	PASS,
	FAIL
};


#if _DEBUG
void Assert(bool Condition);
#else
#define Assert(...)
#endif


#if _WIN64
#define BreakPoint() __debugbreak()
#define UNREACHABLE() __assume(0)
#else
#define BreakPoint()
#define UNREACHABLE() __builtin_unreachable()
#endif


#define FAILED(Expr) (Expr == StatusCode::FAIL)
#define RETURN_ON_FAIL(Expr) { StatusCode Result = Expr; if (Result == StatusCode::FAIL) return Result; }
