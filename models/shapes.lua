
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

local wedge =
	sphere(2)
	:diff(
		union(
			plane(1, .5, 0),
			plane(-1, .5, 0),
			plane(0, .5, 1),
			plane(0, .5, -1)):move_y(-1))

model =
	union(
		sphere(2),
		ellipsoid(1.5, 2, 1):move_y(3),
		cube(2):move_x(-3),
		box(1.5, 2, 1):move(-3, 3, 0),
		torus(2, .5):move_x(3),
		cylinder(1, 2):move(3, 3, 0),
		wedge:move_y(-3),
		cone(2, 2):move(-3, -3, 0))
