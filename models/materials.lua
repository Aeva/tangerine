
-- Copyright 2023 Aeva Palecek
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.


local basic_material = pbrbr_material("#eee")
local inner_material = solid_material("red")
local fancy_material = normal_debug_material()


model = sphere(2):paint(basic_material)
	:diff(
		sphere(1.25))
	:union(
		sphere(1):paint(fancy_material):move_x(1.25),
		sphere(1):paint(fancy_material):move_x(-1.25),
		sphere(1):paint(fancy_material):move_y(1.25),
		sphere(1):paint(fancy_material):move_y(-1.25),
		sphere(1):paint(fancy_material):move_z(1.25),
		sphere(1):paint(fancy_material):move_z(-1.25))
	:blend_diff(
		cube(2):align(-1, 1, -1), .1)
	:union(
		sphere(1):paint(inner_material))


function bezier (points)
	-- Create a bezier spline function from a set of points.

	function inner (points, alpha)
		if #points < 2 then
			return nil

		elseif #points == 2 then
			return lerp(points[1], points[2], alpha)

		else
			local merged = {}
			for p = 1, #points - 1, 1 do
				table.insert(merged, inner({ points[p], points[p+1] }, alpha))
			end
			return inner(merged, alpha)
		end
	end

	return function (alpha)
		return inner(points, alpha)
	end
end


function fract(n)
	-- This follows GLSL's definition:
	-- https://registry.khronos.org/OpenGL-Refpages/gl4/html/fract.xhtml
	return n - math.floor(n)
end


function pdqhue (hue)
	-- A very approximate hue-to-RGB conversion.
	local a = 1
	local b = 1.5
	local c = bezier({
		vec3(a, 0, 0),
		vec3(b, b, 0),
		vec3(0, a, 0),
		vec3(0, b, b),
		vec3(0, 0, a),
		vec3(b, 0, b),
		vec3(a, 0, 0)})
	return c(fract(hue))
end


set_advance_event(function (dt, elapsed_ms)
	-- Modulate the hue of the inner material.
	local elapsed_s = elapsed_ms / 1000
	local period_s = 5
	local phase = (elapsed_s / period_s)
	inner_material:set_color(pdqhue(phase))
end)
