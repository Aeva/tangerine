
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

#include <imgui.h>
#include "license_page.h"
#include "embedding.h"
#include "psmove_loader.h"


void LicenseDisclosuresWindow(bool& ShowLicenses)
{
	if (ShowLicenses)
	{
		int Margin = 0;
		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(MainViewport->WorkPos.x + Margin, MainViewport->WorkPos.y + Margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(MainViewport->WorkSize.x - Margin * 2, MainViewport->WorkSize.y - Margin * 2), ImGuiCond_Always);

		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_AlwaysVerticalScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;

		if (ImGui::Begin("Open Source Licenses", &ShowLicenses, WindowFlags))
		{
			ImGuiTabBarFlags TabBarFlags = ImGuiTabBarFlags_None;
			if (ImGui::BeginTabBar("Open Source Licenses", TabBarFlags))
			{
#include "../third_party/licenses.inl"
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}
}
