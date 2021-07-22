
// Copyright 2021 Aeva Palecek
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
// See the License for the specific language governing permissionsand
// limitations under the License.

#pragma once

#include <glad/glad.h>
#include <vector>
#include <string>
#include <map>
#include "errors.h"


struct ShaderPipeline
{
	GLuint PipelineID = 0;
	std::map<GLenum, GLuint> Stages;
	std::vector<struct BindingPoint*> BindingPoints;

	StatusCode Setup(std::map<GLenum, std::string> Shaders, const char* PipelineName);
	void Activate();
};


struct Buffer
{
	GLuint BufferID;
	const char* DebugName;
	size_t LastSize;
	Buffer(const char* InDebugName = nullptr);
	~Buffer();
	void Release();
	void Reserve(size_t Bytes);
	void Upload(void* Data, size_t Bytes);
	void Bind(GLenum Target, GLuint BindingIndex);
	void Bind(GLenum Target);
};
