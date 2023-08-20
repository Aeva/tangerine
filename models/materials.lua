
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
