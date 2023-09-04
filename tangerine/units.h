
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

#include <string>


enum class UnitSymbol : size_t
{
	// SI Meter
	m = 0,

	// SI Meter Submultiples
	mm,
	cm,
	dm,
	
	// SI Meter Multiples
	dam,
	hm,
	km,

	// United States Customary Units
	in,
	ft,
	yd,
	mi,

	// ------------------------
	Count,
	Invalid = Count
};


double UnitToMeters(UnitSymbol Unit);


namespace ExportGrid
{
	bool SetInternalScale(double Multiplier, std::string Unit);
	bool SetExternalScale(double Multiplier, std::string Unit);

	void ResetScale();
	double GetScale();
};