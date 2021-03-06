
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


template = """
#if {}
if (ImGui::BeginTabItem("{}"))
{{
	ImGui::TextUnformatted("{}", nullptr);
	ImGui::EndTabItem();
}}
#endif
"""


def splat_license(project, path):
    if project.lower().startswith("racket"):
        condition = "EMBED_RACKET"
    elif project.lower().startswith("lua"):
        condition = "EMBED_LUA"
    else:
        condition = "1"
    with open(path, "r") as INFILE:
        license = INFILE.read()
        license = license.replace("\r", "")
        license = license.replace("\n", "\\n")
        license = license.replace("\"", "\\\"")
        return template.format(condition, project, license)


if __name__ == "__main__":
    paths = []
    variants = ["LICENSE", "copying"]
    for variant in variants:
        paths += glob.glob(f"*/{variant}*")

    projects = []
    for path in paths:
        name = os.path.split(path)[0]
        projects.append((name, path))
    projects.sort()

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
