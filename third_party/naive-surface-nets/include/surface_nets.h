#include "mesh.h"
#include "regular_grid.h"

#include <functional>
#include <atomic>
#include <mutex>

namespace isosurface {

// BEGIN TANGERINE MOD - Carve up `par_surface_nets` into parts that can be adapted for parallel exe w/ Tangerine's scheduler
	struct AsyncParallelSurfaceNets
	{
		// Common inputs
		std::function<float(float x, float y, float z)> ImplicitFunction;
		regular_grid_t Grid;
		float Isovalue = 0.f;

		struct GridPoint
		{
			size_t i;
			size_t j;
			size_t k;
		};

		// Inputs used when AtomicControls is active
		std::atomic_bool* ExportActive = nullptr;
		std::atomic_int* ExportState = nullptr;
		std::atomic_int* Progress = nullptr;
		std::atomic_int* Estimate = nullptr;

		// Output
		mesh OutputMesh;

		// Intermediaries
		std::mutex CS;
		std::vector<std::size_t> FirstLoopDomain;
		std::unordered_map<std::size_t, std::uint64_t> SecondLoopDomain;

		// Parallel thunks
		std::function<void(AsyncParallelSurfaceNets& Task, std::size_t)> FirstLoopThunk;
		std::function<void(AsyncParallelSurfaceNets& Task, GridPoint)> FirstLoopInnerThunk;
		std::function<void(AsyncParallelSurfaceNets& Task, std::pair<std::size_t const, std::uint64_t> const&)> SecondLoopThunk;

		// Populates intermediaries and thunks
		void Setup();
	};

	inline bool operator<(const AsyncParallelSurfaceNets::GridPoint& LHS, const AsyncParallelSurfaceNets::GridPoint& RHS)
	{
		return LHS.k < RHS.k || (LHS.k == RHS.k && LHS.j < RHS.j) || (LHS.k == RHS.k && LHS.j == RHS.j && LHS.i < RHS.i);
	}
// END TANGERINE MOD - Carve up `par_surface_nets` into parts that can be adapted for parallel exe w/ Tangerine's scheduler

// BEGIN TANGERINE MOD - Modify declarations to remove LIBIGL dependency
void surface_nets(
	std::function<float(float x, float y, float z)> const& implicit_function,
	regular_grid_t const& grid,
	mesh& out_mesh,
	std::atomic_bool& Live,
	float const isovalue = 0.f
);

void par_surface_nets(
	std::function<float(float x, float y, float z)> const& implicit_function,
	regular_grid_t const& grid,
	mesh& out_mesh,
	float const isovalue = 0.f
);

void par_surface_nets(
	std::function<float(float x, float y, float z)> const& implicit_function,
	regular_grid_t const& grid,
	mesh& out_mesh,
	std::atomic_bool& ExportActive,
	std::atomic_int& ExportState,
	std::atomic_int& Progress,
	std::atomic_int& Estimate,
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