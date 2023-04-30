
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

#include <glad/gl.h>
#include <vector>
#include <string>
#include <map>
#include <atomic>
#include "errors.h"


struct ShaderSource
{
	enum class Variant
	{
		PATH,
		STR,
		LIST,
	};
	Variant Mode;
	std::string Source;
	std::vector<ShaderSource> Composite;

	inline ShaderSource(std::string InSource, bool IsPath)
	{
		Source = InSource;
		Mode = IsPath ? Variant::PATH : Variant::STR;
	}
	inline ShaderSource(std::vector<ShaderSource> InComposite)
	{
		Composite = InComposite;
		Mode = Variant::LIST;
	}
};


ShaderSource GeneratedShader(std::string PrePath, std::string Generated, std::string PostPath);


struct CompileInfo
{
	GLuint ShaderID;
	std::vector<std::string> Sources;
	std::vector<std::string> Index;
};


struct ShaderProgram
{
	GLuint ProgramID = 0;

	std::map<GLenum, ShaderSource> Shaders;
	std::string ProgramName;

	void AsyncSetup(std::map<GLenum, ShaderSource> InShaders, const char* InProgramName);
	StatusCode Setup(std::map<GLenum, ShaderSource> InShaders, const char* InProgramName);
	StatusCode Compile();

	void Activate();
	void Reset();
};


struct Buffer
{
	GLuint BufferID;
	const char* DebugName;
	size_t LastSize;
	Buffer(Buffer&& OldBuffer);
	Buffer(const Buffer& CopySource) = delete;
	Buffer(const char* InDebugName = nullptr);
	~Buffer();
	void Release();
	void Reserve(size_t Bytes);
	void Upload(void* Data, size_t Bytes);
	void Bind(GLenum Target, GLuint BindingIndex);
	void Bind(GLenum Target);
};


struct TimingQuery
{
	bool Pending = false;
	GLuint QueryID = 0;
	void Create(size_t SampleCount = 10);
	void Release();
	void Start();
	void Stop();
	double ReadMs();
private:
	std::vector<double> Samples;
	size_t Cursor = 0;
};


struct DrawArraysIndirectCommand
{
	uint32_t Count;
	uint32_t InstanceCount;
	uint32_t First;
	uint32_t BaseInstance;
};
