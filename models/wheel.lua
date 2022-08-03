
-- Copyright 2022 Aeva Palecek
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


function spin_steps (rotate_fn, steps, stamp, csg_op)
	local csg_op = csg_op or union
	local step = 360.0 / steps
	local acc = stamp
	for angle = step, 360, step do
		acc = csg_op(acc, rotate_fn(stamp, angle))
	end
	return acc
end


function tire ()
	local tread =
		cube(1.)
		:diff(cube(1.):move(0.125, 0.125, 0.125))
		:rotate_z(45)
		:move_z(0.75)
	local tree = spin_steps(rotate_x, 18, tread)

	return
		spin_steps(rotate_x, 18, tread)
		:inter(
			sphere(2.55))
		:diff(
			sphere(4):move_x(2.2),
			sphere(4):move_x(-2.2))
		:blend_union(
			blend_inter(
				sphere(2.45),
				blend_union(
					torus(2.6, .8):move_z(.24),
					torus(2.6, .8):move_z(-.24),
					0.5)
				:rotate_y(90),
				0.1),
			0.05)
		:diff(
			sphere(1.6))
end


function hub ()
	local outset = 0.55
	local diameter = 1.25
	local lip = 0.075
	local lip_blend = lip / 3
	local inner_diameter = diameter - (lip / 2)
	local cuts_radius = inner_diameter / 2.5
	local rim = torus(diameter, lip)
	local plate = ellipsoid(inner_diameter, inner_diameter, .2)
	local edge_cut_limits = cylinder(inner_diameter - lip - lip_blend, .2)
	local edge_cut_stamp = ellipsoid(.4, .2, .2):move_y(cuts_radius)
	local edge_cuts =
		spin_steps(rotate_z, 6., edge_cut_stamp)
		:inter(edge_cut_limits)
	local recess = cylinder(.7, .2)
	local stud_length = .07
	local stud_diameter = 0.06
	local stud = cylinder(stud_diameter, stud_length):move_z(stud_length / 2.)
	local nut_width = .1
	local nut_depth = nut_width / 2.
	local nut_box = box(nut_width * 2, nut_width, nut_depth)
	local lug_nut = spin_steps(rotate_z, 3.0, nut_box, inter):union(stud)
	local lug_nutz = spin_steps(rotate_z, 5.0, lug_nut:move_x(.2))

	return plate
		:blend_diff(
			recess:move_z(.1),
			.05)
		:diff(
			edge_cuts)
		:blend_union(
			rim,
			lip_blend)
		:union(
			lug_nutz)
		:rotate_y(90)
		:move_x(outset)
end


function wheel (angle)
	angle = angle or 0
	return
		tire()
		:union(
			hub())
		:instance()
		--:rotate_z(angle)
end


-- Wheel model instances
wheel_a = wheel(-90):move_x(1)
wheel_b = wheel(-10)


set_advance_event(function (dt, elapsed)
	wheel_a
	:reset_transform()
	:rotate_x(elapsed * 1.5)
	:rotate_z(-90)
	:move_x(1)

	wheel_b
	:reset_transform()
	:rotate_x(elapsed * 1.5)
	:rotate_z(-10)
	:move_x(-1.3)
end)
