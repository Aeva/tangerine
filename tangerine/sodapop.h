
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

#include "errors.h"
#include "sdf_rendering.h"


#if RENDERER_SODAPOP
struct SDFNode;
struct SodapopDrawable;


namespace Sodapop
{
	StatusCode Setup();
	void Teardown();
	void Advance();

	void Schedule(SodapopDrawable* Drawable, SDFNode* Evaluator);
}
#endif
