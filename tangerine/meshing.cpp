
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

#include <predicates.h>
#include "meshing.h"
#include <vector>

using namespace glm;


void VoronoiSurface(SDFNode* Evaluator)
{
	// My intention here is to implement a variant of naive surface nets (and other things).
	// The basic flow is this:
	//
	// First, generate a set of seed points.  At minimum this should include points that were
	// selected randomly within octree bounding regions.  Brush functions in the SDF graph should
	// also contribute points near corners and along feature lines, possibly with an appropriate
	// degree of insetting or outsetting depending on contex.
	//
	// Second, find the Delaunay tetrahedralization of the point set via the Bowyer-Watson algorithm.
	// The "predicates" library has been included already for this purpose.
	//
	// Third, the resulting tetra are then assigned a symbol.  This symbol is the SDF sign evaluated
	// at the center of the tet, where a value >= 0 is exterior.  Adjacent tetra with mismatched
	// symbols are paired, and an isovert is interpolated from the SDF vaules at the paired tetra
	// centers.  Isoedges are determined by the connectivity of adjacent tetra, and so on.
	//
	// This algorithm supports some useful modifications.  The first is responding to incremental
	// additions the SDF graph.  Simply compute the bounding regions that have changes, invalidate
	// the symbols on tetras within the bounds, delete any geometry that was generated from them,
	// add new seed points, etc.
	//
	// The second modification, is that the algorithm can be expanded to also generate interior
	// faces such as for material interfaces and so on.  The symbols set then is one for all exterior
	// space, and an additional symbol for each different material interior.
}
