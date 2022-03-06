
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

#include <functional>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>
#include <imgui_impl_vulkan.h>
#include <fmt/format.h>

#include "hephaestus.h"

#pragma warning( disable : 26819 )


#define ENABLE_VALIDATION 1


struct VkContext : public VkWindow
{
	std::vector<std::function<void(void)> > ShutdownTasks;
	bool Complete;
	SDL_Window* Window;
	vk::Instance Instance;
	vk::PhysicalDevice Adapter;
	vk::PhysicalDeviceProperties AdapterProperties;
	vk::PhysicalDeviceMemoryProperties AdapterMemoryProperties;
	vk::SurfaceKHR Surface;
	vk::SurfaceFormatKHR SurfaceFormat;
	int32_t QueueFamilyIndex = -1;
	vk::Device Device;


	VkContext()
		: Complete(false)
		, Window(nullptr)
	{
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) == 0)
		{
			Window = SDL_CreateWindow(
				"Tangerine",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				900, 900,
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

			ShutdownTasks.push_back([&]()
			{
				SDL_DestroyWindow(Window);
			});
		}
		else
		{
			fmt::print("SDL failed to initialize: {}\n", SDL_GetError());
			return;
		}

		{
			vk::ApplicationInfo AppInfo("Tangerine", 0, "Tangerine", 0, VK_API_VERSION_1_3);

			std::vector<const char*> ValidationLayers = \
			{
#if ENABLE_VALIDATION
				"VK_LAYER_KHRONOS_validation",
#endif
			};

			std::vector<const char*> InstanceExtensionNames;
			{
				uint32_t Count;
				SDL_Vulkan_GetInstanceExtensions(Window, &Count, nullptr);
				InstanceExtensionNames.resize(Count);
				SDL_Vulkan_GetInstanceExtensions(Window, &Count, InstanceExtensionNames.data());
			}

			vk::InstanceCreateInfo CreateInfo({}, &AppInfo, ValidationLayers, InstanceExtensionNames);
			Instance = vk::createInstance(CreateInfo);

			ShutdownTasks.push_back([&]()
			{
				Instance.destroy();
			});
		}

		{
			auto Adapters = Instance.enumeratePhysicalDevices();
			if (Adapters.size() == 0)
			{
				fmt::print("No GPUs found.\n");
				return;
			}
			else
			{
				// Use the first available discrete adapter.
				for (vk::PhysicalDevice& Candidate : Adapters)
				{
					AdapterProperties = Candidate.getProperties();
					if (AdapterProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
					{
						Adapter = Candidate;
						goto FoundAdapter;
					}
				}
				// Or the first available integrated adapter.
				for (vk::PhysicalDevice& Candidate : Adapters)
				{
					AdapterProperties = Candidate.getProperties();
					if (AdapterProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
					{
						Adapter = Candidate;
						goto FoundAdapter;
					}
				}
				// Yolo.
				Adapter = Adapters[0];
				AdapterProperties = Adapter.getProperties();

			FoundAdapter:
				AdapterMemoryProperties = Adapter.getMemoryProperties();
			}
		}

		{
			VkSurfaceKHR SDLSurface;
			if (SDL_Vulkan_CreateSurface(Window, Instance, &SDLSurface))
			{
				Surface = SDLSurface;
				ShutdownTasks.push_back([&]()
				{
					Instance.destroySurfaceKHR(Surface);
				});
			}
			else
			{
				fmt::print("Unable to create window surface.\n");
				return;
			}

			std::vector<vk::SurfaceFormatKHR> SurfaceFormats = Adapter.getSurfaceFormatsKHR(Surface);
			std::vector<vk::QueueFamilyProperties> QueueFamilies = Adapter.getQueueFamilyProperties();
			for (uint32_t i = 0; i < QueueFamilies.size(); ++i)
			{
				vk::QueueFamilyProperties& QueueFamily = QueueFamilies[i];
				bool SupportsPresent = Adapter.getSurfaceSupportKHR(i, Surface);
				vk::QueueFlags Match = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
				if (SupportsPresent && (QueueFamily.queueFlags & Match) == Match)
				{
					QueueFamilyIndex = i;
					break;
				}
			}
			if (QueueFamilyIndex == -1)
			{
				fmt::print("No compatible device queue found.\n");
				return;
			}
		}

		{
			float QueuePriority = 0.0;
			vk::DeviceQueueCreateInfo QueueCreateInfo({}, QueueFamilyIndex, 1, &QueuePriority);

			std::vector<const char*> DeviceExtensions;
			DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

			vk::DeviceCreateInfo DeviceCreateInfo({
				{},
				QueueCreateInfo,
				DeviceExtensions,
				{},
				nullptr
			});

			Device = Adapter.createDevice(DeviceCreateInfo);

			ShutdownTasks.push_back([&]()
			{
				Device.destroy();
			});
		}
	}

	virtual ~VkContext()
	{
		while (ShutdownTasks.size() > 0)
		{
			ShutdownTasks.back()();
			ShutdownTasks.pop_back();
		}
		SDL_Quit();
	}

	virtual bool Initialized()
	{
		return Complete;
	}

	virtual SDL_Window* GetWindow()
	{
		return Window;
	}
};


VkWindow* VkWindow::Create()
{
	return new VkContext();
}
