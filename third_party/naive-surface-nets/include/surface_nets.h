#include "mesh.h"
#include "regular_grid.h"

#include <functional>

namespace isosurface {

// BEGIN TANGERINE MOD - Modify declarations to remove LIBIGL dependency
void surface_nets(
	std::function<float(float x, float y, float z)> const& implicit_function,
	regular_grid_t const& grid,
	mesh& out_mesh,
	float const isovalue = 0.f
);

void par_surface_nets(
	std::function<float(float x, float y, float z)> const& implicit_function,
	regular_grid_t const& grid,
	mesh& out_mesh,
	float const isovalue = 0.f
);

void surface_nets(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
	point_t const& hint,
	mesh& out_mesh,
    float const isovalue                               = 0.f,
    std::size_t max_size_of_breadth_first_search_queue = 32'768u);
// END TANGERINE MOD

} // isosurface