
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

#include <mutex>
#include <string>
#include "fmt/format.h"
#include "gl_debug.h"


std::mutex DebugCS;


std::string SourceString(GLenum Source)
{
	switch (Source)
	{
	case GL_DEBUG_SOURCE_API:
		return "API";
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
		return "Window System";
	case GL_DEBUG_SOURCE_SHADER_COMPILER:
		return "Shader Compiler";
	case GL_DEBUG_SOURCE_THIRD_PARTY:
		return "Third Party";
	case GL_DEBUG_SOURCE_APPLICATION:
		return "Source Application";
	case GL_DEBUG_SOURCE_OTHER:
		return "Other";
	default:
		return "Unknown";
	}
}


std::string TypeString(GLenum Type)
{
	switch (Type)
	{
	case GL_DEBUG_TYPE_ERROR:
		return "Error";
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		return "Deprecated Behavior";
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		return "Undefined Behavior";
	case GL_DEBUG_TYPE_PORTABILITY:
		return "Portability";
	case GL_DEBUG_TYPE_PERFORMANCE:
		return "Performance";
	case GL_DEBUG_TYPE_OTHER:
		return "Other";
	case GL_DEBUG_TYPE_MARKER:
		return "Marker";
	case GL_DEBUG_TYPE_PUSH_GROUP:
		return "Push Group";
	case GL_DEBUG_TYPE_POP_GROUP:
		return "Pop Group";
	default:
		return "Unknown";
	}
}


std::string SeveritySring(GLenum Severity)
{
	switch (Severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:
		return "High";
	case GL_DEBUG_SEVERITY_MEDIUM:
		return "Medium";
	case GL_DEBUG_SEVERITY_LOW:
		return "Low";
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		return "Notification";
	default:
		return "Unknown";
	}
}


std::string ThreadName(size_t Thread)
{
	if (Thread == 0)
	{
		return "Main";
	}
	else
	{
		return fmt::format("Worker {}", Thread);
	}
}


void DebugCallback(
	GLenum Source,
	GLenum Type,
	GLuint Id,
	GLenum Severity,
	GLsizei MessageLength,
	const GLchar* ErrorMessage,
	const void* UserParam)
{
	std::lock_guard<std::mutex> ScopedLock(DebugCS);
	fmt::print("[Context: {}] {} {} {} {}: {}\n",
		ThreadName((size_t)UserParam),
		SourceString(Source),
		TypeString(Type),
		Id,
		SeveritySring(Severity),
		ErrorMessage);
}


void ConnectDebugCallback(size_t Thread)
{
#if ENABLE_DEBUG_CONTEXTS
	glDebugMessageCallback(DebugCallback, (void*)Thread);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, true);
	glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, nullptr, false);
	glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 0, nullptr, false);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, false);
#endif
}
