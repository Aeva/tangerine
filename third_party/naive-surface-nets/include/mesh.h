#pragma once

#include <shared_vertex_mesh.h>

// BEGIN TANGERINE MOD - Remove functionality specific to LIBIGL
#if 0
#include <Eigen/Dense>
#endif
// END TANGERINE MOD

#include <algorithm>

namespace isosurface {

using mesh = shared_vertex_mesh;

} // isosurface

// BEGIN TANGERINE MOD - Remove functionality specific to LIBIGL
#if 0
namespace common {

struct igl_triangle_mesh
{
	Eigen::MatrixX3d V;
	Eigen::MatrixX3i F;
};

namespace convert {

common::igl_triangle_mesh inline from(isosurface::shared_vertex_mesh const& mesh)
{
	using vidx_type = typename decltype(mesh.vertices_cbegin())::difference_type;
	using fidx_type = typename decltype(mesh.faces_cbegin())::difference_type;

	using vertex_type = std::iterator_traits<decltype(mesh.vertices_cbegin())>::value_type;
	using triangle_type = std::iterator_traits<decltype(mesh.faces_cbegin())>::value_type;

	using vertex_iterator_type = decltype(mesh.vertices_cbegin());
	using face_iterator_type = decltype(mesh.faces_cbegin());

	vertex_iterator_type vertices_begin = mesh.vertices_cbegin();
	vertex_iterator_type vertices_end = mesh.vertices_cend();

	face_iterator_type faces_begin = mesh.faces_cbegin();
	face_iterator_type faces_end = mesh.faces_cend();

	common::igl_triangle_mesh igl_mesh;
	auto const vertex_count = std::distance(vertices_begin, vertices_end);
	auto const triangle_count = std::distance(faces_begin, faces_end);

	igl_mesh.V.resize(vertex_count, 3);
	igl_mesh.F.resize(triangle_count, 3);

	vertex_iterator_type vit = vertices_begin;
	for (vidx_type vi = 0; vi < vertex_count; ++vi, ++vit)
	{
		vertex_type vertex = *vit;
		igl_mesh.V.block(vi, 0, 1, 3) = Eigen::RowVector3d(
			static_cast<double>(vertex.x), 
			static_cast<double>(vertex.y), 
			static_cast<double>(vertex.z));
	}

	face_iterator_type fit = faces_begin;
	for (fidx_type fi = 0; fi < triangle_count; ++fi, ++fit)
	{
		triangle_type triangle = *fit;
		igl_mesh.F.block(fi, 0, 1, 3) = Eigen::RowVector3i(
			static_cast<int>(triangle.v0), 
			static_cast<int>(triangle.v1), 
			static_cast<int>(triangle.v2));
	}

	return igl_mesh;
}

} // convert
} // common
#endif
// END TANGERINE MOD