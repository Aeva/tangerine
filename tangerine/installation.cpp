
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

#if !_WIN64
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#endif

#if EMBED_RACKET
#include <chezscheme.h>
#include <racketcs.h>
#endif

namespace fs = std::filesystem;

#if !_WIN64
const char *const tangerine_app_id =
	// TODO: consider a reverse-dns name with the escaping recommended in:
	// https://docs.gtk.org/gio/type_func.Application.id_is_valid.html
	// See rationale in: https://docs.gtk.org/gtk4/migrating-3to4.html#set-a-proper-application-id
	"tangerine";
std::optional<fs::path> tangerine_get_xdg_state_home();
#endif

TangerinePaths::TangerinePaths(int argc, char* argv[])
{
	fs::path ExecutablePath;
#if _WIN64
	// TODO: This is fine for standalone builds, but we will want to do something
	// else for future library builds.  Maybe GetModuleFileNameW?
	ExecutablePath = fs::absolute(argv[0]);

#elif EMBED_RACKET
	// On Windows, `racket_get_self_exe_path()` returns UTF-8.
	ExecutablePath = fs::path(racket_get_self_exe_path(argv[0]));

#else
	// The happy path is based on `racket_get_self_exe_path()`.
	// License: (Apache-2.0 OR MIT)
	{
		char* Path = nullptr;
		ssize_t PathLength = 0;
		ssize_t Allocation = 256;

retry:
		Path = (char*)malloc(Allocation);
		PathLength = readlink("/proc/self/exe", Path, Allocation - 1);
		if (PathLength == (Allocation - 1))
		{
			free(Path);
			Allocation *= 2;
			goto retry;
		}

		if (PathLength < 0)
		{
			// Possibly in a chroot environment where "/proc" is not available, so fall back to generic approach.
			ExecutablePath = fs::absolute(argv[0]);
		}
		else
		{
			Path[PathLength] = 0;
			ExecutablePath = fs::path(Path);
		}

		free(Path);
	}
#endif

	fs::path ExecutableDir = ExecutablePath.parent_path();

#ifdef TANGERINE_PKGDATADIR_FROM_BINDIR
#define STRINGIFY(x) #x
#define EXPAND_AS_STR(x) STRINGIFY(x)
	fs::path PkgDataDir = ExecutableDir / fs::path(EXPAND_AS_STR(TANGERINE_PKGDATADIR_FROM_BINDIR));
#undef EXPAND_AS_STR
#undef STRINGIFY
#else
	fs::path PkgDataDir = ExecutableDir;
#endif

	ShadersDir = PkgDataDir / fs::path("shaders");
	ModelsDir = PkgDataDir / fs::path("models");

#if defined(TANGERINE_SELF_CONTAINED)
	BookmarksPath = PkgDataDir / fs::path("bookmarks.txt");
#elif !_WIN64
	if (std::optional<fs::path> dir = tangerine_get_xdg_state_home())
	{
		BookmarksPath = dir.value() / fs::path(tangerine_app_id) / fs::path("bookmarks.txt");
	}
	else
	{
		BookmarksPath = std::nullopt;
	}
#else // shouldn't get here: handled in "installation.h"
# error "Windows currently requires TANGERINE_SELF_CONTAINED."
	// Using %APPDATA% / CSIDL_APPDATA / FOLDERID_RoamingAppData might be useful, though
#endif
}


#if !_WIN64
std::optional<fs::path> tangerine_get_home_dir() {
	// Based on `rktio_expand_user_tilde()`.
	// License: (Apache-2.0 OR MIT)

	// $HOME overrides everything.
	if (const char *home = std::getenv("HOME"))
	{
		return fs::path(home);
	}

	// $USER and $LOGNAME (in that order) override `getuid()`.
	const char *alt_user_var = "USER";
	const char *alt_user = std::getenv(alt_user_var);
	if (!alt_user)
	{
		alt_user_var = "LOGNAME";
		alt_user = std::getenv(alt_user_var);
	}

	/* getpwdnam(3) man page says: "If one wants to check errno after the
	   call, it should be set to zero before the call." */
	errno = 0;
	struct passwd* info =
		alt_user ? getpwnam(alt_user) : getpwuid(getuid());
	int info_error = errno;

	// Did we find it?
	if (info && info->pw_dir)
	{
		if (0 == info_error)
		{
			std::cout << "Warning: Found home directory, but ";
			std::cout << (alt_user ? "getpwnam" : "getpwuid");
			std::cout << " reported an error.\n";
		}
		else
		{
			// No warning
			return fs::path(info->pw_dir);
		}
	}
	else if (info)
	{
		std::cout << "Warning: User exists, but does not have a home directory.\n";
	}
	else
	{
		std::cout << "Warning: Could not find home directory: user not found.\n";
	}

	// Add warning details:
	// Was `getuid()` overridden?
	if (alt_user)
	{
		std::cout << "   user: " << alt_user << " (from $" << alt_user_var << ");\n";
	}
	// Report system error.
	if (0 == info_error)
	{
		std::cout << "  error: " << std::strerror(info_error) << "\n";
		std::cout << "  errno: " << info_error << "\n";
	}
	else
	{
		std::cout << "  errno: not set by ";
		std::cout << (alt_user ? "getpwnam" : "getpwuid");
		std::cout << "\n";
	}

	if (info && info->pw_dir)
	{
		return fs::path(info->pw_dir);
	}
	else
	{
		return {};
	}
}

std::optional<fs::path> tangerine_get_xdg_state_home()
{
	// Based on `rktio_system_path()`.
	// License: (Apache-2.0 OR MIT)

	const char *envvar = "XDG_STATE_HOME";
	const char *default_subpath = ".local/state";

	// Check the environment variable.
	if (const char *from_env = std::getenv(envvar))
	{
		fs::path candidate = fs::path(from_env);
		/* We must ignore the environment variable if it is not an
		   absolute path. */
		if (candidate.is_absolute()) {
			return candidate;
		}
	}


	// Environment variable was unset or is invalid.
	if (std::optional<fs::path> home = tangerine_get_home_dir())
	{
		return home.value() / fs::path(default_subpath);
	}
	else
	{
		return {};
	}
}
#endif /* !_WIN64 */
