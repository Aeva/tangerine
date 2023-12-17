
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

#pragma once

#include "glm_common.h"
#include <string>
#include <vector>
#include <variant>
#include "errors.h"


enum class ColorSpace
{
	sRGB,
	LinearRGB,
	OkLAB,
	OkLCH,

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
		if (Encoding == ColorSpace::OkLAB)
		{
			// https://www.w3.org/TR/css-color-4/#specifying-oklab-oklch

			float& Lightness = Channels[0];
			float& AxisA = Channels[1];
			float& AxisB = Channels[2];

			Lightness = glm::clamp(Lightness, 0.0f, 1.0f);

			if (Lightness == 0.0f || Lightness == 1.0f)
			{
				AxisA = 0.0f;
				AxisB = 0.0f;
			}
		}
		else if (Encoding == ColorSpace::OkLCH)
		{
			// https://www.w3.org/TR/css-color-4/#specifying-oklab-oklch

			float& Lightness = Channels[0];
			float& Chroma = Channels[1];
			float& Hue = Channels[2];

			Lightness = glm::clamp(Lightness, 0.0f, 1.0f);
			Chroma = glm::max(Chroma, 0.0f);

			if (Lightness == 0.0f || Lightness == 1.0f)
			{
				Chroma = 0.0f;
				Hue = 0.0f;
			}

			if (Chroma == 0.0f)
			{
				Hue = 0.0f;
			}
		}
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


struct ColorPointCmp
{
	bool operator()(const ColorPoint& LHS, const ColorPoint& RHS) const;
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
