#pragma once

namespace isosurface {

struct point_t
{
	float x, y, z;

	friend point_t operator*(float k, point_t p)
	{
		return point_t
		{
			k * p.x,
			k * p.y,
			k * p.z,
		};
	}

	friend point_t operator/(point_t p, float k)
	{
		return point_t
		{
			p.x / k,
			p.y / k,
			p.z / k
		};
	}

	point_t operator+(point_t const& other) const
	{
		return point_t
		{
			x + other.x,
			y + other.y,
			z + other.z
		};
	}

	point_t operator-(point_t const& other) const
	{
		return point_t
		{
			x - other.x,
			y - other.y,
			z - other.z
		};
	}
};

} // isosurface