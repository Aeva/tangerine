#include "surface_nets.h"

#include <algorithm>
#include <array>
#include <execution>
#include <mutex>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace isosurface {

/**
 * Implements the naive surface nets algorithm which approximates the isosurface
 * of the given implicit function at the given isovalue in the given regular grid
 * by a triangle mesh.
 *
 * @author   Quoc-Minh Ton-That
 * @param    implicit_function    The implicit function defined over 3d space for which surface nets
 * extracts an isosurface.
 * @param    grid                 The regular grid that discretizes 3d space and contains the
 * isosurface to extract.
 * @param    isovalue             The isovalue used to extract the isosurface as the level-set
 * implicit_function(x,y,z)=isovalue.
 * @return Returns the vertices and faces of the naive surface nets mesh.
 */
 // BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
void surface_nets(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
    isosurface::mesh& mesh,
    float const isovalue)
{
// END TANGERINE MOD

    struct mesh_bounding_box_t
    {
        point_t min, max;
    };

    // bounding box of the mesh in coordinate frame of the mesh
    // clang-format off
    mesh_bounding_box_t const mesh_bounding_box{
        {grid.x                    , grid.y                    , grid.z                    },
        {grid.x + grid.sx * grid.dx, grid.y + grid.sy * grid.dy, grid.z + grid.sz * grid.dz}};
    // clang-format on

    // mapping from 3d coordinates to 1d
    auto const get_active_cube_index =
        [](std::size_t x, std::size_t y, std::size_t z, regular_grid_t const& grid) -> std::size_t {
        return x + (y * grid.sx) + (z * grid.sx * grid.sy);
    };

    auto const get_ijk_from_idx =
        [](std::size_t active_cube_index,
           regular_grid_t const& grid) -> std::tuple<std::size_t, std::size_t, std::size_t> {
        std::size_t i = (active_cube_index) % grid.sx;
        std::size_t j = (active_cube_index / grid.sx) % grid.sy;
        std::size_t k = (active_cube_index) / (grid.sx * grid.sy);
        return std::make_tuple(i, j, k);
    };

    // mapping from active cube indices to vertex indices of the generated mesh
    std::unordered_map<std::size_t, std::uint64_t> active_cube_to_vertex_index_map{};

    /*
     * Vertex generation and placement
     *
     * We visit every voxel of the voxel grid, that is every cube of the
     * regular 3d grid, and determine which ones are intersected by the
     * implicit surface defined by the implicit function. To do so, we
     * look for bipolar edges. Bipolar edges are edges for which their
     * vertices (v1,v2) have associated scalar values for which either:
     *
     * f(v1) >= isovalue and f(v2) < isovalue
     * or
     * f(v1) < isovalue and f(v2) >= isovalue
     *
     * is true, where f is the implicit function.
     *
     * Walking over every voxel, we look for bipolar edges, and if we
     * find at least one, the voxel is marked as an active cube. Every
     * active cube must generate one vertex of the resulting mesh that
     * resides in that cube. Computing the position of the generated vertex
     * in the active cube is what we call vertex placement.
     */
    // clang-format off
	for (std::size_t k = 0; k < grid.sz; ++k)
	for (std::size_t j = 0; j < grid.sy; ++j)
	for (std::size_t i = 0; i < grid.sx; ++i)
	{
		/*
		*
		* Visit every voxel in the regular grid with the following configuration
		*
		* Coordinate frame
		*
		*       z y
		*       |/
		*       o--x
		*
		* Voxel corner indices
		*
		*        7          6
		*        o----------o
		*       /|         /|
		*     4/ |       5/ |
		*     o--|-------o  |
		*     |  o-------|--o
		*     | /3       | /2
		*     |/         |/
		*     o----------o
		*     0          1
		*
		*/

		// coordinates of voxel corners in voxel grid coordinate frame
		point_t const voxel_corner_grid_positions[8] =
		{
			{ static_cast<float>(i)    , static_cast<float>(j)    , static_cast<float>(k)     },
			{ static_cast<float>(i + 1), static_cast<float>(j)    , static_cast<float>(k)     },
			{ static_cast<float>(i + 1), static_cast<float>(j + 1), static_cast<float>(k)     },
			{ static_cast<float>(i)    , static_cast<float>(j + 1), static_cast<float>(k)     },
			{ static_cast<float>(i)    , static_cast<float>(j)    , static_cast<float>(k + 1) },
			{ static_cast<float>(i + 1), static_cast<float>(j)    , static_cast<float>(k + 1) },
			{ static_cast<float>(i + 1), static_cast<float>(j + 1), static_cast<float>(k + 1) },
			{ static_cast<float>(i)    , static_cast<float>(j + 1), static_cast<float>(k + 1) },
		};

		// coordinates of voxel corners in the mesh's coordinate frame
		point_t const voxel_corner_positions[8] =
		{
			{ grid.x + i       * grid.dx, grid.y + j       * grid.dy, grid.z + k       * grid.dz },
			{ grid.x + (i + 1) * grid.dx, grid.y + j       * grid.dy, grid.z + k       * grid.dz },
			{ grid.x + (i + 1) * grid.dx, grid.y + (j + 1) * grid.dy, grid.z + k       * grid.dz },
			{ grid.x + i       * grid.dx, grid.y + (j + 1) * grid.dy, grid.z + k       * grid.dz },
			{ grid.x + i       * grid.dx, grid.y + j       * grid.dy, grid.z + (k + 1) * grid.dz },
			{ grid.x + (i + 1) * grid.dx, grid.y + j       * grid.dy, grid.z + (k + 1) * grid.dz },
			{ grid.x + (i + 1) * grid.dx, grid.y + (j + 1) * grid.dy, grid.z + (k + 1) * grid.dz },
			{ grid.x + i       * grid.dx, grid.y + (j + 1) * grid.dy, grid.z + (k + 1) * grid.dz }
		};

		// scalar values of the implicit function evaluated at cube vertices (voxel corners)
		float const voxel_corner_values[8] =
		{
			implicit_function(voxel_corner_positions[0].x, voxel_corner_positions[0].y, voxel_corner_positions[0].z),
			implicit_function(voxel_corner_positions[1].x, voxel_corner_positions[1].y, voxel_corner_positions[1].z),
			implicit_function(voxel_corner_positions[2].x, voxel_corner_positions[2].y, voxel_corner_positions[2].z),
			implicit_function(voxel_corner_positions[3].x, voxel_corner_positions[3].y, voxel_corner_positions[3].z),
			implicit_function(voxel_corner_positions[4].x, voxel_corner_positions[4].y, voxel_corner_positions[4].z),
			implicit_function(voxel_corner_positions[5].x, voxel_corner_positions[5].y, voxel_corner_positions[5].z),
			implicit_function(voxel_corner_positions[6].x, voxel_corner_positions[6].y, voxel_corner_positions[6].z),
			implicit_function(voxel_corner_positions[7].x, voxel_corner_positions[7].y, voxel_corner_positions[7].z)
		};

		// the edges provide indices to the corresponding current cube's vertices (voxel corners)
		std::size_t const edges[12][2] =
		{
			{ 0u, 1u },
			{ 1u, 2u },
			{ 2u, 3u },
			{ 3u, 0u },
			{ 4u, 5u },
			{ 5u, 6u },
			{ 6u, 7u },
			{ 7u, 4u },
			{ 0u, 4u },
			{ 1u, 5u },
			{ 2u, 6u },
			{ 3u, 7u }
		};

		auto const is_scalar_positive = [](float scalar, float isovalue) -> bool
		{
			return scalar >= isovalue;
		};

		auto const are_edge_scalars_bipolar = [&is_scalar_positive](float scalar1, float scalar2, float isovalue) -> bool
		{
			return is_scalar_positive(scalar1, isovalue) != is_scalar_positive(scalar2, isovalue);
		};

		bool const edge_bipolarity_array[12] =
		{
			are_edge_scalars_bipolar(voxel_corner_values[edges[0][0]], voxel_corner_values[edges[0][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[1][0]], voxel_corner_values[edges[1][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[2][0]], voxel_corner_values[edges[2][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[3][0]], voxel_corner_values[edges[3][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[4][0]], voxel_corner_values[edges[4][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[5][0]], voxel_corner_values[edges[5][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[6][0]], voxel_corner_values[edges[6][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[7][0]], voxel_corner_values[edges[7][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[8][0]], voxel_corner_values[edges[8][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[9][0]], voxel_corner_values[edges[9][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[10][0]], voxel_corner_values[edges[10][1]], isovalue),
			are_edge_scalars_bipolar(voxel_corner_values[edges[11][0]], voxel_corner_values[edges[11][1]], isovalue),
		};

		// an active voxel must have at least one bipolar edge
		bool const is_voxel_active = edge_bipolarity_array[0] ||
			edge_bipolarity_array[1] ||
			edge_bipolarity_array[2] ||
			edge_bipolarity_array[3] ||
			edge_bipolarity_array[4] ||
			edge_bipolarity_array[5] ||
			edge_bipolarity_array[6] ||
			edge_bipolarity_array[7] ||
			edge_bipolarity_array[8] ||
			edge_bipolarity_array[9] ||
			edge_bipolarity_array[10] ||
			edge_bipolarity_array[11];

		// cubes that are not active do not generate mesh vertices
		if (!is_voxel_active)
			continue;

		// store all edge intersection points with the implicit surface in voxel grid coordinates
		std::vector<point_t> edge_intersection_points;

		// visit every bipolar edge
		for (std::size_t e = 0; e < 12; ++e)
		{
			if (!edge_bipolarity_array[e])
				continue;

			// get points p1, p2 of the edge e in grid coordinates
			auto const p1 = voxel_corner_grid_positions[edges[e][0]];
			auto const p2 = voxel_corner_grid_positions[edges[e][1]];

			// get value of the implicit function at edge vertices
			auto const s1 = voxel_corner_values[edges[e][0]];
			auto const s2 = voxel_corner_values[edges[e][1]];

			// perform linear interpolation using implicit function
			// values at vertices
			auto const t = (isovalue - s1) / (s2 - s1);
			edge_intersection_points.emplace_back(
				p1 + t * (p2 - p1)
			);
		}

		/*
		* We approximate the generated mesh vertex using the geometric center of all
		* bipolar edges' intersection point with the implicit surface. The geometric
		* center if first computed in local voxel grid coordinates and will then be
		* mapped to the mesh's coordinates later.
		*/
		float const number_of_intersection_points = static_cast<float>(edge_intersection_points.size());
		point_t const sum_of_intersection_points = std::accumulate(
			edge_intersection_points.cbegin(),
			edge_intersection_points.cend(),
			point_t{ 0.f, 0.f, 0.f }
		);
		point_t const geometric_center_of_edge_intersection_points = sum_of_intersection_points / number_of_intersection_points;

		/*
		*
		* map geometric center from local grid coordinates to world coordinates
		*
		* with local grid (lp1, lp2) and world grid (wp1, wp2),
		* and a point lp in local grid mapped to wp in world grid,
		* we have that:
		*
		* (lp - lp1) / (lp2 - lp1) = (wp - wp1) / (wp2 - wp1),
		* Our local grid is the voxel grid with lp1=(0, 0, 0) and lp2=(sx, sy, sz),
		* and our world grid is the mesh's bounding box, wp1=(minx, miny, minz) and wp2=(maxx, maxy, maxz),
		* and we have a local point lp="geometric center of edge intersection points" mapped to wp,
		* which we are looking for, such that:
		*
		* wp = wp1 + (wp2 - wp1) * (lp - lp1) / (lp2 - lp1)
		*
		*/
		point_t const mesh_vertex =
		{
			mesh_bounding_box.min.x +
				(mesh_bounding_box.max.x - mesh_bounding_box.min.x) * (geometric_center_of_edge_intersection_points.x - 0.f) / (static_cast<float>(grid.sx) - 0.f),
			mesh_bounding_box.min.y +
				(mesh_bounding_box.max.y - mesh_bounding_box.min.y) * (geometric_center_of_edge_intersection_points.y - 0.f) / (static_cast<float>(grid.sy) - 0.f),
			mesh_bounding_box.min.z +
				(mesh_bounding_box.max.z - mesh_bounding_box.min.z) * (geometric_center_of_edge_intersection_points.z - 0.f) / (static_cast<float>(grid.sz) - 0.f),
		};

		/*
		* Store mapping from this active cube to the mesh's vertex index
		* for triangulation later on.
		*/
		std::size_t const active_cube_index = get_active_cube_index(i, j, k, grid);
		std::uint64_t const vertex_index = mesh.vertex_count();
		active_cube_to_vertex_index_map[active_cube_index] = vertex_index;

		mesh.add_vertex(mesh_vertex);
	}

	/*
	* Triangulation
	*
	* For triangulation, we need not iterate over every voxel. We simply
	* visit every active cube and look at neighbors with which there is
	* a possible triangulation to be made. In the surface nets algorithm,
	* a quad is generated when four active cubes share a common edge.
	* As such, at each iteration, we will look at neighboring voxels of
	* the current active cube, and if they are all active too, we
	* generate a quad and triangulate the quad.
	*/
	for (auto const& key_value : active_cube_to_vertex_index_map)
	{
		std::size_t   const active_cube_index = key_value.first;
		std::uint64_t const vertex_index = key_value.second;

		/*
		* Knowing active_cube_index = i + j*sx + k*sx*sy,
		* we can recover i,j,k using only active_cube_index and sx,sy using:
		* i = index % xmax
		* j = (index / xmax) % ymax
		* k = index / (xmax * ymax)
		*/
		auto const ijk = get_ijk_from_idx(active_cube_index, grid);
		std::size_t i = std::get<0>(ijk);
		std::size_t j = std::get<1>(ijk);
		std::size_t k = std::get<2>(ijk);

		auto const is_lower_boundary_cube = [](std::size_t i, std::size_t j, std::size_t k, regular_grid_t const& g)
		{
			return (i == 0 || j == 0 || k == 0);
		};

		/*
		* we define a  lower boundary cube to be a cube with (i=0       || j=0       || k=0	    )
		* we define an upper boundary cube to be a cube with (i >= sx-1 || j >= sy-1 || k >=sz-1)
		*
		* lower boundary cubes have missing neighbor voxels, so we don't triangulate
		* when the current voxel is a boundary cube. Our method of quad generation considers
		* the following potentially crossed edges of an active cube:
		*
		* Active cube
		*
		*        7          6
		*        o----------o
		*       /|         /|
		*     4/ |       5/ |
		*     o--|-------o  |
		*     |  o-------|--o
		*     | /3       | /2
		*     |/         |/
		*     o----------o
		*     0          1
		*
		* Potentially crossed edges
		*
		*     4
		*     o
		*     |  o
		*     | /3
		*     |/
		*     o----------o
		*     0          1
		*
		* By considering only these potential crossed edges for each active cube,
		* we make sure that every interior edge of the voxel grid is visited only
		* once. Additional voxel grid edges are also visited at upper boundary cubes.
		*
		* To make sure that these additional edge do not cause any problems,
		* we should make sure that the implicit surface
		* (level-set of the implicit function with level=isovalue)
		* never crosses those additional edges. One way to do that is to add an
		* outer layer to the voxel grid, that is to augment the grid dimensions
		* and center the implicit function in the augmented grid. That way, we
		* know for sure that the implicit surface only crossed interior edges of
		* the augmented grid (since we know it used to fit the non-augmented grid).
		* Currently, we leave this to the user to give us an implicit function for which
		* the level-set of the given isovalue defines an isosurface contained entirely
		* in interior voxel grid cubes or to give us an augmented grid otherwise.
		*
		*/
		if (is_lower_boundary_cube(i, j, k, grid))
			continue;

		/*
		*
		* In coordinate frame:
		*
		*     z y
		*     |/
		*     o--x
		*
		* For potentially crossed edges, we generate quads connecting the 4 cubes of
		* the crossed edge, so the considered neighbors are:
		*
		*
		*           o----------o----------o
		*          /|         /|         /|
		*         / |   0    / |        / | <---- this is the current active cube (i,j,k) in the above coordinate frame
		*        o--|-------o--|-------o  |
		*       /|  o------/|--o------/|--o
		*      / | /|1    / | /|2    / | /|
		*     o--|/-|--5-o--|/-|--4-o  |/ |
		*     |  o--|----|--o--|----|--o  |
		*     | /|  o----|-/|--o----|-/|--o
		*     |/ | /     |/ | /3    |/ | /
		*     o--|/------o--|/------o  |/
		*        o-------|--o-------|--o
		*                | /        | /
		*                |/         |/
		*                o----------o
		*
		* Indices of neighbor voxels are written on the top squares of the cubes.
		* So upper cubes are 0, 1, 2 and lower cubes are 3, 4, 5.
		*/
		std::size_t const neighbor_grid_positions[6][3] =
		{
			{ i - 1, j    , k     },
			{ i - 1, j - 1, k     },
			{ i    , j - 1, k     },
			{ i    , j - 1, k - 1 },
			{ i    , j    , k - 1 },
			{ i - 1, j    , k - 1 }
		};

		point_t const voxel_corners_of_interest[4] =
		{
			// vertex 0
			{ grid.x + i       * grid.dx, grid.y + j       * grid.dy, grid.z + k       * grid.dz },
			// vertex 4
			{ grid.x + i       * grid.dx, grid.y + j       * grid.dy, grid.z + (k + 1) * grid.dz },
			// vertex 3
			{ grid.x + i       * grid.dx, grid.y + (j + 1) * grid.dy, grid.z + k       * grid.dz },
			// vertex 1
			{ grid.x + (i + 1) * grid.dx, grid.y + j       * grid.dy, grid.z + k       * grid.dz }
		};

		float const edge_scalar_values[3][2] =
		{
			// directed edge (0,4)
			{
				implicit_function(voxel_corners_of_interest[0].x, voxel_corners_of_interest[0].y, voxel_corners_of_interest[0].z),
				implicit_function(voxel_corners_of_interest[1].x, voxel_corners_of_interest[1].y, voxel_corners_of_interest[1].z)
			},
			// directed edge (3,0)
			{
				implicit_function(voxel_corners_of_interest[2].x, voxel_corners_of_interest[2].y, voxel_corners_of_interest[2].z),
				implicit_function(voxel_corners_of_interest[0].x, voxel_corners_of_interest[0].y, voxel_corners_of_interest[0].z)
			},
			// directed edge (0,1)
			{
				implicit_function(voxel_corners_of_interest[0].x, voxel_corners_of_interest[0].y, voxel_corners_of_interest[0].z),
				implicit_function(voxel_corners_of_interest[3].x, voxel_corners_of_interest[3].y, voxel_corners_of_interest[3].z)
			}
		};

		/*
		* The three potentially generated quads all share the current
		* active cube's mesh vertex. Store the current active cube's
		* neighbors for each potentially generated quad.
		*
		* The order of the neighbors is such that the quads are generated
		* with an outward normal direction that is in the same direction
		* as the directed/oriented edges:
		* (0, 4), (3, 0) and (0, 1)
		*
		* Then, looking at the gradient of the implicit function along
		* the edge, we can flip the quad's outward normal direction
		* to match the gradient's direction.
		*/
		std::size_t const quad_neighbors[3][3] =
		{
			{ 0, 1, 2 },
			{ 0, 5, 4 },
			{ 2, 3, 4 }
		};

		/*
		* For directed edge e that has the same direction as the
		* gradient along e, the correct order of neighbor vertices
		* is 0,1,2. If the direction of e is opposite the gradient,
		* the correct order is 2,1,0.
		*
		* For current active cube's mesh vertex v, we will then have
		* quads: v,0,1,2 or v,2,1,0
		*/
		std::array<std::size_t, 3> const quad_neighbor_orders[2] =
		{
			{ 0, 1, 2 },
			{ 2, 1, 0 }
		};

		// look at each potentially generated quad
		for (std::size_t i = 0; i < 3; ++i)
		{
			auto const neighbor1 = get_active_cube_index(
				neighbor_grid_positions[quad_neighbors[i][0]][0],
				neighbor_grid_positions[quad_neighbors[i][0]][1],
				neighbor_grid_positions[quad_neighbors[i][0]][2],
				grid);

			auto const neighbor2 = get_active_cube_index(
				neighbor_grid_positions[quad_neighbors[i][1]][0],
				neighbor_grid_positions[quad_neighbors[i][1]][1],
				neighbor_grid_positions[quad_neighbors[i][1]][2],
				grid);

			auto const neighbor3 = get_active_cube_index(
				neighbor_grid_positions[quad_neighbors[i][2]][0],
				neighbor_grid_positions[quad_neighbors[i][2]][1],
				neighbor_grid_positions[quad_neighbors[i][2]][2],
				grid);

			/*
			* Only generate a quad if all neighbors are active cubes.
			* If a neighbor is an active cube, our mapping should
			* contain it.
			*/
			if (active_cube_to_vertex_index_map.count(neighbor1) == 0 ||
				active_cube_to_vertex_index_map.count(neighbor2) == 0 ||
				active_cube_to_vertex_index_map.count(neighbor3) == 0)
				continue;

			std::size_t const neighbor_vertices[3] =
			{
				active_cube_to_vertex_index_map.at(neighbor1),
				active_cube_to_vertex_index_map.at(neighbor2),
				active_cube_to_vertex_index_map.at(neighbor3)
			};

			/*
			* If the edge e=(v0,v1) has f(v1) >= f(v0), then
			* the gradient along e goes from v0 to v1, and
			* we use the first quad neighbor ordering.
			* Otherwise, the gradient goes from v1 to v0 and
			* we flip the quad's orientation by using the
			* second quad neighbor ordering.
			*/
			auto const& neighbor_vertices_order =
				edge_scalar_values[i][1] > edge_scalar_values[i][0] ?
				quad_neighbor_orders[0] :
				quad_neighbor_orders[1];

			/*
			* Generate the quad q = (v0,v1,v2,v3)
			*/
			auto const v0 = vertex_index;
			auto const v1 = neighbor_vertices[neighbor_vertices_order[0]];
			auto const v2 = neighbor_vertices[neighbor_vertices_order[1]];
			auto const v3 = neighbor_vertices[neighbor_vertices_order[2]];

			/*
			* Triangulate the quad q naively.
			*
			* To produce better quality triangulations,
			* we could also triangulate based on the
			* diagonal that minimizes the maximum triangle angle
			* to have better regularity in the mesh.
			*
			* We can also verify that the 2 generated triangles are
			* in the envelope of the current bipolar edge and if
			* not, generate 4 triangles instead of 2. The four
			* triangles are formed by creating diagonals between
			* all of the quad's corners (the mesh vertices) and
			* the current edge's intersection point. By "current
			* edge", we mean the edge shared by all 3 neighbor
			* active cubes and the current active cube.
			*/
			mesh.add_face({ v0, v1, v2 });
			mesh.add_face({ v0, v2, v3 });
		}
	}
    // clang-format on

// BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
#if 0
    return common::convert::from(mesh);
#endif
// END TANGERINE MOD
}

/**
 * Starting from here on, we will define many helper functions to reduce
 * code duplication in the optimized versions of surface nets. We don't
 * use these helper functions in the first naive surface nets implementation
 * so that we can explaine very step of that implementation. The optimized
 * versions don't need to repeat the same comments as in the non-optimized
 * version, so we simply reuse the helper functions there instead.
 */

point_t get_world_point_of(std::size_t i, std::size_t j, std::size_t k, regular_grid_t const& grid)
{
    return point_t{
        grid.x + static_cast<float>(i) * grid.dx,
        grid.y + static_cast<float>(j) * grid.dy,
        grid.z + static_cast<float>(k) * grid.dz};
};

auto get_grid_point_of(point_t const& p, regular_grid_t const& grid)
    -> std::tuple<std::size_t, std::size_t, std::size_t>
{
    // if x = grid.x + i*grid.dx, then i = (x - grid.x) / grid.dx
    return std::make_tuple(
        static_cast<std::size_t>((p.x - grid.x) / grid.dx),
        static_cast<std::size_t>((p.y - grid.y) / grid.dy),
        static_cast<std::size_t>((p.z - grid.z) / grid.dz));
};

std::size_t
get_active_cube_index(std::size_t x, std::size_t y, std::size_t z, regular_grid_t const& grid)
{
    return x + (y * grid.sx) + (z * grid.sx * grid.sy);
};

auto get_ijk_from_idx(std::size_t active_cube_index, regular_grid_t const& grid)
    -> std::tuple<std::size_t, std::size_t, std::size_t>
{
    std::size_t i = (active_cube_index) % grid.sx;
    std::size_t j = (active_cube_index / grid.sx) % grid.sy;
    std::size_t k = (active_cube_index) / (grid.sx * grid.sy);
    return std::make_tuple(i, j, k);
};

std::array<point_t, 8> get_voxel_corner_grid_positions(std::size_t i, std::size_t j, std::size_t k)
{
    auto const ifloat = static_cast<float>(i);
    auto const jfloat = static_cast<float>(j);
    auto const kfloat = static_cast<float>(k);
    return std::array<point_t, 8>{
        point_t{ifloat, jfloat, kfloat},
        point_t{ifloat + 1.f, jfloat, kfloat},
        point_t{ifloat + 1.f, jfloat + 1.f, kfloat},
        point_t{ifloat, jfloat + 1.f, kfloat},
        point_t{ifloat, jfloat, kfloat + 1.f},
        point_t{ifloat + 1.f, jfloat, kfloat + 1.f},
        point_t{ifloat + 1.f, jfloat + 1.f, kfloat + 1.f},
        point_t{ifloat, jfloat + 1.f, kfloat + 1.f}};
};

std::array<point_t, 8> get_voxel_corner_world_positions(
    std::size_t i,
    std::size_t j,
    std::size_t k,
    regular_grid_t const& grid)
{
    auto const ifloat = static_cast<float>(i);
    auto const jfloat = static_cast<float>(j);
    auto const kfloat = static_cast<float>(k);
    return std::array<point_t, 8>{
        point_t{grid.x + ifloat * grid.dx, grid.y + jfloat * grid.dy, grid.z + kfloat * grid.dz},
        point_t{
            grid.x + (ifloat + 1) * grid.dx,
            grid.y + jfloat * grid.dy,
            grid.z + kfloat * grid.dz},
        point_t{
            grid.x + (ifloat + 1) * grid.dx,
            grid.y + (jfloat + 1) * grid.dy,
            grid.z + kfloat * grid.dz},
        point_t{
            grid.x + ifloat * grid.dx,
            grid.y + (jfloat + 1) * grid.dy,
            grid.z + kfloat * grid.dz},
        point_t{
            grid.x + ifloat * grid.dx,
            grid.y + jfloat * grid.dy,
            grid.z + (kfloat + 1) * grid.dz},
        point_t{
            grid.x + (ifloat + 1) * grid.dx,
            grid.y + jfloat * grid.dy,
            grid.z + (kfloat + 1) * grid.dz},
        point_t{
            grid.x + (ifloat + 1) * grid.dx,
            grid.y + (jfloat + 1) * grid.dy,
            grid.z + (kfloat + 1) * grid.dz},
        point_t{
            grid.x + ifloat * grid.dx,
            grid.y + (jfloat + 1) * grid.dy,
            grid.z + (kfloat + 1) * grid.dz}};
};

std::array<float, 8> get_voxel_corner_values(
    std::array<point_t, 8> const& voxel_corner_world_positions,
    std::function<float(float, float, float)> const& implicit_function)
{
    return std::array<float, 8>{
        implicit_function(
            voxel_corner_world_positions[0].x,
            voxel_corner_world_positions[0].y,
            voxel_corner_world_positions[0].z),
        implicit_function(
            voxel_corner_world_positions[1].x,
            voxel_corner_world_positions[1].y,
            voxel_corner_world_positions[1].z),
        implicit_function(
            voxel_corner_world_positions[2].x,
            voxel_corner_world_positions[2].y,
            voxel_corner_world_positions[2].z),
        implicit_function(
            voxel_corner_world_positions[3].x,
            voxel_corner_world_positions[3].y,
            voxel_corner_world_positions[3].z),
        implicit_function(
            voxel_corner_world_positions[4].x,
            voxel_corner_world_positions[4].y,
            voxel_corner_world_positions[4].z),
        implicit_function(
            voxel_corner_world_positions[5].x,
            voxel_corner_world_positions[5].y,
            voxel_corner_world_positions[5].z),
        implicit_function(
            voxel_corner_world_positions[6].x,
            voxel_corner_world_positions[6].y,
            voxel_corner_world_positions[6].z),
        implicit_function(
            voxel_corner_world_positions[7].x,
            voxel_corner_world_positions[7].y,
            voxel_corner_world_positions[7].z)};
};

std::array<bool, 12> get_edge_bipolarity_array(
    std::array<float, 8> const& voxel_corner_values,
    float isovalue,
    std::uint8_t const edges[12][2])
{
    auto const is_scalar_positive = [&isovalue](float scalar) -> bool {
        return scalar >= isovalue;
    };

    auto const are_edge_scalars_bipolar =
        [&is_scalar_positive](float scalar1, float scalar2) -> bool {
        return is_scalar_positive(scalar1) != is_scalar_positive(scalar2);
    };

    std::array<bool, 12> const edge_bipolarity_array = {
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[0][0]],
            voxel_corner_values[edges[0][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[1][0]],
            voxel_corner_values[edges[1][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[2][0]],
            voxel_corner_values[edges[2][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[3][0]],
            voxel_corner_values[edges[3][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[4][0]],
            voxel_corner_values[edges[4][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[5][0]],
            voxel_corner_values[edges[5][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[6][0]],
            voxel_corner_values[edges[6][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[7][0]],
            voxel_corner_values[edges[7][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[8][0]],
            voxel_corner_values[edges[8][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[9][0]],
            voxel_corner_values[edges[9][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[10][0]],
            voxel_corner_values[edges[10][1]]),
        are_edge_scalars_bipolar(
            voxel_corner_values[edges[11][0]],
            voxel_corner_values[edges[11][1]])};

    return edge_bipolarity_array;
};

bool get_is_cube_active(std::array<bool, 12> const& edge_bipolarity_array)
{
    // clang-format off
    // an active voxel must have at least one bipolar edge
    bool const is_voxel_active = 
        edge_bipolarity_array[0] ||
        edge_bipolarity_array[1] ||
        edge_bipolarity_array[2] ||
        edge_bipolarity_array[3] ||
        edge_bipolarity_array[4] ||
        edge_bipolarity_array[5] ||
        edge_bipolarity_array[6] ||
        edge_bipolarity_array[7] ||
        edge_bipolarity_array[8] ||
        edge_bipolarity_array[9] ||
        edge_bipolarity_array[10] ||
        edge_bipolarity_array[11];
    // clang-format on

    return is_voxel_active;
};

std::array<std::array<std::size_t, 3>, 3> get_adjacent_cubes_of_edge(
    std::size_t i,
    std::size_t j,
    std::size_t k,
    std::size_t edge,
    std::int8_t const adjacent_cubes_of_edges[12][3][3])
{
    std::array<std::array<std::size_t, 3>, 3> adjacent_cubes;
    adjacent_cubes[0] = {
        i + adjacent_cubes_of_edges[edge][0][0],
        j + adjacent_cubes_of_edges[edge][0][1],
        k + adjacent_cubes_of_edges[edge][0][2]};
    adjacent_cubes[1] = {
        i + adjacent_cubes_of_edges[edge][1][0],
        j + adjacent_cubes_of_edges[edge][1][1],
        k + adjacent_cubes_of_edges[edge][1][2]};
    adjacent_cubes[2] = {
        i + adjacent_cubes_of_edges[edge][2][0],
        j + adjacent_cubes_of_edges[edge][2][1],
        k + adjacent_cubes_of_edges[edge][2][2]};
    return adjacent_cubes;
};

/**
 * @brief Implements naive surface nets algorithm in parallel
 * @param implicit_function
 * @param grid
 * @param isovalue
 * @return
 */
// BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
// I also moved the implementation of par_surface_nets out into its own implementation
// function for adding optional progress atomics
template<bool atomic_controls>
void par_surface_nets_inner(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
    isosurface::mesh& mesh,
    std::atomic_bool& ExportActive,
    std::atomic_int& ExportState,
    std::atomic_int& Progress,
    std::atomic_int& Estimate,
    float const isovalue)
{
// END TANGERINE MOD

    struct mesh_bounding_box_t
    {
        point_t min, max;
    };

    // bounding box of the mesh in coordinate frame of the mesh
    mesh_bounding_box_t const mesh_bounding_box{
        {grid.x, grid.y, grid.z},
        {grid.x + grid.sx * grid.dx, grid.y + grid.sy * grid.dy, grid.z + grid.sz * grid.dz}};

    // mapping from active cube indices to vertex indices of the generated mesh
    std::unordered_map<std::size_t, std::uint64_t> active_cube_to_vertex_index_map{};

    bool const is_x_longest_dimension = grid.sx > grid.sy && grid.sx > grid.sz;
    bool const is_y_longest_dimension = grid.sy > grid.sx && grid.sy > grid.sz;
    bool const is_z_longest_dimension = grid.sz > grid.sx && grid.sz > grid.sy;

    std::size_t longest_dimension_size =
        is_x_longest_dimension ? grid.sx : is_y_longest_dimension ? grid.sy : grid.sz;

    std::vector<std::size_t> ks(longest_dimension_size, 0u);
    std::iota(ks.begin(), ks.end(), 0u);

// BEGIN TANGERINE MOD - Atomic progress controls
    Estimate.store(longest_dimension_size);
// END TANGERINE MOD

    std::mutex sync;
    std::for_each(std::execution::par, ks.cbegin(), ks.cend(), [&](std::size_t k) {
        for (std::size_t j = 0; j < grid.sy; ++j)
        {
            for (std::size_t i = 0; i < grid.sx; ++i)
            {
// BEGIN TANGERINE MOD - Atomic progress controls
                if (atomic_controls && (!ExportActive.load() || ExportState.load() != 1))
                {
                    return;
                }
// END TANGERINE MOD

                if (is_x_longest_dimension)
                {
                    std::swap(i, k);
                }

                if (is_y_longest_dimension)
                {
                    std::swap(j, k);
                }

                // coordinates of voxel corners in voxel grid coordinate frame
                std::array<point_t, 8> const voxel_corner_grid_positions =
                    get_voxel_corner_grid_positions(i, j, k);

                // coordinates of voxel corners in the mesh's coordinate frame
                std::array<point_t, 8> const voxel_corner_world_positions =
                    get_voxel_corner_world_positions(i, j, k, grid);

                // scalar values of the implicit function evaluated at cube vertices (voxel corners)
                std::array<float, 8> const voxel_corner_values =
                    get_voxel_corner_values(voxel_corner_world_positions, implicit_function);

                // the edges provide indices to the corresponding current cube's vertices (voxel
                // corners)
                std::uint8_t constexpr edges[12][2] = {
                    {0u, 1u},
                    {1u, 2u},
                    {2u, 3u},
                    {3u, 0u},
                    {4u, 5u},
                    {5u, 6u},
                    {6u, 7u},
                    {7u, 4u},
                    {0u, 4u},
                    {1u, 5u},
                    {2u, 6u},
                    {3u, 7u}};

                std::array<bool, 12> const edge_bipolarity_array =
                    get_edge_bipolarity_array(voxel_corner_values, isovalue, edges);

                // an active voxel must have at least one bipolar edge
                bool const is_voxel_active = get_is_cube_active(edge_bipolarity_array);

                // cubes that are not active do not generate mesh vertices
                if (!is_voxel_active)
                    continue;

                // store all edge intersection points with the implicit surface in voxel grid
                // coordinates
                std::vector<point_t> edge_intersection_points;

                // visit every bipolar edge
                for (std::size_t e = 0; e < 12; ++e)
                {
                    if (!edge_bipolarity_array[e])
                        continue;

                    // get points p1, p2 of the edge e in grid coordinates
                    auto const p1 = voxel_corner_grid_positions[edges[e][0]];
                    auto const p2 = voxel_corner_grid_positions[edges[e][1]];

                    // get value of the implicit function at edge vertices
                    auto const s1 = voxel_corner_values[edges[e][0]];
                    auto const s2 = voxel_corner_values[edges[e][1]];

                    // perform linear interpolation using implicit function
                    // values at vertices
                    auto const t = (isovalue - s1) / (s2 - s1);
                    edge_intersection_points.emplace_back(p1 + t * (p2 - p1));
                }

                float const number_of_intersection_points =
                    static_cast<float>(edge_intersection_points.size());

                point_t const sum_of_intersection_points = std::accumulate(
                    edge_intersection_points.cbegin(),
                    edge_intersection_points.cend(),
                    point_t{0.f, 0.f, 0.f});

                point_t const geometric_center_of_edge_intersection_points =
                    sum_of_intersection_points / number_of_intersection_points;

                point_t const mesh_vertex = {
                    mesh_bounding_box.min.x +
                        (mesh_bounding_box.max.x - mesh_bounding_box.min.x) *
                            (geometric_center_of_edge_intersection_points.x - 0.f) /
                            (static_cast<float>(grid.sx) - 0.f),
                    mesh_bounding_box.min.y +
                        (mesh_bounding_box.max.y - mesh_bounding_box.min.y) *
                            (geometric_center_of_edge_intersection_points.y - 0.f) /
                            (static_cast<float>(grid.sy) - 0.f),
                    mesh_bounding_box.min.z +
                        (mesh_bounding_box.max.z - mesh_bounding_box.min.z) *
                            (geometric_center_of_edge_intersection_points.z - 0.f) /
                            (static_cast<float>(grid.sz) - 0.f),
                };

                std::size_t const active_cube_index = get_active_cube_index(i, j, k, grid);

                std::lock_guard<std::mutex> lock{sync};
                std::uint64_t const vertex_index                   = mesh.vertex_count();
                active_cube_to_vertex_index_map[active_cube_index] = vertex_index;

                mesh.add_vertex(mesh_vertex);
            }
        }
// BEGIN TANGERINE MOD - Atomic progress controls
        if (atomic_controls)
        {
            Progress.fetch_add(1);
        }
// END TANGERINE MOD
    });

// BEGIN TANGERINE MOD - Atomic progress controls
    if (atomic_controls)
    {
        Estimate.fetch_add(active_cube_to_vertex_index_map.size());
    }
// END TANGERINE MOD

    std::for_each(
        std::execution::par,
        active_cube_to_vertex_index_map.cbegin(),
        active_cube_to_vertex_index_map.cend(),
        [&](std::pair<std::size_t const, std::uint64_t> const& key_value) {
// BEGIN TANGERINE MOD - Atomic progress controls
            if (atomic_controls && (!ExportActive.load() || ExportState.load() != 1))
            {
                return;
            }
// END TANGERINE MOD
            std::size_t const active_cube_index = key_value.first;
            std::uint64_t const vertex_index    = key_value.second;

            auto const ijk = get_ijk_from_idx(active_cube_index, grid);
            std::size_t i  = std::get<0>(ijk);
            std::size_t j  = std::get<1>(ijk);
            std::size_t k  = std::get<2>(ijk);

            auto const is_lower_boundary_cube =
                [](std::size_t i, std::size_t j, std::size_t k, regular_grid_t const& g) {
                    return (i == 0 || j == 0 || k == 0);
                };

            if (is_lower_boundary_cube(i, j, k, grid))
                return;

            std::size_t const neighbor_grid_positions[6][3] = {
                {i - 1, j, k},
                {i - 1, j - 1, k},
                {i, j - 1, k},
                {i, j - 1, k - 1},
                {i, j, k - 1},
                {i - 1, j, k - 1}};

            point_t const voxel_corners_of_interest[4] = {// vertex 0
                                                          get_world_point_of(i, j, k, grid),
                                                          // vertex 4
                                                          get_world_point_of(i, j, k + 1, grid),
                                                          // vertex 3
                                                          get_world_point_of(i, j + 1, k, grid),
                                                          // vertex 1
                                                          get_world_point_of(i + 1, j, k, grid)};

            float const edge_scalar_values[3][2] = {// directed edge (0,4)
                                                    {implicit_function(
                                                         voxel_corners_of_interest[0].x,
                                                         voxel_corners_of_interest[0].y,
                                                         voxel_corners_of_interest[0].z),
                                                     implicit_function(
                                                         voxel_corners_of_interest[1].x,
                                                         voxel_corners_of_interest[1].y,
                                                         voxel_corners_of_interest[1].z)},
                                                    // directed edge (3,0)
                                                    {implicit_function(
                                                         voxel_corners_of_interest[2].x,
                                                         voxel_corners_of_interest[2].y,
                                                         voxel_corners_of_interest[2].z),
                                                     implicit_function(
                                                         voxel_corners_of_interest[0].x,
                                                         voxel_corners_of_interest[0].y,
                                                         voxel_corners_of_interest[0].z)},
                                                    // directed edge (0,1)
                                                    {implicit_function(
                                                         voxel_corners_of_interest[0].x,
                                                         voxel_corners_of_interest[0].y,
                                                         voxel_corners_of_interest[0].z),
                                                     implicit_function(
                                                         voxel_corners_of_interest[3].x,
                                                         voxel_corners_of_interest[3].y,
                                                         voxel_corners_of_interest[3].z)}};

            std::size_t const quad_neighbors[3][3] = {{0, 1, 2}, {0, 5, 4}, {2, 3, 4}};

            std::array<std::size_t, 3> const quad_neighbor_orders[2] = {{0, 1, 2}, {2, 1, 0}};

            for (std::size_t i = 0; i < 3; ++i)
            {
                auto const neighbor1 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[i][0]][0],
                    neighbor_grid_positions[quad_neighbors[i][0]][1],
                    neighbor_grid_positions[quad_neighbors[i][0]][2],
                    grid);

                auto const neighbor2 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[i][1]][0],
                    neighbor_grid_positions[quad_neighbors[i][1]][1],
                    neighbor_grid_positions[quad_neighbors[i][1]][2],
                    grid);

                auto const neighbor3 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[i][2]][0],
                    neighbor_grid_positions[quad_neighbors[i][2]][1],
                    neighbor_grid_positions[quad_neighbors[i][2]][2],
                    grid);

                if (active_cube_to_vertex_index_map.count(neighbor1) == 0 ||
                    active_cube_to_vertex_index_map.count(neighbor2) == 0 ||
                    active_cube_to_vertex_index_map.count(neighbor3) == 0)
                    continue;

                std::size_t const neighbor_vertices[3] = {
                    active_cube_to_vertex_index_map.at(neighbor1),
                    active_cube_to_vertex_index_map.at(neighbor2),
                    active_cube_to_vertex_index_map.at(neighbor3)};

                auto const& neighbor_vertices_order =
                    edge_scalar_values[i][1] > edge_scalar_values[i][0] ? quad_neighbor_orders[0] :
                                                                          quad_neighbor_orders[1];

                auto const v0 = vertex_index;
                auto const v1 = neighbor_vertices[neighbor_vertices_order[0]];
                auto const v2 = neighbor_vertices[neighbor_vertices_order[1]];
                auto const v3 = neighbor_vertices[neighbor_vertices_order[2]];

                std::lock_guard<std::mutex> lock{sync};
                mesh.add_face({v0, v1, v2});
                mesh.add_face({v0, v2, v3});
            }
// BEGIN TANGERINE MOD - Atomic progress controls
            if (atomic_controls)
            {
                Progress.fetch_add(1);
            }
// END TANGERINE MOD
        });
    // clang-format on
// BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
#if 0
    return common::convert::from(mesh);
#endif
// END TANGERINE MOD
}

// BEGIN TANGERINE MOD - Moving internals to another function to add progress atomics
/**
 * @brief Implements naive surface nets algorithm in parallel
 * @param implicit_function
 * @param grid
 * @param isovalue
 * @return
 */
void par_surface_nets(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
    isosurface::mesh& mesh,
    float const isovalue)
{
    std::atomic_bool ExportActive(true);
    std::atomic_int ExportState(1);
    std::atomic_int Progress(0);
    std::atomic_int Estimate(0);
    par_surface_nets_inner<false>(implicit_function, grid, mesh, ExportActive, ExportState, Progress, Estimate, isovalue);
}

void par_surface_nets(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
    mesh& mesh,
    std::atomic_bool& ExportActive,
    std::atomic_int& ExportState,
    std::atomic_int& Progress,
    std::atomic_int& Estimate,
    float const isovalue)
{
    par_surface_nets_inner<true>(implicit_function, grid, mesh, ExportActive, ExportState, Progress, Estimate, isovalue);
}
// END TANGERINE MOD

/**
 * @brief
 * Implements naive surface nets optimized for cases where
 * you know approximately in which neighborhood there is
 * a voxel that intersects the surface. This surface_nets
 * overload takes a "hint", which is a point is 3d space.
 *
 * This modified algorithm starts by performing a breadth
 * first search of the voxel grid starting from that 3d
 * point in space, looking at neighboring cubes as neighbor
 * vertices in a graph.
 *
 * The breadth first search stops when
 * the first active cube is found (the first cube intersecting
 * the surface). This means that if the given hint was not
 * too far off the surface, then the breadth first search
 * will terminate very quickly. If the hint was very far
 * from the surface, the breadth first search will be very
 * slow.
 *
 * Once the breadth first search has found an active cube,
 * we can now start a new breadth first search, but now,
 * we only consider neighboring cubes that share a bipolar
 * edge with the current cube. As such, we will only ever
 * iterate on active cubes. This means that iteration
 * in this second breadth first search will only ever
 * iterate on vertices of the resulting mesh. The time
 * complexity for the search is then linear in the number of
 * resulting vertices of the mesh. This is a lot better
 * than iterating over every single voxel in the regular
 * grid, which can get very long very fast.
 *
 * Finally, we triangulate the surface in the same way
 * as before.
 *
 * Another issue with this version of surface nets is that
 * if the surface has different separated regions in the
 * grid, then we will only mesh the region in which the
 * active cube found by breadth first search is part of.
 * For example, let's say the isosurface described by the
 * implicit function is actually two separate sphere, one
 * to the left of the grid, and the other to the right,
 * both not touching each other. If our breadth first search
 * finds an active cube in the left sphere, then only the
 * left sphere will be meshed. If the found active cube is
 * on the right sphere, then only the right sphere will be
 * meshed.
 *
 * @param implicit_function
 * @param grid
 * @param hint Point in 3d space that is known to be close to the surface
 * @param isovalue
 * @return
 */
 // BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
void surface_nets(
    std::function<float(float x, float y, float z)> const& implicit_function,
    regular_grid_t const& grid,
    point_t const& hint,
    isosurface::mesh& mesh,
    float const isovalue,
    std::size_t max_size_of_breadth_first_search_queue)
{
    using point_type  = point_t;
    using scalar_type = float;
// END TANGERINE MOD

    // bounding box of the mesh in coordinate frame of the mesh
    struct mesh_bounding_box_t
    {
        point_type min, max;
    };

    // bounding box of the mesh in coordinate frame of the mesh
    mesh_bounding_box_t const mesh_aabb = {
        {grid.x, grid.y, grid.z},
        {grid.x + static_cast<scalar_type>(grid.sx) * grid.dx,
         grid.y + static_cast<scalar_type>(grid.sy) * grid.dy,
         grid.z + static_cast<scalar_type>(grid.sz) * grid.dz}};

    // setup useful functions for surface nets active cube search

    std::uint8_t constexpr edges[12][2] = {
        {0u, 1u},
        {1u, 2u},
        {2u, 3u},
        {3u, 0u},
        {4u, 5u},
        {5u, 6u},
        {6u, 7u},
        {7u, 4u},
        {0u, 4u},
        {1u, 5u},
        {2u, 6u},
        {3u, 7u}};

    // use hint as starting point of a breadth first search for all active cubes
    struct active_cube_t
    {
        std::size_t idx;
        std::array<scalar_type, 8> voxel_corner_values;
        std::uint64_t vertex_idx;
    };

    auto const make_cube = [&grid,
                            &implicit_function](std::size_t i, std::size_t j, std::size_t k) {
        active_cube_t cube{};
        cube.idx                                = get_active_cube_index(i, j, k, grid);
        auto const voxel_corner_world_positions = get_voxel_corner_world_positions(i, j, k, grid);
        cube.voxel_corner_values =
            get_voxel_corner_values(voxel_corner_world_positions, implicit_function);
        return cube;
    };

    // perform breadth first search starting from the hint in grid coordinates
    using bfs_queue_type = std::queue<active_cube_t>;
    using visited_type   = std::unordered_set<std::size_t>;

    auto const clear_memory = [](bfs_queue_type& queue, visited_type& visited) {
        bfs_queue_type empty_queue{};
        queue.swap(empty_queue);
        visited_type empty_set{};
        visited.swap(empty_set);
    };

    // search for the first active cube around the hint
    auto const [ihint, jhint, khint] = get_grid_point_of(hint, grid);
    visited_type visited{};
    bfs_queue_type bfs_queue{};
    active_cube_t root;
    bfs_queue.push(make_cube(ihint, jhint, khint));
    while (!bfs_queue.empty())
    {
        // just do normal surface nets if the search takes too much time
        if (bfs_queue.size() == max_size_of_breadth_first_search_queue)
// BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
            return par_surface_nets(implicit_function, grid, mesh, isovalue);
// END TANGERINE MOD

        active_cube_t cube = bfs_queue.front();
        bfs_queue.pop();

        visited.insert(cube.idx);
        auto const edge_bipolarity_array =
            get_edge_bipolarity_array(cube.voxel_corner_values, isovalue, edges);
        bool const is_cube_active = get_is_cube_active(edge_bipolarity_array);
        if (is_cube_active)
        {
            root = cube;
            break;
        }

        // if cube is inactive, add cube neighbors to the search queue if they haven't been visited
        // yet
        auto const [i, j, k]                  = get_ijk_from_idx(cube.idx, grid);
        std::size_t const neighbor_ijks[6][3] = {
            {i + 1, j, k},
            {i - 1, j, k},
            {i, j + 1, k},
            {i, j - 1, k},
            {i, j, k + 1},
            {i, j, k - 1}};
        std::uint64_t const neighbor_indexes[6] = {
            get_active_cube_index(
                neighbor_ijks[0][0],
                neighbor_ijks[0][1],
                neighbor_ijks[0][2],
                grid),
            get_active_cube_index(
                neighbor_ijks[1][0],
                neighbor_ijks[1][1],
                neighbor_ijks[1][2],
                grid),
            get_active_cube_index(
                neighbor_ijks[2][0],
                neighbor_ijks[2][1],
                neighbor_ijks[2][2],
                grid),
            get_active_cube_index(
                neighbor_ijks[3][0],
                neighbor_ijks[3][1],
                neighbor_ijks[3][2],
                grid),
            get_active_cube_index(
                neighbor_ijks[4][0],
                neighbor_ijks[4][1],
                neighbor_ijks[4][2],
                grid),
            get_active_cube_index(
                neighbor_ijks[5][0],
                neighbor_ijks[5][1],
                neighbor_ijks[5][2],
                grid)};

        for (auto l = 0u; l < 6u; ++l)
        {
            if (visited.count(neighbor_indexes[l]) == 0u)
            {
                bfs_queue.push(
                    make_cube(neighbor_ijks[l][0], neighbor_ijks[l][1], neighbor_ijks[l][2]));
            }
        }
    }
    clear_memory(bfs_queue, visited);

    // if an edge is bipolar, it means that
    // the other three cubes sharing this edges are
    // also active
    std::int8_t constexpr adjacent_cubes_of_edges[12][3][3] = {
        {{0, -1, 0}, {0, -1, -1}, {0, 0, -1}},
        {{1, 0, 0}, {1, 0, -1}, {0, 0, -1}},
        {{0, 1, 0}, {0, 1, -1}, {0, 0, -1}},
        {{-1, 0, 0}, {-1, 0, -1}, {0, 0, -1}},
        {{0, -1, 0}, {0, -1, 1}, {0, 0, 1}},
        {{1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
        {{-1, 0, 0}, {-1, 0, 1}, {0, 0, 1}},
        {{-1, 0, 0}, {-1, -1, 0}, {0, -1, 0}},
        {{1, 0, 0}, {1, -1, 0}, {0, -1, 0}},
        {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{-1, 0, 0}, {-1, 1, 0}, {0, 1, 0}},
    };

    // Now look at active cubes only which are connected to the root
    // and perform vertex placement. This is the main iteration loop
    // over all active cubes connected to the root active cube.
    std::unordered_map<std::size_t, active_cube_t> active_cubes_map{};
    bfs_queue.push(root);
    while (!bfs_queue.empty())
    {
        active_cube_t active_cube = bfs_queue.front();
        bfs_queue.pop();
        if (active_cubes_map.count(active_cube.idx) == 1)
            continue;

        auto const [i, j, k]                   = get_ijk_from_idx(active_cube.idx, grid);
        auto const voxel_corner_grid_positions = get_voxel_corner_grid_positions(i, j, k);

        auto const edge_bipolarity_array =
            get_edge_bipolarity_array(active_cube.voxel_corner_values, isovalue, edges);

        std::vector<point_type> edge_intersection_points;

        // visit every bipolar edge
        for (std::size_t e = 0; e < 12; ++e)
        {
            if (!edge_bipolarity_array[e])
                continue;

            // since this edge is bipolar, all cubes adjacent to it
            // are also active, so we add them to the queue
            auto const adjacent_cubes_of_edge =
                get_adjacent_cubes_of_edge(i, j, k, e, adjacent_cubes_of_edges);

            auto const c1 = get_active_cube_index(
                adjacent_cubes_of_edge[0][0],
                adjacent_cubes_of_edge[0][1],
                adjacent_cubes_of_edge[0][2],
                grid);
            auto const c2 = get_active_cube_index(
                adjacent_cubes_of_edge[1][0],
                adjacent_cubes_of_edge[1][1],
                adjacent_cubes_of_edge[1][2],
                grid);
            auto const c3 = get_active_cube_index(
                adjacent_cubes_of_edge[2][0],
                adjacent_cubes_of_edge[2][1],
                adjacent_cubes_of_edge[2][2],
                grid);

            if (active_cubes_map.count(c1) == 0)
            {
                bfs_queue.push(make_cube(
                    adjacent_cubes_of_edge[0][0],
                    adjacent_cubes_of_edge[0][1],
                    adjacent_cubes_of_edge[0][2]));
            }
            if (active_cubes_map.count(c2) == 0)
            {
                bfs_queue.push(make_cube(
                    adjacent_cubes_of_edge[1][0],
                    adjacent_cubes_of_edge[1][1],
                    adjacent_cubes_of_edge[1][2]));
            }
            if (active_cubes_map.count(c3) == 0)
            {
                bfs_queue.push(make_cube(
                    adjacent_cubes_of_edge[2][0],
                    adjacent_cubes_of_edge[2][1],
                    adjacent_cubes_of_edge[2][2]));
            }

            // get points p1, p2 of the edge e in grid coordinates
            auto const p1 = voxel_corner_grid_positions[edges[e][0]];
            auto const p2 = voxel_corner_grid_positions[edges[e][1]];

            // get value of the implicit function at edge vertices
            auto const s1 = active_cube.voxel_corner_values[edges[e][0]];
            auto const s2 = active_cube.voxel_corner_values[edges[e][1]];

            // perform linear interpolation using implicit function
            // values at vertices
            auto const t = (isovalue - s1) / (s2 - s1);
            edge_intersection_points.push_back(point_type{p1 + t * (p2 - p1)});
        }

        scalar_type const number_of_intersection_points =
            static_cast<scalar_type>(edge_intersection_points.size());
        point_type const sum_of_intersection_points = std::accumulate(
            edge_intersection_points.cbegin(),
            edge_intersection_points.cend(),
            point_type{0.f, 0.f, 0.f});
        point_type const geometric_center_of_edge_intersection_points =
            sum_of_intersection_points / number_of_intersection_points;

        point_type const mesh_vertex = {
            mesh_aabb.min.x + (mesh_aabb.max.x - mesh_aabb.min.x) *
                                  (geometric_center_of_edge_intersection_points.x - 0.f) /
                                  (static_cast<scalar_type>(grid.sx) - 0.f),
            mesh_aabb.min.y + (mesh_aabb.max.y - mesh_aabb.min.y) *
                                  (geometric_center_of_edge_intersection_points.y - 0.f) /
                                  (static_cast<scalar_type>(grid.sy) - 0.f),
            mesh_aabb.min.z + (mesh_aabb.max.z - mesh_aabb.min.z) *
                                  (geometric_center_of_edge_intersection_points.z - 0.f) /
                                  (static_cast<scalar_type>(grid.sz) - 0.f),
        };

        auto const vertex_index           = mesh.vertex_count();
        active_cube.vertex_idx            = vertex_index;
        active_cubes_map[active_cube.idx] = active_cube;

        mesh.add_vertex(mesh_vertex);
    }

    using key_value_type = typename decltype(active_cubes_map)::value_type;

    // triangulate the same way as before
    std::mutex sync;
    std::for_each(
        std::execution::par,
        active_cubes_map.cbegin(),
        active_cubes_map.cend(),
        [&](key_value_type const& key_value) {
            auto const active_cube_index = key_value.first;
            auto const active_cube       = key_value.second;

            auto const is_lower_boundary_cube = [](auto i, auto j, auto k) {
                return (i == 0 || j == 0 || k == 0);
            };

            auto const [i, j, k] = get_ijk_from_idx(active_cube_index, grid);

            if (is_lower_boundary_cube(i, j, k))
                return;

            // clang-format off
            std::size_t const neighbor_grid_positions[6][3] = {
                {i - 1, j,     k},
                {i - 1, j - 1, k},
                {i,     j - 1, k},
                {i,     j - 1, k - 1},
                {i,     j,     k - 1},
                {i - 1, j,     k - 1}};

            point_type const voxel_corners_of_interest[4] = {
                // vertex 0,
                get_world_point_of(i, j, k, grid),
                // vertex 4
                get_world_point_of(i, j, k+1, grid),
                // vertex 3
                get_world_point_of(i, j+1, k, grid),
                // vertex 1
                get_world_point_of(i+1, j, k, grid)
            };
            // clang-format on

            scalar_type const edge_scalar_values[3][2] = {
                // directed edge (0,4)
                {active_cube.voxel_corner_values[0], active_cube.voxel_corner_values[4]},
                // directed edge (3,0)
                {active_cube.voxel_corner_values[3], active_cube.voxel_corner_values[0]},
                // directed edge (0,1)
                {active_cube.voxel_corner_values[0], active_cube.voxel_corner_values[1]}};

            std::size_t const quad_neighbors[3][3] = {{0, 1, 2}, {0, 5, 4}, {2, 3, 4}};

            std::array<std::size_t, 3> const quad_neighbor_orders[2] = {{0, 1, 2}, {2, 1, 0}};

            for (std::size_t idx = 0; idx < 3; ++idx)
            {
                auto const neighbor1 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[idx][0]][0],
                    neighbor_grid_positions[quad_neighbors[idx][0]][1],
                    neighbor_grid_positions[quad_neighbors[idx][0]][2],
                    grid);

                auto const neighbor2 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[idx][1]][0],
                    neighbor_grid_positions[quad_neighbors[idx][1]][1],
                    neighbor_grid_positions[quad_neighbors[idx][1]][2],
                    grid);

                auto const neighbor3 = get_active_cube_index(
                    neighbor_grid_positions[quad_neighbors[idx][2]][0],
                    neighbor_grid_positions[quad_neighbors[idx][2]][1],
                    neighbor_grid_positions[quad_neighbors[idx][2]][2],
                    grid);

                if (active_cubes_map.count(neighbor1) == 0 ||
                    active_cubes_map.count(neighbor2) == 0 ||
                    active_cubes_map.count(neighbor3) == 0)
                    continue;

                std::size_t const neighbor_vertices[3] = {
                    active_cubes_map[neighbor1].vertex_idx,
                    active_cubes_map[neighbor2].vertex_idx,
                    active_cubes_map[neighbor3].vertex_idx};

                auto const& neighbor_vertices_order =
                    edge_scalar_values[idx][1] > edge_scalar_values[idx][0] ?
                        quad_neighbor_orders[0] :
                        quad_neighbor_orders[1];

                auto const v0 = active_cube.vertex_idx;
                auto const v1 = neighbor_vertices[neighbor_vertices_order[0]];
                auto const v2 = neighbor_vertices[neighbor_vertices_order[1]];
                auto const v3 = neighbor_vertices[neighbor_vertices_order[2]];

                std::lock_guard<std::mutex> lock{sync};

                mesh.add_face({v0, v1, v2});
                mesh.add_face({v0, v2, v3});
            }
        });

// BEGIN TANGERINE MOD - Modify function to remove LIBIGL dependency
#if 0
    return common::convert::from(mesh);
#endif
// END TANGERINE MOD
}

} // namespace isosurface
