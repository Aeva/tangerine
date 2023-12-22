
# Copyright 2022 Aeva Palecek
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os.path, glob


rewrite = {
}


template = """
#if {}{}
{{
	if (ImGui::BeginTabItem("{}"))
	{{
		const char* LicenseText = \\{};
		ImGui::TextUnformatted(LicenseText, nullptr);
		ImGui::EndTabItem();
	}}
}}
#endif
"""


def splat_license(project, path):
    build_condition = "1"
    run_condition = ""

    if project.lower().startswith("racket"):
        build_condition = "EMBED_RACKET"

    elif project.lower().startswith("lua"):
        build_condition = "EMBED_LUA"

    elif project.lower().startswith("freetype"):
        build_condition = "defined(ENABLE_RMLUI)"

    elif project.lower().startswith("rmlui"):
        build_condition = "defined(ENABLE_RMLUI)"

    elif project.lower().startswith("psmove"):
        build_condition = "ENABLE_PSMOVE_BINDINGS"
        run_condition = "PSMoveAvailable()"

    if len(run_condition) > 0:
        run_condition = f"\nif ({run_condition})"

    with open(path, "r") as INFILE:
        license = INFILE.read()
        license = license.replace("\r", "")
        parts = [i + "\\n" for i in license.split("\n")]
        parts = [i.replace("\"", "\\\"") for i in parts]
        text = ""
        for part in parts:
            text += '\n		"{}"'.format(part)

        return template.format(build_condition, run_condition, project, text)


if __name__ == "__main__":
    paths = []
    variants = ["LICENSE", "FTL", "copying"]
    for variant in variants:
        paths += glob.glob(f"*/{variant}*")

    projects = []
    for path in paths:
        name = os.path.split(path)[0]
        name = rewrite.get(name, name)
        projects.append((name, path))

    projects.sort(key=lambda p: p[0].lower())

    inl = """
// This file was generated by a script, and contains code for displaying
// open source license information via DearIMGUI.  This contains both
// the license text for Tangerine, and Tangerine's third party dependencies.
"""

    inl += splat_license("Tangerine", "../LICENSE.txt")
    for name, path in projects:
        inl += splat_license(name, path)

    with open("licenses.inl", "w") as OUTFILE:
        OUTFILE.write(inl)
