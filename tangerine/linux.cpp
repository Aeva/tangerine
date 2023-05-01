
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

#include "linux.h"
#include "errors.h"

#include <stdlib.h>
#include <string>
#include <array>
#include <regex>
#include <iostream>


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

void Linux::DriverCheck(bool RequestSoftwareDriver)
{
#if 1
	if (!RequestSoftwareDriver)
	{
		std::string Found = "";
		ShellOut("which glxinfo", Found);

		if (Found.size() > 0)
		{
			std::string DeviceInfo = "";
			ShellOut("glxinfo -B", DeviceInfo);

			int CoreMajorVersion = 99;
			Search(DeviceInfo, "Max core profile version: (\\d+)\\.\\d+", CoreMajorVersion);

			int CoreMinorVersion = 99;
			Search(DeviceInfo, "Max core profile version: \\d+\\.(\\d+)", CoreMinorVersion);

			int CompatMajorVersion = 99;
			Search(DeviceInfo, "Max compat profile version: (\\d+)\\.\\d+", CompatMajorVersion);

			int CompatMinorVersion = 99;
			Search(DeviceInfo, "Max compat profile version: \\d+\\.(\\d+)", CompatMinorVersion);

			int MajorVersion = std::max(CoreMajorVersion, CompatMajorVersion);
			int MinorVersion = std::max(CoreMinorVersion, CompatMinorVersion);

			if (MajorVersion < 4)
			{
				RequestSoftwareDriver = true;
			}
			else if (MajorVersion == 4 && MinorVersion < 2)
			{
				RequestSoftwareDriver = true;
			}

			// TODO: Also check for missing extensions.

			if (RequestSoftwareDriver)
			{
				std::cout
					<< "The maximum OpenGL version supported by the system is "
					<< MajorVersion << "." << MinorVersion << ", but OpenGL 4.2\n"
					<< "or later is required for this program to run.  The \"llvmpipe\" fallback\n"
					<< "driver will be used instead if it is available.\n\n";
			}
		}
	}
#endif

	if (RequestSoftwareDriver)
	{
		char* Env = (char*)"LIBGL_ALWAYS_SOFTWARE=1";
		putenv(Env);
	}
}
