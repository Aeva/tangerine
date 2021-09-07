
// Copyright 2021 Aeva Palecek
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
// See the License for the specific language governing permissionsand
// limitations under the License.

#pragma once

#include "errors.h"


#if _WIN64
#define BACKEND_API __declspec(dllexport)

#elif defined(__GNUC__)
#define BACKEND_API __attribute__ ((visibility ("default")))

#endif


extern "C" bool BACKEND_API PlatformSupportsAsyncRenderer();


extern "C" StatusCode BACKEND_API Setup();


extern "C" bool BACKEND_API RenderFrame();


extern "C" void BACKEND_API Resize(int NewWidth, int NewHeight);


extern "C" void BACKEND_API LockShaders();


extern "C" void BACKEND_API PostShader(int ClusterCount, const char* ClusterDist, const char* ClusterData);


extern "C" void BACKEND_API UnlockShaders();


extern "C" void BACKEND_API Shutdown();
