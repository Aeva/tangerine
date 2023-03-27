
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

// This file was adapted with significant alterations from the "surface-nets"
// C library by Roberto Toro, which in turn was adapted from a Javascript
// implementation by Mikola Lysenko, which in turn was based on S.F. Gibson
// "Constrained Elastic Surface Nets" (1998) MERL Tech Report.
//
// https://github.com/r03ert0/surface-nets
// https://github.com/mikolalysenko/mikolalysenko.github.com/blob/master/Isosurface/js/surfacenets.js
//
// Both Lysenko and Toro's implementations were released under the MIT license,
// and so the license of the earlier implementations is below:

// The MIT License (MIT)
//
// Copyright (c) 2016 Roberto Toro
// Copyright (c) 2012-2013 Mikola Lysenko
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "surface_nets.h"
#include <array>


static std::array<int, 24> CubeEdges;
static std::array<int, 256> EdgeTable;


static void PrecomputeEdgeTable()
{
	{
		// Initialize the cube_edges table.  This is just the vertex number of each cube.

		int Edge = 0;
		for (int i = 0; i < 8; ++i)
		{
			for (int j = 1; j <= 4; j <<= 1)
			{
				int p = i ^ j;
				if (i <= p)
				{
					CubeEdges[Edge++] = i;
					CubeEdges[Edge++] = p;
				}
			}
		}
	}

	{
		// Initialize the intersection table.  This is a 2^(cube configuration) -> 2^(edge configuration) map
		// There is one entry for each possible cube configuration, and the output is a 12-bit vector enumerating all edges crossing the 0-level.

		for (int i = 0; i < 256; ++i)
		{
			int em = 0;
			for (size_t j = 0; j < 24; j += 2)
			{
				const bool a = (i & (1 << CubeEdges[j]));
				const bool b = (i & (1 << CubeEdges[j + 1]));
				if (a != b)
				{
					em |= (1 << (j >> 1));
				}
			}
			CubeEdges[i] = em;
		}
	}
}


void SurfaceNets(SDFNode* Evaluator, AABB Bounds, glm::vec3 Step, std::vector<float>& OutVertices, std::vector<int>& OutFaces)
{
	static bool Initialized = false;
	if (!Initialized)
	{
		PrecomputeEdgeTable();
	}
#if 0 // WIP new implementation
	glm::ivec3 Dimensions = glm::ceil(Bounds.Extent() / Step);

	// Corresponds to array "R" in the earlier implementations.
	glm::ivec3 Stride = { 1, Dimensions.x + 1, (Dimensions.x + 1) * (Dimensions.y + 1) };

	// Corresponds to array "buffer" in the earlier implementations, which contains the vertex indices of the previous x/y slice.
	std::vector<int> SliceCache;
	SliceCache.reserve(Stride.z * 2);

	// Corresponds to array "x" in the earlier implementations.
	glm::ivec3 Voxel = glm::ivec3(0, 0, 0);

	// Unclear
	std::array<int, 8> Grid = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int n = 0;
	int BufferNumber = 1;

	// Loop over the voxel grid slice by slice.
	for (; Voxel.z < Dimensions.z - 1; ++Voxel.z)
	{
		// This corresponds to "m" in the earlier implementations.
		// This is the offset into SliceCache to be used by this layer.  The original javascript implementation notes
		// that this is to work around Javascript's poor support for packed data structures.  This was preserved in the
		// earlier C-implementation.  In the interest in putting together a working implementation before factoring out
		// stuff like this, this hack is reproduced here as well.
		int CacheOffset = 1 + (Dimensions.x + 1) * (1 + BufferNumber * (Dimensions.y + 1));


		for (; Voxel.y < Dimensions.y - 1; ++Voxel.y, ++n, CacheOffset += 2)
		{
			for (; Voxel.x < Dimensions.z - 1; ++Voxel.x, ++n, ++CacheOffset)
			{
				// Read in 8 field values around this vertex and store them in an array
				// Also calculate 8-bit mask, like in marching cubes, so we can speed up sign checks later
				int Mask = 0;
				int g = 0;
				int idx = n;
				for (int k = 0; k < 2; ++k)
				{
					for (int j = 0; j < 2; ++j)
					{
						for (int i = 0; i < 2; ++i)
						{
							// TODO -- I'm unsure at the moment if the data buffer in the earlier implementations is meant to store
							// the SDF values at the voxel centers or at the voxel corners.  The convention needs to be pinned down
							// so that the correct point location can be determined.  The current value of "Point" below is incorrect.

							const glm::vec3 Point = glm::vec3(Voxel) * Step + Bounds.Min;
							const float Dist = Evaluator->Eval(Point);

							Grid[g] = Dist;
							Mask |= (Dist < 0) ? (1 << g) : 0;

							++g;
							++idx;
						}
						idx += Dimensions[0] - 2;
					}
					idx += Dimensions[0] * (Dimensions[1] - 2);
				}

				// Check for early termination if cell does not intersect boundary
				if (Mask == 0 || Mask == 0xff)
				{
					continue;
				}

				// TODO
			}

			// TODO
		}

		n += Dimensions.x;
		BufferNumber ^= 1;
		Stride.z *= -1.0;
	}
#endif

#if 0
	// March over the voxel grid

	// NOTE: this loop steps through the Z axis
	for (x[2] = 0; x[2] < Dimensions[2] - 1; ++x[2], n += Dimensions[0], BufferNumber ^= 1, R[2] = -R[2])
	{

		// m is the pointer into the buffer we are going to use.
		// This is slightly obtuse because javascript does not have good support for packed data structures, so we must use typed arrays :(
		// The contents of the buffer will be the indices of the vertices on the previous x/y slice of the volume
		int m = 1 + (Dimensions[0] + 1) * (1 + BufferNumber * (Dimensions[1] + 1));

		// NOTE: this loop steps through the Y axis
		for (x[1] = 0; x[1] < Dimensions[1] - 1; ++x[1], ++n, m += 2)
		{

			// NOTE: this loop steps through the x axis
			for (x[0] = 0; x[0] < Dimensions[0] - 1; ++x[0], ++n, ++m)
			{
				// Read in 8 field values around this vertex and store them in an array
				// Also calculate 8-bit mask, like in marching cubes, so we can speed up sign checks later
				int mask = 0, g = 0, idx = n;
				for (int k = 0; k < 2; ++k)
				{
					for (j = 0; j < 2; ++j)
					{
						for (i = 0; i < 2; ++i, ++g, ++idx)
						{
							float p = data[idx];
							Grid[g] = p;
							mask |= (p < 0) ? (1 << g) : 0;
						}
						idx += Dimensions[0] - 2;
					}
					idx += Dimensions[0] * (Dimensions[1] - 2);
				}

				// Check for early termination if cell does not intersect boundary
				if (mask == 0 || mask == 0xff)
				{
					continue;
				}

				// Sum up edge intersections
				int edge_mask = EdgeTable[mask];
				std::array<float, 3> v = { 0.0,0.0,0.0 };
				int e_count = 0;

				// For every edge of the cube...
				for (int i = 0; i < 12; ++i)
				{
					// Use edge mask to check if it is crossed
					if (!(edge_mask & (1 << i)))
					{
						continue;
					}

					// If it did, increment number of edge crossings
					++e_count;
					int e0 = CubeEdges[i << 1];			//Unpack vertices
					int e1 = CubeEdges[(i << 1) + 1];
					float g0 = Grid[e0];				//Unpack grid values
					float g1 = Grid[e1];
					float t = g0 - g1;					//Compute point of intersection

					if (glm::abs(t) > 1e-6)
					{
						t = g0 / t;
					}
					else
					{
						continue;
					}

					// Interpolate vertices and add up intersections (this can be done without multiplying)
					int k = 1;
					for (j = 0; j < 3; ++j)
					{
						int a = e0 & k;
						int b = e1 & k;
						if (a != b)
						{
							((float*)&v)[j] += a ? 1.0 - t : t;
						}
						else
						{
							((float*)&v)[j] += a ? 1.0 : 0;
						}
						k = k << 1;
					}
				}

				// Now we just average the edge intersections and add them to coordinate
				float s = 1.0 / e_count;
				for (i = 0; i < 3; ++i)
				{
					((float*)&v)[i] = x[i] + s * ((float*)&v)[i];
				}

				//Add vertex to buffer, store pointer to vertex index in buffer
				Buffer[m] = OutVertices.size() / 3;
				for (const float Value : v)
				{
					OutVertices.push_back(Value);
				}

				//Now we need to add faces together, to do this we just loop over 3 basis components
				for (int i = 0; i < 3; ++i)
				{
					// The first three entries of the edge_mask count the crossings along the edge.
					if (!(edge_mask & (1 << i)))
					{
						continue;
					}

					// i = axes we are point along.  iu, iv = orthogonal axes
					int iu = (i + 1) % 3;
					int iv = (i + 2) % 3;

					// If we are on a boundary, skip it
					if (x[iu] == 0 || x[iv] == 0)
					{
						continue;
					}

					//Otherwise, look up adjacent edges in buffer
					int du = R[iu];
					int dv = R[iv];

					//Remember to flip orientation depending on the sign of the corner.
					if (mask & 1)
					{
						OutFaces.push_back(Buffer[m]);
						OutFaces.push_back(Buffer[m - du]);
						OutFaces.push_back(Buffer[m - du - dv]);
						OutFaces.push_back(Buffer[m - dv]);
					}
					else
					{
						OutFaces.push_back(Buffer[m]);
						OutFaces.push_back(Buffer[m - dv]);
						OutFaces.push_back(Buffer[m - du - dv]);
						OutFaces.push_back(Buffer[m - du]);
					}
				}
			}
		}

		n += Dimensions[0];
		BufferNumber ^= 1;
		R[2] = -R[2];
	}
#endif
}
