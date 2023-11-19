
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

#ifndef ENABLE_PSMOVE_BINDINGS
#define ENABLE_PSMOVE_BINDINGS _WIN64
#endif

#if ENABLE_PSMOVE_BINDINGS
#include "errors.h"
#include "glm_common.h"
#include "psmove/psmove.h"


void BootPSMove();


void TeardownPSMove();


bool PSMoveAvailable();


struct MoveConnection
{
	int Index = -1;
	PSMove* Handle = nullptr;
	PSMove_Connection_Type Connection = Conn_Unknown;
	PSMove_Model_Type Model;
	bool Local = false;
	char* Serial;
	glm::quat Orientation = glm::identity<glm::quat>();

	MoveConnection(int InIndex);

	int Score();

	void SetColor(glm::vec3 Color);

	void Activate();

	void Refresh();

	~MoveConnection();
};


#endif // ENABLE_PSMOVE_BINDINGS
