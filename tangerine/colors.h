
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

#pragma once

#include <glm/vec3.hpp>
#include <string>
#include <vector>
#include <variant>
#include "errors.h"


enum class ColorSpace
{
	sRGB,
	OkLAB,
	LinearRGB,

	Count
};


std::string ColorSpaceName(ColorSpace Encoding);
bool FindColorSpace(std::string Name, ColorSpace& OutEncoding);


struct ColorPoint
{
	ColorSpace Encoding;
	glm::vec3 Channels;

	ColorPoint()
		: Encoding(ColorSpace::sRGB)
	{
		Channels = glm::vec3(0.0f, 0.0f, 0.0f);
	}

	ColorPoint(glm::vec3 InColor)
		: Encoding(ColorSpace::sRGB)
		, Channels(InColor)
	{
	}

	ColorPoint(ColorSpace InEncoding, glm::vec3 InChannels)
		: Encoding(InEncoding)
		, Channels(InChannels)
	{
	}

	ColorPoint(ColorSpace InEncoding, ColorPoint Other)
		: Encoding(InEncoding)
		, Channels(Other.Eval(InEncoding))
	{
	}

	ColorPoint Encode(ColorSpace OutEncoding);

	glm::vec3 Eval(ColorSpace OutEncoding);

	void MutateEncoding(ColorSpace NewEncoding);

	void MutateChannels(glm::vec3 NewChannels);
};


struct ColorRamp
{
	ColorSpace Encoding;
	std::vector<ColorPoint> Stops;

	ColorRamp(std::vector<ColorPoint>& InStops, ColorSpace InEncoding = ColorSpace::OkLAB);

	glm::vec3 Eval(ColorSpace OutEncoding, float Alpha);
};


using ColorSampler = std::variant<ColorPoint, ColorRamp>;


glm::vec3 SampleColor(ColorSampler Color, ColorSpace Encoding = ColorSpace::sRGB);


glm::vec3 SampleColor(ColorSampler Color, float Alpha, ColorSpace Encoding = ColorSpace::sRGB);


StatusCode ParseColor(std::string ColorString, ColorPoint& OutColor);


StatusCode ParseColor(std::string ColorString, glm::vec3& OutColor);


ColorPoint ParseColor(std::string ColorString);
