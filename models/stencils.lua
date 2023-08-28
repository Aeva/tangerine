
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


-- This line is a hack to avoid artifacts with the current meshing system.
push_meshing_density(32)


set_bg("#8ad1f6")
local material_a = pbrbr_material("tangerine")
local material_b = normal_debug_material()


function wedge_maker (wedge_count)
	local wedge = sphere(4)
		:inter(plane(0, 1, 0):move_y(-.1):rotate_z(360 / wedge_count / 2))
		:inter(plane(0, -1, 0):move_y(.1):rotate_z(-360 / wedge_count / 2))
	local wedges = wedge
	for i = 2, wedge_count, 1 do
		wedges = wedges:rotate_z(360 / wedge_count):union(wedge)
	end
	return wedges
end
local wedges = wedge_maker(8)


stencil_example = sphere(4.36)
	:paint(material_a)
	:stencil(wedges, material_b)
	:diff(sphere(6):move_z(2.25))
	:diff(plane(1, 1, 0):move(-1, -1, 0))
	:rotate_x(40)
	:instance()
	:rotate_z(30)
	:move_z(2.5)


mask_example = sphere(4.36)
	:paint(material_a)
	:mask(wedges, material_b)
	:diff(sphere(6):move_z(2.25))
	:diff(plane(-1, 1, 0):move(1, -1, 0))
	:rotate_x(40)
	:instance()
	:rotate_z(-30)
	:move_z(-2)
