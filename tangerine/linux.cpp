
// Copyright 2024 Aeva Palecek
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

#include "linux.h"
#include <fmt/format.h>

#include <stdlib.h>
#include <array>
#include <regex>


template<size_t BufferSize = 512>
int ShellOut(const std::string Command, std::string& Output)
{
	FILE* Pipe = popen(Command.c_str(), "r");
	if (Pipe)
	{
		std::array<char, BufferSize> Buffer;
		while (fgets(Buffer.data(), BufferSize, Pipe) != nullptr)
		{
			Output += Buffer.data();
		}
		return pclose(Pipe);
	}
	else
	{
		return -1;
	}
}


void Search(std::string Sequence, const char* Pattern, int& Result)
{
	std::regex Regex(Pattern, std::regex_constants::multiline);
	std::smatch Match;
	if (std::regex_search(Sequence, Match, Regex) && !Match.empty())
	{
		Result = std::atoi(Match[1].str().c_str());
	}
}


StatusCode GetFontPath(std::string FontFileName, std::string& OutFontPath)
{
	OutFontPath = "";

	std::string CommandOutput;
	int StatusCode = ShellOut("fc-list", CommandOutput);
	if (StatusCode != 0)
	{
		fmt::print("fc-list not found, falling back to default font.\n");
		return StatusCode::FAIL;
	}

	std::regex Regex(fmt::format("(^.+?{}):.*$", FontFileName), std::regex_constants::multiline);
	std::smatch Match;
	if (std::regex_search(CommandOutput, Match, Regex) && !Match.empty())
	{
		OutFontPath = Match[1].str();
		return StatusCode::PASS;
	}

	// No match?
	return StatusCode::FAIL;
}


StatusCode MatchFontInner(std::string Pattern, std::string Suffix, std::string& OutFontPath)
{
	OutFontPath = "";

	std::string CommandOutput;
	int StatusCode = ShellOut(fmt::format("fc-match -s \"{}\"", Pattern), CommandOutput);
	if (StatusCode != 0)
	{
		fmt::print("fc-match not found, falling back to default font.\n");
		return StatusCode::FAIL;
	}

	std::regex Regex(fmt::format("^(.+?{}):.*$", Suffix), std::regex_constants::multiline);
	std::smatch Match;
	if (std::regex_search(CommandOutput, Match, Regex) && !Match.empty())
	{
		return GetFontPath(Match[1].str(), OutFontPath);
	}

	// Should be unreachable, but not the end of the world.
	return StatusCode::FAIL;
}


StatusCode Linux::MatchFont(std::vector<std::string> Patterns, std::string& OutFontPath)
{
	std::string DefaultFont = "";
	if (MatchFontInner("", "", DefaultFont) == StatusCode::FAIL)
	{
		return StatusCode::FAIL;
	}

	OutFontPath = DefaultFont;

	for (std::string& Pattern : Patterns)
	{
		std::string Candidate;
#if 0
		// Passing Cantarell into DearImGui causes a segfault, so something with .otf file handling is broken somewhere.
		const std::string Suffix = "(otf|ttf)";
#else
		const std::string Suffix = "ttf";
#endif
		if (MatchFontInner(Pattern, Suffix, Candidate) == StatusCode::PASS)
		{
			if (Candidate != DefaultFont)
			{
				OutFontPath = Candidate;
				break;
			}
		}
	}
	return StatusCode::PASS;
}


void Linux::SetEnvironmentVariable(const char* Name, const char* Value, bool Overwrite)
{
	setenv(Name, Value, Overwrite);
}
