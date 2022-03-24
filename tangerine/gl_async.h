
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


#include "gl_boilerplate.h"
#include <memory>


#if _WIN64
#define ENABLE_ASYNC_SHADER_COMPILE 0
#endif


struct ShaderEnvelope
{
	std::atomic_bool Ready = false;
	std::atomic_bool Failed = false;
	std::unique_ptr<ShaderProgram> Shader = nullptr;

	ShaderProgram* Access();
	~ShaderEnvelope();
};


void SetPipelineDefaults();


void AsyncCompile(std::unique_ptr<ShaderProgram> NewProgram, std::shared_ptr<ShaderEnvelope> Outbox);


void StartWorkerThreads();


void JoinWorkerThreads();
