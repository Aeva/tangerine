
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

#include "colors.h"
#include "glm_common.h"

#include <cmath>
#include <regex>
#include <array>
#include <unordered_map>


const std::array<std::pair<ColorSpace, std::string>, size_t(ColorSpace::Count) > EncodingNames = \
{
	std::pair<ColorSpace, std::string> { ColorSpace::sRGB, "sRGB" },
	std::pair<ColorSpace, std::string> { ColorSpace::OkLAB, "OkLAB" },
	std::pair<ColorSpace, std::string> { ColorSpace::LinearRGB, "LinearRGB"},
};


std::string ColorSpaceName(ColorSpace Encoding)
{
	for (int i = 0; i < size_t(ColorSpace::Count) ; ++i)
	{
		if (EncodingNames[i].first == Encoding)
		{
			return EncodingNames[i].second;
		}
	}
	return "error";
}


bool FindColorSpace(std::string Name, ColorSpace& OutEncoding)
{
	for (int i = 0; i < size_t(ColorSpace::Count); ++i)
	{
		if (EncodingNames[i].second == Name)
		{
			OutEncoding = EncodingNames[i].first;
			return true;
		}
	}
	return false;
}


static glm::vec3 sRGB2Linear(glm::vec3 sRGB)
{
	// Convert from sRGB to Linear RGB.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code

	glm::vec3 Linear(0.0f);

	for (int Channel = 0; Channel < 3; ++Channel)
	{
		const float Color = sRGB[Channel];
		const float AbsColor = glm::abs(Color);

		if (AbsColor < 0.04045f)
		{
			Linear[Channel] = Color / 12.92f;
		}
		else
		{
			Linear[Channel] = glm::sign(Color) * (glm::pow((AbsColor + 0.055f) / 1.055f, 2.4f));
		}
	}

	return Linear;
}


static glm::vec3 Linear2sRGB(glm::vec3 Linear)
{
	// Convert from Linear RGB to sRGB.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code

	glm::vec3 sRGB(0.0f);

	for (int Channel = 0; Channel < 3; ++Channel)
	{
		const float Color = Linear[Channel];
		const float AbsColor = glm::abs(Color);

		if (AbsColor > 0.0031308f)
		{
			sRGB[Channel] = glm::sign(Color) * (1.055f * glm::pow(AbsColor, 1.f/2.4f) - 0.055f);
		}
		else
		{
			sRGB[Channel] = 12.92f * Color;
		}
	}

	return sRGB;
}


static glm::vec3 Linear2XYZ(glm::vec3 Linear)
{
	// Convert from Linear RGB to CIE XYZ.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code
	const glm::mat3 ToXYZ(
		glm::vec3(506752.f / 1228815.f, 87881.f / 245763.f, 12673.f / 70218.f),
		glm::vec3(87098.f / 409605.f, 175762.f / 245763.f, 12673.f / 175545.f),
		glm::vec3(7918.f / 409605.f, 87881.f / 737289.f, 1001167.f / 1053270.f));
	return Linear * ToXYZ;
}


static glm::vec3 XYZ2Linear(glm::vec3 XYZ)
{
	// Convert from CIE XYZ to Linear RGB.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code
	glm::mat3 ToLinear(
		glm::vec3(12831.f / 3959.f, -329.f / 214.f, -1974.f / 3959.f),
		glm::vec3(-851781.f / 878810.f, 1648619.f / 878810.f, 36519.f / 878810.f),
		glm::vec3(705.f / 12673.f, -2585.f / 12673.f, 705.f / 667.f));
	return XYZ * ToLinear;
}


static glm::vec3 XYZ2OkLab(glm::vec3 XYZ)
{
	// Convert from D65-relative CIE XYZ to OKLab.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code

	glm::mat3 ToLMS(
		glm::vec3(0.8190224432164319f, 0.3619062562801221f, -0.12887378261216414f),
		glm::vec3(0.0329836671980271f, 0.9292868468965546f, 0.03614466816999844f),
		glm::vec3(0.048177199566046255f, 0.26423952494422764f, 0.6335478258136937f));

	glm::mat3 ToOkLab(
		glm::vec3(0.2104542553f, 0.7936177850f, -0.0040720468f),
		glm::vec3(1.9779984951f, -2.4285922050f, 0.4505937099f),
		glm::vec3(0.0259040371f, 0.7827717662f, -0.8086757660f));

	glm::vec3 LMS = XYZ * ToLMS;

	for (int Channel = 0; Channel < 3; ++Channel)
	{
		LMS[Channel] = std::cbrt(LMS[Channel]);
	}

	glm::vec3 OkLab = LMS * ToOkLab;

	return OkLab;
}


static glm::vec3 OkLab2XYZ(glm::vec3 OkLab)
{
	// Convert from OKLab to D65-relative CIE XYZ.
	// Adapted from https://www.w3.org/TR/css-color-4/#color-conversion-code

	glm::mat3 ToLMS(
		glm::vec3(0.99999999845051981432f, 0.39633779217376785678f, 0.21580375806075880339f),
		glm::vec3(1.0000000088817607767f, -0.1055613423236563494f, -0.063854174771705903402f),
		glm::vec3(1.0000000546724109177f, -0.089484182094965759684f, -1.2914855378640917399f));

	glm::mat3 ToXYZ(
		glm::vec3(1.2268798733741557f, -0.5578149965554813f, 0.28139105017721583f),
		glm::vec3(-0.04057576262431372f, 1.1122868293970594f, -0.07171106666151701f),
		glm::vec3(-0.07637294974672142f, -0.4214933239627914f, 1.5869240244272418f));

	glm::vec3 LMS = OkLab * ToLMS;

	for (int Channel = 0; Channel < 3; ++Channel)
	{
		LMS[Channel] = LMS[Channel] * LMS[Channel] * LMS[Channel];
	}

	glm::vec3 XYZ = LMS * ToXYZ;
	return XYZ;
}


ColorPoint ColorPoint::Encode(ColorSpace OutEncoding)
{
	if (OutEncoding == Encoding)
	{
		return *this;
	}
	else
	{
		glm::vec3 Intermediary = Channels;

		if (Encoding != OutEncoding)
		{
			// Convert the stored color to an sRGB intermediary.
			switch (Encoding)
			{
			case(ColorSpace::OkLAB):
				Intermediary = Linear2sRGB(XYZ2Linear(OkLab2XYZ(Intermediary)));
				break;
			case(ColorSpace::LinearRGB):
				Intermediary = Linear2sRGB(Intermediary);
				break;
			default:
				break;
			}

			// Convert the sRGB intermediary to the output encoding.
			switch (OutEncoding)
			{
			case(ColorSpace::OkLAB):
				Intermediary = XYZ2OkLab(Linear2XYZ(sRGB2Linear(Intermediary)));
				break;
			case(ColorSpace::LinearRGB):
				Intermediary = sRGB2Linear(Intermediary);
				break;
			default:
				break;
			}
		}
		return ColorPoint(OutEncoding, Intermediary);
	}
}


glm::vec3 ColorPoint::Eval(ColorSpace OutEncoding)
{
	if (OutEncoding == Encoding)
	{
		return Channels;
	}
	else
	{
		ColorPoint Transcoded = Encode(OutEncoding);
		return Transcoded.Channels;
	}
}


void ColorPoint::MutateEncoding(ColorSpace NewEncoding)
{
	if (Encoding != NewEncoding)
	{
		Channels = Eval(NewEncoding);
		Encoding = NewEncoding;
	}
}


void ColorPoint::MutateChannels(glm::vec3 NewChannels)
{
	Channels = NewChannels;
}


bool ColorPointCmp::operator()(const ColorPoint& LHS, const ColorPoint& RHS) const
{
	if (LHS.Encoding < RHS.Encoding)
	{
		return true;
	}
	else if (LHS.Encoding == RHS.Encoding)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (LHS.Channels[i] < RHS.Channels[i])
			{
				return true;
			}
			else if (LHS.Channels[i] > RHS.Channels[i])
			{
				break;
			}
		}
	}
	return false;
}


ColorRamp::ColorRamp(std::vector<ColorPoint>& InStops, ColorSpace InEncoding)
	: Encoding(InEncoding)
{
	Stops.reserve(InStops.size());
	for (ColorPoint Stop : InStops)
	{
		Stops.push_back(Stop.Encode(InEncoding));
	}
	if (Stops.size() == 0)
	{
		Stops.push_back(ColorPoint());
	}
}


glm::vec3 ColorRamp::Eval(ColorSpace OutEncoding, float Alpha)
{
	if (Stops.size() == 1)
	{
		return Stops[0].Eval(OutEncoding);
	}
	else if (Stops.size() == 2)
	{
		ColorPoint Intermediary(Encoding, glm::mix(Stops[0].Channels, Stops[1].Channels, Alpha));
		return Intermediary.Eval(OutEncoding);
	}
	else if (Stops.size() > 1)
	{
		float WedgeCount = float(Stops.size() - 1);
		float WedgeSpan = 1.0 / WedgeCount;
		float LowStop = glm::floor(WedgeCount * Alpha);
		float WedgeAlpha = (Alpha - (LowStop * WedgeSpan)) / WedgeSpan;
		size_t LowIndex = size_t(LowStop);
		size_t HighIndex = LowIndex + 1;
		ColorPoint Intermediary(Encoding, glm::mix(Stops[LowIndex].Channels, Stops[HighIndex].Channels, WedgeAlpha));
		return Intermediary.Eval(OutEncoding);
	}
	else
	{
		return ColorPoint().Eval(OutEncoding);
	}
}


glm::vec3 SampleColor(ColorSampler Color, ColorSpace Encoding)
{
	if (ColorPoint* AsPoint = std::get_if<ColorPoint>(&Color))
	{
		return AsPoint->Eval(Encoding);
	}
	else if (ColorRamp* AsRamp = std::get_if<ColorRamp>(&Color))
	{
		return AsRamp->Eval(Encoding, 0.0);
	}
	else
	{
		return glm::vec3(0.0f);
	}
}


glm::vec3 SampleColor(ColorSampler Color, float Alpha, ColorSpace Encoding)
{
	if (ColorPoint* AsPoint = std::get_if<ColorPoint>(&Color))
	{
		return AsPoint->Eval(Encoding);
	}
	else if (ColorRamp* AsRamp = std::get_if<ColorRamp>(&Color))
	{
		return AsRamp->Eval(Encoding, Alpha);
	}
	else
	{
		return glm::vec3(0.0f);
	}
}


const std::unordered_map<std::string, std::string> ColorNames = \
{
	// https://www.w3.org/TR/CSS1/
	{ "black", "#000000" },
	{ "silver", "#c0c0c0" },
	{ "gray", "#808080" },
	{ "white", "#ffffff" },
	{ "maroon", "#800000" },
	{ "red", "#ff0000" },
	{ "purple", "#800080" },
	{ "fuchsia", "#ff00ff" },
	{ "green", "#008000" },
	{ "lime", "#00ff00" },
	{ "olive", "#808000" },
	{ "yellow", "#ffff00" },
	{ "navy", "#000080" },
	{ "blue", "#0000ff" },
	{ "teal", "#008080" },
	{ "aqua", "#00ffff" },

	// https://www.w3.org/TR/CSS2/
	{ "orange", "#ffa500" },

	// https://drafts.csswg.org/css-color-3/
	{ "aliceblue", "#f0f8ff" },
	{ "antiquewhite", "#faebd7" },
	{ "aquamarine", "#7fffd4" },
	{ "azure", "#f0ffff" },
	{ "beige", "#f5f5dc" },
	{ "bisque", "#ffe4c4" },
	{ "blanchedalmond", "#ffebcd" },
	{ "blueviolet", "#8a2be2" },
	{ "brown", "#a52a2a" },
	{ "burlywood", "#deb887" },
	{ "cadetblue", "#5f9ea0" },
	{ "chartreuse", "#7fff00" },
	{ "chocolate", "#d2691e" },
	{ "coral", "#ff7f50" },
	{ "cornflowerblue", "#6495ed" },
	{ "cornsilk", "#fff8dc" },
	{ "crimson", "#dc143c" },
	{ "cyan", "#00ffff" },
	{ "darkblue", "#00008b" },
	{ "darkcyan", "#008b8b" },
	{ "darkgoldenrod", "#b8860b" },
	{ "darkgray", "#a9a9a9" },
	{ "darkgreen", "#006400" },
	{ "darkgrey", "#a9a9a9" },
	{ "darkkhaki", "#bdb76b" },
	{ "darkmagenta", "#8b008b" },
	{ "darkolivegreen", "#556b2f" },
	{ "darkorange", "#ff8c00" },
	{ "darkorchid", "#9932cc" },
	{ "darkred", "#8b0000" },
	{ "darksalmon", "#e9967a" },
	{ "darkseagreen", "#8fbc8f" },
	{ "darkslateblue", "#483d8b" },
	{ "darkslategray", "#2f4f4f" },
	{ "darkslategrey", "#2f4f4f" },
	{ "darkturquoise", "#00ced1" },
	{ "darkviolet", "#9400d3" },
	{ "deeppink", "#ff1493" },
	{ "deepskyblue", "#00bfff" },
	{ "dimgray", "#696969" },
	{ "dimgrey", "#696969" },
	{ "dodgerblue", "#1e90ff" },
	{ "firebrick", "#b22222" },
	{ "floralwhite", "#fffaf0" },
	{ "forestgreen", "#228b22" },
	{ "gainsboro", "#dcdcdc" },
	{ "ghostwhite", "#f8f8ff" },
	{ "gold", "#ffd700" },
	{ "goldenrod", "#daa520" },
	{ "greenyellow", "#adff2f" },
	{ "grey", "#808080" },
	{ "honeydew", "#f0fff0" },
	{ "hotpink", "#ff69b4" },
	{ "indianred", "#cd5c5c" },
	{ "indigo", "#4b0082" },
	{ "ivory", "#fffff0" },
	{ "khaki", "#f0e68c" },
	{ "lavender", "#e6e6fa" },
	{ "lavenderblush", "#fff0f5" },
	{ "lawngreen", "#7cfc00" },
	{ "lemonchiffon", "#fffacd" },
	{ "lightblue", "#add8e6" },
	{ "lightcoral", "#f08080" },
	{ "lightcyan", "#e0ffff" },
	{ "lightgoldenrodyellow", "#fafad2" },
	{ "lightgray", "#d3d3d3" },
	{ "lightgreen", "#90ee90" },
	{ "lightgrey", "#d3d3d3" },
	{ "lightpink", "#ffb6c1" },
	{ "lightsalmon", "#ffa07a" },
	{ "lightseagreen", "#20b2aa" },
	{ "lightskyblue", "#87cefa" },
	{ "lightslategray", "#778899" },
	{ "lightslategrey", "#778899" },
	{ "lightsteelblue", "#b0c4de" },
	{ "lightyellow", "#ffffe0" },
	{ "limegreen", "#32cd32" },
	{ "linen", "#faf0e6" },
	{ "magenta", "#ff00ff" },
	{ "mediumaquamarine", "#66cdaa" },
	{ "mediumblue", "#0000cd" },
	{ "mediumorchid", "#ba55d3" },
	{ "mediumpurple", "#9370db" },
	{ "mediumseagreen", "#3cb371" },
	{ "mediumslateblue", "#7b68ee" },
	{ "mediumspringgreen", "#00fa9a" },
	{ "mediumturquoise", "#48d1cc" },
	{ "mediumvioletred", "#c71585" },
	{ "midnightblue", "#191970" },
	{ "mintcream", "#f5fffa" },
	{ "mistyrose", "#ffe4e1" },
	{ "moccasin", "#ffe4b5" },
	{ "navajowhite", "#ffdead" },
	{ "oldlace", "#fdf5e6" },
	{ "olivedrab", "#6b8e23" },
	{ "orangered", "#ff4500" },
	{ "orchid", "#da70d6" },
	{ "palegoldenrod", "#eee8aa" },
	{ "palegreen", "#98fb98" },
	{ "paleturquoise", "#afeeee" },
	{ "palevioletred", "#db7093" },
	{ "papayawhip", "#ffefd5" },
	{ "peachpuff", "#ffdab9" },
	{ "peru", "#cd853f" },
	{ "pink", "#ffc0cb" },
	{ "plum", "#dda0dd" },
	{ "powderblue", "#b0e0e6" },
	{ "rosybrown", "#bc8f8f" },
	{ "royalblue", "#4169e1" },
	{ "saddlebrown", "#8b4513" },
	{ "salmon", "#fa8072" },
	{ "sandybrown", "#f4a460" },
	{ "seagreen", "#2e8b57" },
	{ "seashell", "#fff5ee" },
	{ "sienna", "#a0522d" },
	{ "skyblue", "#87ceeb" },
	{ "slateblue", "#6a5acd" },
	{ "slategray", "#708090" },
	{ "slategrey", "#708090" },
	{ "snow", "#fffafa" },
	{ "springgreen", "#00ff7f" },
	{ "steelblue", "#4682b4" },
	{ "tan", "#d2b48c" },
	{ "thistle", "#d8bfd8" },
	{ "tomato", "#ff6347" },
	{ "turquoise", "#40e0d0" },
	{ "violet", "#ee82ee" },
	{ "wheat", "#f5deb3" },
	{ "whitesmoke", "#f5f5f5" },
	{ "yellowgreen", "#9acd32" },

	// https://drafts.csswg.org/css-color-4/
	{ "rebeccapurple", "#663399" },

	// 🍊🎀✨
	{ "tangerine", "#f0811a" },
	{ "🍊", "#f0811a" },
};


std::regex MakeOklabExpr()
{
	std::string Prefix = "oklab\\(";
	std::string Suffix = "\\)";

	std::string NumberGroup = "(-?\\d+|-?\\d+\\.\\d*|-?\\d*\\.\\d+)";
	std::string Padding = "\\s*";
	std::string Separator = "\\s+";

	std::string Expr = Prefix + Padding + NumberGroup + Separator + NumberGroup + Separator + NumberGroup + Padding + Suffix;
	return std::regex(Expr, std::regex::ECMAScript | std::regex::icase);
}


StatusCode ParseColor(std::string ColorString, ColorPoint& OutColor)
{
	static const std::regex HexTripple("#[0-9A-F]{3}", std::regex::icase);
	static const std::regex HexSextuple("#[0-9A-F]{6}", std::regex::icase);
	static const std::regex OklabExpr = MakeOklabExpr();

	std::smatch Match;

	if (std::regex_match(ColorString.c_str(), HexTripple))
	{
		int Color = std::stoi(ColorString.substr(1, 3), nullptr, 16);
		glm::vec3 Channels = glm::vec3(
			float((Color >> 8) & 15),
			float((Color >> 4) & 15),
			float((Color >> 0) & 15)) / glm::vec3(15.0);
		OutColor = ColorPoint(ColorSpace::sRGB, Channels);
		return StatusCode::PASS;
	}
	else if (std::regex_match(ColorString.c_str(), HexSextuple))
	{
		int Color = std::stoi(ColorString.substr(1, 6), nullptr, 16);
		glm::vec3 Channels = glm::vec3(
			float((Color >> 16) & 255),
			float((Color >> 8) & 255),
			float((Color >> 0) & 255)) / glm::vec3(255.0);
		OutColor = ColorPoint(ColorSpace::sRGB, Channels);
		return StatusCode::PASS;
	}
	else if (std::regex_match(ColorString, Match, OklabExpr))
	{
		// https://developer.mozilla.org/en-US/docs/Web/CSS/color_value/oklab
		// https://www.w3.org/TR/css-color-4/#specifying-oklab-oklch

		// Should percentages ever be supported, the percent ranges are:
		// for L: 0% = 0.0, 100% = 1.0
		// for a & b: 0% = -0.4, 100% = 0.4

		glm::vec3 Channels;
		Channels[0] = glm::clamp(std::stof(Match[1]), 0.0f, 1.0f);
		Channels[1] = std::stof(Match[2]);
		Channels[2] = std::stof(Match[3]);

		OutColor = ColorPoint(ColorSpace::OkLAB, Channels);
		return StatusCode::PASS;
	}
	else
	{
		auto Search = ColorNames.find(ColorString);
		if (Search != ColorNames.end())
		{
			return ParseColor(Search->second, OutColor);
		}
	}

	OutColor = ColorPoint();
	return StatusCode::FAIL;
};


StatusCode ParseColor(std::string ColorString, glm::vec3& OutColor)
{
	ColorPoint Intermediary;
	StatusCode Result = ParseColor(ColorString, Intermediary);
	OutColor = Intermediary.Eval(ColorSpace::sRGB);
	return Result;
}


ColorPoint ParseColor(std::string ColorString)
{
	ColorPoint ParsedColor = ColorPoint();
	StatusCode Result = ParseColor(ColorString, ParsedColor);
	return ParsedColor;
}
