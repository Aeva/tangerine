
// Copyright 2023 Philip McGrath
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

#include "installation.h"
#include "whereami.h"
#include <iostream>

#if !_WIN64
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <cerrno>
#include <cstring>
const char *const TangerineAppID =
	// TODO: consider a reverse-dns name with the escaping recommended in:
	// https://docs.gtk.org/gio/type_func.Application.id_is_valid.html
	// See rationale in: https://docs.gtk.org/gtk4/migrating-3to4.html#set-a-proper-application-id
	"tangerine";
std::optional<std::filesystem::path> GetXDGStateHome();
#endif


StatusCode TangerinePaths::PopulateInstallationPaths()
{
	std::filesystem::path ExecutablePath;
	{
		int Length = wai_getExecutablePath(NULL, 0, NULL);
		if (Length > -1)
		{
			char* Path = (char*)malloc(Length + 1);
			wai_getExecutablePath(Path, Length, NULL);
			Path[Length] = '\0';
			ExecutablePath = std::filesystem::path(Path);
			free(Path);
		}
		else
		{
#if !_WIN64
			// TODO: Right now our platform coverage is "Windows" and "not Windows == Linux".  More nuance may be needed here in the future.
			if (!std::filesystem::exists("/proc"))
			{
				std::cout << "Proc filesystem not found.\n";
			}
#endif
			std::cout << "Failed to determine Tangerine's filesystem path.\n";
			return StatusCode::FAIL;
		}
	}

	std::filesystem::path ExecutableDir = ExecutablePath.parent_path();

#ifdef TANGERINE_PKGDATADIR_FROM_BINDIR
#define STRINGIFY(x) #x
#define EXPAND_AS_STR(x) STRINGIFY(x)
	std::filesystem::path PkgDataDir = ExecutableDir / std::filesystem::path(EXPAND_AS_STR(TANGERINE_PKGDATADIR_FROM_BINDIR));
#undef EXPAND_AS_STR
#undef STRINGIFY
#else
	std::filesystem::path PkgDataDir = ExecutableDir;
#endif

	ShadersDir = PkgDataDir / std::filesystem::path("shaders");
	ModelsDir = PkgDataDir / std::filesystem::path("models");

#if defined(TANGERINE_SELF_CONTAINED)
	BookmarksPath = PkgDataDir / std::filesystem::path("bookmarks.txt");
#elif !_WIN64
	if (std::optional<std::filesystem::path> HomeDir = GetXDGStateHome())
	{
		BookmarksPath = HomeDir.value() / std::filesystem::path(TangerineAppID) / std::filesystem::path("bookmarks.txt");
	}
	else
	{
		BookmarksPath = std::nullopt;
	}
#else // Shouldn't get here: handled in "installation.h".  Using %APPDATA% / CSIDL_APPDATA / FOLDERID_RoamingAppData might be useful, though.
# error "Windows currently requires TANGERINE_SELF_CONTAINED."
#endif

	return StatusCode::PASS;
}


#if !_WIN64
std::optional<std::filesystem::path> GetHomeDir() {
	// Based on `rktio_expand_user_tilde()`.
	// License: (Apache-2.0 OR MIT)

	// $HOME overrides everything.
	if (const char *Home = std::getenv("HOME"))
	{
		return std::filesystem::path(Home);
	}

	// $USER and $LOGNAME (in that order) override `getuid()`.
	const char *AltUserVar = "USER";
	const char *AltUser = std::getenv(AltUserVar);
	if (!AltUser)
	{
		AltUserVar = "LOGNAME";
		AltUser = std::getenv(AltUserVar);
	}

	/* getpwnam(3) man page says: "If one wants to check errno after the
	   call, it should be set to zero before the call." */
	errno = 0;
	struct passwd *Passwd = AltUser ? getpwnam(AltUser) : getpwuid(getuid());
	int PasswdError = errno;

	// Did we find it?
	if (Passwd && Passwd->pw_dir)
	{
		if (0 != PasswdError)
		{
			std::cout << "Warning: Found home directory, but " << (AltUser ? "getpwnam" : "getpwuid") << " reported an error.\n";
		}
		else
		{
			// No warning
			return std::filesystem::path(Passwd->pw_dir);
		}
	}
	else if (Passwd)
	{
		std::cout << "Warning: User exists, but does not have a home directory.\n";
	}
	else
	{
		std::cout << "Warning: Could not find home directory: user not found.\n";
	}

	// Add warning details:
	// Was `getuid()` overridden?
	if (AltUser)
	{
		std::cout << "   user: " << AltUser << " (from $" << AltUserVar << ");\n";
	}
	// Report system error.
	if (0 != PasswdError)
	{
		std::cout << "  error: " << std::strerror(PasswdError) << "\n";
		std::cout << "  errno: " << PasswdError << "\n";
	}
	else
	{
		std::cout << "  errno: not set by " << (AltUser ? "getpwnam" : "getpwuid") << "\n";
	}

	if (Passwd && Passwd->pw_dir)
	{
		return std::filesystem::path(Passwd->pw_dir);
	}
	else
	{
		return {};
	}
}

std::optional<std::filesystem::path> GetXDGStateHome()
{
	// Based on `rktio_system_path()`.
	// License: (Apache-2.0 OR MIT)

	const char *EnvVar = "XDG_STATE_HOME";
	const char *DefaultSubpath = ".local/state";

	// Check the environment variable.
	if (const char *FromEnv = std::getenv(EnvVar))
	{
		std::filesystem::path Candidate = std::filesystem::path(FromEnv);
		// We must ignore the environment variable if it is not an absolute path.
		if (Candidate.is_absolute()) {
			return Candidate;
		}
	}


	// Environment variable was unset or is invalid.
	if (std::optional<std::filesystem::path> Home = GetHomeDir())
	{
		return Home.value() / std::filesystem::path(DefaultSubpath);
	}
	else
	{
		return {};
	}
}
#endif /* !_WIN64 */
