
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

set_bg("#b4e2ff")

petal =
	inter(
		sphere(20):move_z(9.6),
		sphere(20):move_z(-9.6))
	:blend_inter(
		inter(
			sphere(8):move(2, -.5, 0),
			sphere(8):move(-2, -.5, 0)),
		.5)
	:move_y(5)
	:rotate_x(10)
	:paint("#fccbff")

model = sphere(1.8):move_z(.5)

step = 45
for angle = 0, 360 - step, step do
	model = union(model, petal:rotate_z(angle))
end


pearl_a = sphere(1):move(0, 1.5, .75)
pearl_b = sphere(.7):move(0, 2.2, .7)

step = 45
for angle = 0, 360 - step, step do
	model =
		union(
			model,
			pearl_a:rotate_z(angle),
			pearl_b:rotate_z(angle + step * .5))
end
