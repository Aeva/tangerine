#pragma once

namespace isosurface {

/**
* This class is a discretization of continuous 3d space into voxels, which are the smallest volume 
* in the regular grid, in the form of cubes. The grid is represented as:
* 
* - a point (x,y,z)
* - voxel dimensions in every direction (dx,dy,dz)
* - the number of voxels in every direction (sx,sy,sz)
* 
* We consider that the grid's dimensions in 3d space form a rectangular prism with 
* lower-left-frontmost corner (x,y,z)
* upper-right-deepest corner  (x + sx*dx, y + sy*dy, z + sz*dz)
* 
* As such, the grid is only a subset of 3d space and corresponds to the axis-aligned bounding box 
* delimited by:
* lower-left-frontmost corner (x,y,z)
* upper-right-deepest corner  (x + sx*dx, y + sy*dy, z + sz*dz)
* 
* The grid considers that space is divided into small cubes of dimensions (dx,dy,dz) called
* voxels, all stacked next to each other in the x,y,z directions.
* 
* Many scalar functions can be approximated continuously everywhere in the voxel grid
* using trilinear interpolation by having the values of the scalar function at every
* voxel's corners.
*/
struct regular_grid_t
{
	// origin of the grid
	float x = 0.f, y = 0.f, z = 0.f;
	// voxel size in x,y,z directions
	float dx = 0.f, dy = 0.f, dz = 0.f;
	// number of voxels in x,y,z directions starting from the point (x,y,z)
	std::size_t sx = 0, sy = 0, sz = 0;
};

} // isosurface