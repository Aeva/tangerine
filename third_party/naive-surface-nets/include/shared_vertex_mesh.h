#pragma once

#include "point.h"

#include <vector>
// BEGIN TANGERINE MOD - Linux build fix
#include <cstdint>
// END TANGERINE MOD

namespace isosurface {

class shared_vertex_mesh
{
public:
	using vertex_t = point_t;
	struct triangle_t
	{
		std::uint64_t v0, v1, v2;
	};

	void add_vertex(point_t const& v) { vertices_.push_back(v); }
	void add_vertex(point_t&& v) { vertices_.emplace_back(v); }

	void add_face(triangle_t const& t) { faces_.push_back(t); }
	void add_face(triangle_t&& t) { faces_.emplace_back(t); }

	using vertices_container_type = std::vector<vertex_t>;
	using faces_container_type = std::vector<triangle_t>;

	typename vertices_container_type::const_iterator vertices_cbegin() const { return vertices_.cbegin(); }
	typename vertices_container_type::const_iterator vertices_cend() const { return vertices_.cend(); }

	typename vertices_container_type::iterator vertices_begin() { return vertices_.begin(); }
	typename vertices_container_type::iterator vertices_end() { return vertices_.end(); }
	
	typename faces_container_type::const_iterator faces_cbegin() const { return faces_.cbegin(); }
	typename faces_container_type::const_iterator faces_cend() const { return faces_.cend(); }

	typename faces_container_type::iterator faces_begin() { return faces_.begin(); }
	typename faces_container_type::iterator faces_end() { return faces_.end(); }

	std::size_t vertex_count() const { return std::distance(vertices_cbegin(), vertices_cend()); }
	std::size_t face_count() const { return std::distance(faces_cbegin(), faces_cend()); }

// BEGIN TANGERINE MOD - Holepunch goes kachunk!
#if 0
private:
#endif
// END TANGERINE MOD
	vertices_container_type vertices_;
	faces_container_type faces_;
};

} // isosurface