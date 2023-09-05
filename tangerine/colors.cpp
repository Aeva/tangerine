
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

#include "colors.h"
#include "glm_common.h"

#include <oklab.h>
#include <regex>
#include <unordered_map>


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


StatusCode ParseColor(std::string ColorString, glm::vec3& OutColor)
{
	static const std::regex HexTripple("#[0-9A-F]{3}", std::regex::icase);
	static const std::regex HexSextuple("#[0-9A-F]{6}", std::regex::icase);
	static const std::regex OklabExpr = MakeOklabExpr();

	std::smatch Match;

	if (std::regex_match(ColorString.c_str(), HexTripple))
	{
		int Color = std::stoi(ColorString.substr(1, 3), nullptr, 16);
		OutColor = glm::vec3(
			float((Color >> 8) & 15),
			float((Color >> 4) & 15),
			float((Color >> 0) & 15)) / glm::vec3(15.0);
		return StatusCode::PASS;
	}
	else if (std::regex_match(ColorString.c_str(), HexSextuple))
	{
		int Color = std::stoi(ColorString.substr(1, 6), nullptr, 16);
		OutColor = glm::vec3(
			float((Color >> 16) & 255),
			float((Color >>  8) & 255),
			float((Color >>  0) & 255)) / glm::vec3(255.0);
		return StatusCode::PASS;
	}
	else if (std::regex_match(ColorString, Match, OklabExpr))
	{
		// https://developer.mozilla.org/en-US/docs/Web/CSS/color_value/oklab
		Oklab::Lab Color;
		Color.L = glm::clamp(std::stof(Match[1]), 0.0f, 1.0f);

		// MDN claims the legal range of these terms is is -0.4 to 0.4, but their own examples contradict this.
		Color.a = glm::clamp(std::stof(Match[2]), -1.0f, 1.0f);
		Color.b = glm::clamp(std::stof(Match[3]), -1.0f, 1.0f);

		Oklab::RGB Tmp = Oklab::oklab_to_srgb(Color);
		OutColor.r = glm::clamp(Tmp.r, 0.0f, 1.0f);
		OutColor.g = glm::clamp(Tmp.g, 0.0f, 1.0f);
		OutColor.b = glm::clamp(Tmp.b, 0.0f, 1.0f);

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

	OutColor = glm::vec3(0.0);
	return StatusCode::FAIL;
}
