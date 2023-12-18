
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
#include <functional>
#include "sdf_evaluator.h"

void PostScriptError(std::string ErrorMessage);
void LoadModelCommon(std::function<void()> LoadingCallback);
void SetWindowTitle(std::string Title);
void ShowDebugMenu();
void HideDebugMenu();
void SetClearColor(glm::vec3& Color);
void SetFixedCamera(glm::vec3& Origin, glm::vec3& Focus, glm::vec3& Up);

struct ScriptEnvironment* GetMainEnvironment();
