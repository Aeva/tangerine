
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


function gear(diameter, height, teeth)
	local radius = diameter / 2
	local circumference = 2 * math.pi * radius
	local slice = circumference / teeth
	local step = 360 / teeth
	local inner_diameter = diameter - slice * 3
	local gear = cylinder(diameter, height)
		:diff(cylinder(inner_diameter, height / 3):move_z(height / 2))
		:diff(cylinder(inner_diameter, height / 3):move_z(height / -2))

	local hole_cut = cylinder(diameter / 4, height + .1):move_x(radius / 2)
	for t = 1, 4, 1 do
		gear = gear:diff(hole_cut:rotate_z(t * 90))
	end

	local tooth_cut = box(slice, slice, height + .1):rotate_z(45):move_x(radius)
	for t = 1, teeth, 1 do
		gear = gear:diff(tooth_cut:rotate_z(t * step))
	end

	return gear
end

model = gear(8, .5, 100)
