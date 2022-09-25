
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

function segment(diameter, length)
	return union(
			sphere(diameter),
			sphere(diameter):move_z(length),
			cylinder(diameter, length):align(0, 0, -1))
end

function grow()
	local b = random(5.0, 50.0)
	local c = random(20.0, 50.0)
	local a = (b + c) * .5

	local d = random(1.5, 3.0)
	local e = random(1.5, 3.0)
	local r = .35

	return
		-- point
		cone(r * 1.9, r * 2.5)
		:align(0, 0, -1)
		:rotate_x(a)

		-- stalk top
		:move_z(d)
		:blend_union(
			segment(r, d), r * .5)
		:rotate_x(b)

		-- stalk bottom
		:move_z(e)
		:union(
			segment(r, e))
		:move_z(1)
		:rotate_x(c)
end

local stalks = {}
for i = 1, 10, 1 do
	table.insert(stalks,
		grow():rotate_z((360 / 10) * i))
end
stalks = union(table.unpack(stalks))

model = sphere(2)
	:inter(
		plane(0, 0, -1))
	:blend_union(stalks, .5)
