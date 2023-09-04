
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

#include "units.h"
#include <vector>
#include <unordered_map>
#include <cctype>
#include <algorithm>

// https://en.wikipedia.org/wiki/Metre
// https://en.wikipedia.org/wiki/United_States_customary_units


namespace ExportScale
{
	double Internal = 0.0;
	double External = 0.0;
};


static void InitUnit(std::vector<double>& ScaleTable, std::unordered_map<std::string, UnitSymbol>& AliasMap, UnitSymbol Symbol, double Scale, std::string SymbolString, std::string UnitName, std::string Plural)
{
	size_t Index = size_t(Symbol);
	ScaleTable[Index] = Scale;

	AliasMap[SymbolString] = Symbol;
	AliasMap[UnitName] = Symbol;
	AliasMap[Plural] = Symbol;
}


static void InitUnit(std::vector<double>& ScaleTable, std::unordered_map<std::string, UnitSymbol>& AliasMap, UnitSymbol Symbol, double Scale, std::string SymbolString, std::string UnitName)
{
	std::string Plural = UnitName + "s";
	InitUnit(ScaleTable, AliasMap, Symbol, Scale, SymbolString, UnitName, Plural);

	if (UnitName.ends_with("meter"))
	{
		std::string BritishVariant = UnitName;
		BritishVariant.erase(BritishVariant.end() - 5);
		BritishVariant += "metre";
		AliasMap[BritishVariant] = Symbol;
		AliasMap[BritishVariant + "s"] = Symbol;
	}
}


static void InitUnitDefinitions(std::vector<double>& ScaleTable, std::unordered_map<std::string, UnitSymbol>& AliasMap)
{
	ScaleTable.resize((size_t)UnitSymbol::Count);

#define SET(Symbol, Scale, ...) InitUnit(ScaleTable, AliasMap, UnitSymbol::Symbol, Scale, #Symbol, __VA_ARGS__)

	// SI meter
	SET(m, 1.0, "meter");

	// SI Meter submultiples
	SET(mm, 0.001, "millimeter");
	SET(cm, 0.01, "centimeter");
	SET(dm, 0.1, "decimeter");

	// SI Meter multiples
	SET(dam, 10.0, "decameter");
	SET(hm,  100.0, "hectometer");
	SET(km,  1000.0, "kilometer");

	// United States customary units
	SET(in, 0.0254, "inch", "inches");
	SET(ft, 0.3048, "foot", "feet");
	SET(yd, 0.9144, "yard");
	SET(mi, 1609.344, "mile");

#undef SET
}


static std::vector<double> MakeScaleTable()
{
	std::vector<double> ScaleTable;
	std::unordered_map<std::string, UnitSymbol> Ignore;
	InitUnitDefinitions(ScaleTable, Ignore);
	return ScaleTable;
}


static std::unordered_map<std::string, UnitSymbol> MakeAliasMap()
{
	std::vector<double> Ignore;
	std::unordered_map<std::string, UnitSymbol> AliasMap;
	InitUnitDefinitions(Ignore, AliasMap);
	return AliasMap;
}


static const std::vector<double> UnitScaleTable = MakeScaleTable();
static const std::unordered_map<std::string, UnitSymbol> UnitAliasMap = MakeAliasMap();


double UnitToMeters(UnitSymbol Symbol)
{
	if (Symbol < UnitSymbol::Count)
	{
		size_t Index = size_t(Symbol);
		return UnitScaleTable[Index];
	}
	else
	{
		return 0.0;
	}
}


static std::string LowerCase(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return Text;
}


static UnitSymbol FindUnitSymbolByName(std::string UnitName)
{
	auto Found = UnitAliasMap.find(UnitName);
	if (Found != UnitAliasMap.end())
	{
		return Found->second;
	}

	Found = UnitAliasMap.find(LowerCase(UnitName));
	if (Found != UnitAliasMap.end())
	{
		return Found->second;
	}

	return UnitSymbol::Invalid;
}


static double FindUnitSizeByName(std::string UnitName)
{
	UnitSymbol Symbol = FindUnitSymbolByName(UnitName);
	return UnitToMeters(Symbol);
};


static double SetExportGridScale(double Multiplier, std::string Unit)
{
	double UnitSize = FindUnitSizeByName(Unit);
	if (UnitSize > 0.0)
	{
		return UnitSize * Multiplier;
	}
	else
	{
		return 0.0;
	}
}


bool ExportGrid::SetInternalScale(double Multiplier, std::string Unit)
{
	ExportScale::Internal = SetExportGridScale(Multiplier, Unit);
	return ExportScale::Internal > 0.0;
}


bool ExportGrid::SetExternalScale(double Multiplier, std::string Unit)
{
	ExportScale::External = SetExportGridScale(Multiplier, Unit);
	return ExportScale::External > 0.0;
}


void ExportGrid::ResetScale()
{
	ExportScale::Internal = 0.0;
	ExportScale::External = 0.0;
}


double ExportGrid::GetScale()
{
	const bool InternalIsValid = ExportScale::Internal != 0.0;
	const bool ExternalIsValid = ExportScale::External != 0.0;
	
	if (InternalIsValid && ExternalIsValid)
	{
		return ExportScale::Internal / ExportScale::External;
	}
	else
	{
		return 0.0;
	}
}
