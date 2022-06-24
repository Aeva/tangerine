
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

#if _DEBUG
#include <cstdlib>
#include <iostream>
#include "errors.h"

void Assert(bool Condition)
{
	if (!Condition)
	{
		std::cout << "ASSERTION FAILURE\n";
		BreakPoint();
		std::abort();
	}
}

#endif
