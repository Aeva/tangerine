
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

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include "gl_init.h"
#include "gl_boilerplate.h"
#include "profiling.h"
#include "installation.h"

extern TangerinePaths Installed;


inline GLsizei min(GLsizei LHS, GLsizei RHS)
{
	return LHS <= RHS ? LHS : RHS;
}


GLsizei GetMaxLabelLength()
{
	GLint MaxLabelLength;
	glGetIntegerv(GL_MAX_LABEL_LENGTH, &MaxLabelLength);
	return (GLsizei)MaxLabelLength - 1;
}


void SetDebugLable(GLenum ObjectType, GLuint ObjectID, std::string DebugLabel)
{
	static GLsizei MaxLabelLength = GetMaxLabelLength();
	GLsizei DebugNameLength = min(DebugLabel.size(), MaxLabelLength);
	glObjectLabel(ObjectType, ObjectID, DebugNameLength, DebugLabel.c_str());
}


ShaderSource GeneratedShader(std::string PrePath, std::string Generated, std::string PostPath)
{
	std::vector<ShaderSource> Sources = {
		ShaderSource("defines.h", true),
		ShaderSource(PrePath, true),
		ShaderSource(Generated, false),
		ShaderSource(PostPath, true)
	};
	return ShaderSource(Sources);
}


std::string GetShaderInfoLog(GLuint ObjectId)
{
	GLint LogLength;
	glGetShaderiv(ObjectId, GL_INFO_LOG_LENGTH, &LogLength);
	if (LogLength)
	{
		std::string InfoLog(LogLength, 0);
		glGetShaderInfoLog(ObjectId, LogLength, nullptr, (char*)InfoLog.data());
		return InfoLog;
	}
	else
	{
		return std::string();
	}
}


std::string GetProgramInfoLog(GLuint ObjectId)
{
	GLint LogLength;
	glGetProgramiv(ObjectId, GL_INFO_LOG_LENGTH, &LogLength);
	if (LogLength)
	{
		std::string InfoLog(LogLength, 0);
		glGetProgramInfoLog(ObjectId, LogLength, nullptr, (char*)InfoLog.data());
		return InfoLog;
	}
	else
	{
		return std::string();
	}
}


bool IsPrepender(std::string Line, std::string& NextPath)
{
	const std::string Prefix = "prepend: ";
	if (Line.size() >= Prefix.size())
	{
		const std::string Test = Line.substr(0, Prefix.size());
		if (Test == Prefix)
		{
			NextPath = Line.substr(Prefix.size(), Line.size() - Prefix.size());
			return true;
		}
	}
	return false;
}


bool IsPerforation(std::string Line)
{
	// Matches ^----*$
	if (Line.size() < 3)
	{
		return false;
	}
	for (int i = 0; i < Line.size(); i++)
	{
		if (Line[i] != '-' && Line[i] != '\r')
		{
			return false;
		}
	}
	return true;
}


StatusCode FillSources(std::vector<std::string>& BreadCrumbs, std::vector<std::string>& Index, std::vector<std::string>& Sources, std::string Path)
{
	for (const auto& Visited : BreadCrumbs)
	{
		if (Path == Visited)
		{
			return StatusCode::PASS;
		}
	}
	BreadCrumbs.push_back(Path);

	std::filesystem::path FullPath = Installed.ShadersDir / Path;

	std::ifstream File(FullPath);
	if (!File.is_open())
	{
		std::cout << "Error: cannot open file \"" << Path << "\"\n";
		return StatusCode::FAIL;
	}
	std::string Line;
	std::string Source;

	// Scan for prepends
	bool bFoundPrepend = false;
	bool bFoundTear = false;
	int TearLine = -1;
	for (int LineNumber = 0; getline(File, Line); ++LineNumber)
	{
		std::string Detour;
		if (IsPerforation(Line))
		{
			bFoundTear = true;
			TearLine = LineNumber;
			break;
		}
		else if (IsPrepender(Line, Detour))
		{
			bFoundPrepend = true;
			RETURN_ON_FAIL(FillSources(BreadCrumbs, Index, Sources, Detour));
			continue;
		}
		else
		{
			break;
		}
	}

	if (bFoundPrepend && !bFoundTear)
	{
		std::cout << "Error in file \"" << Path << "\":\n";
		std::cout << "  Cannot use prepend statements without a perforated line.\n";
		return StatusCode::FAIL;
	}

	Index.push_back(Path);
	File.seekg(0);
	if (bFoundTear)
	{
		for (int LineNumber = 0; LineNumber <= TearLine; ++LineNumber)
		{
			getline(File, Line);
		}
		Source = "#line ";
		Source += std::to_string(TearLine + 1);
		Source += " ";
		Source += std::to_string(Index.size() - 1);
		Source += "\n";
	}
	else
	{
		Source = "#line 0 ";
		Source += std::to_string(Index.size() - 1);
		Source += "\n";
	}

	while (getline(File, Line))
	{
		Source += Line + '\n';
	}

	File.close();
	Sources.push_back(Source);
	return StatusCode::PASS;
}


StatusCode RouteSource(std::vector<std::string>& BreadCrumbs, std::vector<std::string>& Index, std::vector<std::string>& Sources, const ShaderSource& Source)
{
	if (Source.Mode == ShaderSource::Variant::PATH)
	{
		static std::map<std::string, std::vector<std::string>> Cache;
		std::vector<std::string>& CachedSources = Cache[Source.Source];
		if (CachedSources.empty())
		{
			std::vector<std::string> NewSources;
			RETURN_ON_FAIL(FillSources(BreadCrumbs, Index, NewSources, Source.Source));
			for (const auto& SourceString : NewSources)
			{
				CachedSources.push_back(SourceString);
				Sources.push_back(SourceString);
			}
		}
		else
		{
			for (const auto& SourceString : CachedSources)
			{
				Sources.push_back(SourceString);
			}
		}
		return StatusCode::PASS;
	}
	else if (Source.Mode == ShaderSource::Variant::STR)
	{
		Sources.push_back(Source.Source);
		Index.push_back("(unknown string source)");
		return StatusCode::PASS;
	}
	else if (Source.Mode == ShaderSource::Variant::LIST)
	{
		for (const ShaderSource& Page : Source.Composite)
		{
			RETURN_ON_FAIL(RouteSource(BreadCrumbs, Index, Sources, Page));
		}
		return StatusCode::PASS;
	}
	//no match?
	return StatusCode::FAIL;
}


const std::string GetShaderExtensionsGL4(GLenum ShaderType)
{
	std::string Version = "#version 420\n";

	static const std::string VertexExtensions = \
		"#extension GL_ARB_gpu_shader5 : require\n" \
		"#extension GL_ARB_shader_storage_buffer_object : require\n" \
		"#extension GL_ARB_shading_language_420pack : require\n";

	static const std::string TessellationExtensions = \
		"#extension GL_ARB_gpu_shader5 : require\n" \
		"#extension GL_ARB_shader_storage_buffer_object : require\n" \
		"#extension GL_ARB_shading_language_420pack : require\n";

	static const std::string FragmentExtensions = \
		"#extension GL_ARB_shader_storage_buffer_object : require\n" \
		"#extension GL_ARB_shader_image_load_store : require\n" \
		"#extension GL_ARB_gpu_shader5 : require\n" \
		"#extension GL_ARB_shading_language_420pack : require\n" \
		"#extension GL_ARB_fragment_coord_conventions : require\n";

	static const std::string ComputeExtensions = \
		"#extension GL_ARB_compute_shader : require\n" \
		"#extension GL_ARB_shader_storage_buffer_object : require\n" \
		"#extension GL_ARB_shader_image_load_store : require\n" \
		"#extension GL_ARB_gpu_shader5 : require\n" \
		"#extension GL_ARB_shading_language_420pack : require\n";

	static const std::string ShaderTypeMeta = \
		"#define VERTEX_SHADER " + std::to_string(GL_VERTEX_SHADER) + "\n" + \
		"#define TESS_CONTROL_SHADER " + std::to_string(GL_TESS_CONTROL_SHADER) + "\n" + \
		"#define TESS_EVALUATION_SHADER " + std::to_string(GL_TESS_EVALUATION_SHADER) + "\n" + \
		"#define GEOMETRY_SHADER " + std::to_string(GL_GEOMETRY_SHADER) + "\n" + \
		"#define FRAGMENT_SHADER " + std::to_string(GL_FRAGMENT_SHADER) + "\n" + \
		"#define COMPUTE_SHADER " + std::to_string(GL_COMPUTE_SHADER) + "\n";

	const std::string ShaderTypeDefine = ShaderTypeMeta + "#define SHADER_TYPE " + std::to_string(ShaderType) + "\n";

	switch (ShaderType)
	{
	case GL_VERTEX_SHADER:
		return Version + VertexExtensions + ShaderTypeDefine;

	case GL_FRAGMENT_SHADER:
		return Version + FragmentExtensions + ShaderTypeDefine;

	case GL_TESS_CONTROL_SHADER:
	case GL_TESS_EVALUATION_SHADER:
	case GL_GEOMETRY_SHADER:
		return Version + TessellationExtensions + ShaderTypeDefine;

	default:
		return Version + ComputeExtensions + ShaderTypeDefine;
	}
}


const std::string GetShaderExtensionsES2(GLenum ShaderType)
{
	std::string Version = "#version 100\n";

	std::string VertexPrecision = \
		"precision highp float;\n";

	std::string FragmentPrecision = \
		"#ifdef GL_FRAGMENT_PRECISION_HIGH\n" \
		"precision highp float;\n" \
		"#else\n" \
		"precision mediump float;\n" \
		"#endif\n";

	static const std::string ShaderTypeMeta = \
		"#define VERTEX_SHADER " + std::to_string(GL_VERTEX_SHADER) + "\n" + \
		"#define FRAGMENT_SHADER " + std::to_string(GL_FRAGMENT_SHADER) + "\n";

	const std::string ShaderTypeDefine = ShaderTypeMeta + "#define SHADER_TYPE " + std::to_string(ShaderType) + "\n";

	switch (ShaderType)
	{
	case GL_VERTEX_SHADER:
		return Version + VertexPrecision + ShaderTypeDefine;

	case GL_FRAGMENT_SHADER:
		return Version + FragmentPrecision + ShaderTypeDefine;

	default:
		return Version + ShaderTypeDefine;
	}
}


const std::string GetShaderExtensions(GLenum ShaderType)
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		return GetShaderExtensionsGL4(ShaderType);
	}
	else if (GraphicsBackend == GraphicsAPI::OpenGLES2)
	{
		return GetShaderExtensionsES2(ShaderType);
	}
	else
	{
		return "";
	}
}


StatusCode CompileShader(GLuint ShaderID, GLuint ProgramID, GLenum ShaderType, const ShaderSource& InputSource, std::vector<std::string>& Sources, std::vector<std::string>& Index)
{

	const std::string Extensions = GetShaderExtensions(ShaderType);
	Sources.push_back(Extensions);
	Index.push_back("(generated block)");

	{
		std::vector<std::string> BreadCrumbs;
		static std::mutex RouteSourceCS;
		RouteSourceCS.lock();
		StatusCode Status = RouteSource(BreadCrumbs, Index, Sources, InputSource);
		RouteSourceCS.unlock();
		RETURN_ON_FAIL(Status);
	}

	GLsizei StringCount = (GLsizei)Sources.size();
	std::vector<const char*> Strings;
	Strings.reserve(StringCount);
	for (std::string& Source : Sources)
	{
		Strings.push_back(Source.c_str());
	}

	glShaderSource(ShaderID, StringCount, Strings.data(), nullptr);
	glCompileShader(ShaderID);

	return StatusCode::PASS;
}


GLuint ShaderModeBit(GLenum ShaderMode)
{
	if (ShaderMode == GL_VERTEX_SHADER) return GL_VERTEX_SHADER_BIT;
	else if (ShaderMode == GL_TESS_CONTROL_SHADER) return GL_TESS_CONTROL_SHADER_BIT;
	else if (ShaderMode == GL_TESS_EVALUATION_SHADER) return GL_TESS_EVALUATION_SHADER_BIT;
	else if (ShaderMode == GL_GEOMETRY_SHADER) return GL_GEOMETRY_SHADER_BIT;
	else if (ShaderMode == GL_FRAGMENT_SHADER) return GL_FRAGMENT_SHADER_BIT;
	else if (ShaderMode == GL_COMPUTE_SHADER) return GL_COMPUTE_SHADER_BIT;
	else return 0;
}


void ShaderProgram::AsyncSetup(std::map<GLenum, ShaderSource> InShaders, const char* InProgramName)
{
	Reset();
	Shaders = InShaders;
	ProgramName = std::string(InProgramName);
}


StatusCode ShaderProgram::Setup(std::map<GLenum, ShaderSource> InShaders, const char* InProgramName)
{
	AsyncSetup(InShaders, InProgramName);

	StatusCode Result = Compile();
	if (FAILED(Result))
	{
		Reset();
	}

	return Result;
}


StatusCode ShaderProgram::Compile()
{
	ProgramID = glCreateProgram();
	SetDebugLable(GL_PROGRAM, ProgramID, ProgramName);

	StatusCode Result = StatusCode::PASS;
	std::vector<CompileInfo> CompileJobs;
	std::vector<GLuint> Intermediaries;

	for (const auto& Shader : Shaders)
	{
		GLenum ShaderType = Shader.first;

		GLuint ShaderID = glCreateShader(ShaderType);
		glAttachShader(ProgramID, ShaderID);
		Intermediaries.push_back(ShaderID);

		CompileInfo CompileJob;
		CompileJob.ShaderID = ShaderID;
		Result = CompileShader(ShaderID, ProgramID, ShaderType, Shader.second, CompileJob.Sources, CompileJob.Index);
		if (Result == StatusCode::FAIL)
		{
			for (GLuint ShaderID : Intermediaries)
			{
				glDeleteShader(ShaderID);
			}
			Reset();
			return StatusCode::FAIL;
		}
		else
		{
			CompileJobs.push_back(CompileJob);
		}
	}
	glLinkProgram(ProgramID);

	for (CompileInfo& Shader : CompileJobs)
	{
		GLint CompileStatus;
		BeginEvent("glGetShaderiv GL_COMPILE_STATUS");
		glGetShaderiv(Shader.ShaderID, GL_COMPILE_STATUS, &CompileStatus);
		EndEvent();

		if (CompileStatus == GL_FALSE)
		{
			std::cout << "\n\n################################################################\n";
			for (std::string& Source : Shader.Sources)
			{
				std::cout << Source << "\n";
				std::cout << "################################################################\n";
			}
			std::cout << "Shader string paths:\n";
			int i = 0;
			for (std::string& Index : Shader.Index)
			{
				std::cout << ++i << " -> " << Index << "\n";
			}

			Result = StatusCode::FAIL;
			std::string Error = GetShaderInfoLog(Shader.ShaderID);
			std::cout << "\n" << Error << '\n';
		}
	}

	if (Result == StatusCode::PASS)
	{
		GLint LinkStatus;
		BeginEvent("glGetProgramiv GL_LINK_STATUS");
		glGetProgramiv(ProgramID, GL_LINK_STATUS, &LinkStatus);
		EndEvent();

		if (LinkStatus == GL_FALSE)
		{
			Result = StatusCode::FAIL;
			std::string Error = GetProgramInfoLog(ProgramID);
			std::cout << "\n" << Error << '\n';
		}
	}

	if (Result == StatusCode::FAIL)
	{
		Reset();
	}
	else
	{
		for (CompileInfo& Shader : CompileJobs)
		{
			glDetachShader(ProgramID, Shader.ShaderID);
		}
		CompileJobs.clear();
	}

	for (GLuint ShaderID : Intermediaries)
	{
		glDeleteShader(ShaderID);
	}

	return Result;
}


void ShaderProgram::Activate()
{
	glUseProgram(ProgramID);
}


void ShaderProgram::Reset()
{
	if (ProgramID != 0)
	{
		glDeleteProgram(ProgramID);
		ProgramID = 0;
	}
}


Buffer::Buffer(Buffer&& OldBuffer)
	: BufferID(OldBuffer.BufferID)
	, LastSize(OldBuffer.LastSize)
	, DebugName(OldBuffer.DebugName)
{
	OldBuffer.BufferID = 0;
	OldBuffer.LastSize = 0;
}


Buffer::Buffer(const char* InDebugName)
	: BufferID(0)
	, LastSize(0)
	, DebugName(InDebugName)
{
}


Buffer::~Buffer()
{
	Release();
}


void Buffer::Release()
{
	if (BufferID != 0)
	{
		glDeleteBuffers(1, &BufferID);
		BufferID = 0;
	}
}


void Buffer::Reserve(size_t Bytes)
{
	Upload(nullptr, Bytes);
}


void Buffer::Upload(void* Data, size_t Bytes)
{
	if (Bytes != LastSize)
	{
		Release();
	}
	if (BufferID == 0)
	{
		glCreateBuffers(1, &BufferID);
		if (DebugName != nullptr)
		{
			SetDebugLable(GL_BUFFER, BufferID, DebugName);
		}
		glNamedBufferStorage(BufferID, Bytes, Data, GL_DYNAMIC_STORAGE_BIT);
		LastSize = Bytes;
	}
	else
	{
		glNamedBufferSubData(BufferID, 0, Bytes, Data);
	}
}


void Buffer::Upload(GLenum Target, GLenum Usage, void* Data, size_t Bytes)
{
	if (Bytes != LastSize)
	{
		Release();
	}
	if (BufferID == 0)
	{
		glGenBuffers(1, &BufferID);
		glBindBuffer(Target, BufferID);
		glBufferData(Target, Bytes, Data, Usage);
		glBindBuffer(Target, 0);
		LastSize = Bytes;
	}
	else
	{
		Bind(Target);
		glBindBuffer(Target, BufferID);
		glBufferSubData(Target, 0, Bytes, Data);
		glBindBuffer(Target, 0);
	}
}


void Buffer::Bind(GLenum Target, GLuint BindingIndex)
{
	glBindBufferBase(Target, BindingIndex, BufferID);
}


void Buffer::Bind(GLenum Target)
{
	glBindBuffer(Target, BufferID);
}


void TimingQuery::Create(size_t SampleCount)
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		Release();
		glGenQueries(1, &QueryID);
		Samples.resize(SampleCount);
		for (double& Sample : Samples)
		{
			Sample = 0.0;
		}
	}
}


void TimingQuery::Release()
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		if (QueryID != 0)
		{
			glDeleteQueries(1, &QueryID);
			QueryID = 0;
		}
	}
}


void TimingQuery::Start()
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		glBeginQuery(GL_TIME_ELAPSED, QueryID);
		Pending = true;
	}
}


void TimingQuery::Stop()
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		glEndQuery(GL_TIME_ELAPSED);
	}
}


double TimingQuery::ReadMs()
{
	if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
	{
		double TimeMs = 0.0;

		if (Pending)
		{
			Pending = false;
			GLint TimeNs = 0;
			glGetQueryObjectiv(QueryID, GL_QUERY_RESULT, &TimeNs);
			Samples[Cursor] = double(TimeNs) / 1000000.0;
			Cursor = ++Cursor % Samples.size();
		}

		for (const double& Sample : Samples)
		{
			TimeMs += Sample;
		}

		return TimeMs / double(Samples.size());
	}
	else
	{
		return 0.0;
	}
}
