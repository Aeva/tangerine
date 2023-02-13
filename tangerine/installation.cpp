
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
#if !(defined(TANGERINE_WINDOWS) || EMBED_RACKET)
# include <unistd.h>
#endif

std::filesystem::path tangerine_get_self_exe_path(char* argv0)
{
#if defined(TANGERINE_WINDOWS)
        // Is this really right? Should it use GetModuleFileNameW?
        return std::filesystem::absolute(argv0);
#elif EMBED_RACKET
        return std::filesystem::path(racket_get_self_exe_path(argv0));
#else
        // limited fallback support for linux
        char *s;
        ssize_t len, blen = 256;

        s = (char*)malloc(blen);

        while (1) {
                len = readlink("/proc/self/exe", s, blen-1);
                if (len == (blen-1)) {
                        free(s);
                        blen *= 2;
                        s = (char*)malloc(blen);
                } else if (len < 0) {
                        /* possibly in a chroot environment where "/proc" is not
                           available, so fall back to generic approach: */
                        free(s);
                        return std::filesystem::absolute(argv0);
                } else
                        break;
        }
        s[len] = 0;
        std::filesystem::path ret = std::filesystem::path(s);
        free(s);
        return ret;
#endif
};
