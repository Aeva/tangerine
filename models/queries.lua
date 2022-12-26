
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


model = nil


function dist(x1, y1, x2, y2)
	return math.sqrt((x2 - x1) ^ 2 + (y2 - y1) ^ 2)
end


local locus_x = random(-3, 3)
local locus_y = random(-3, 3)
local maxima = dist(
	math.abs(locus_x),
	math.abs(locus_y),
	-5, -5)


for y = -5, 5, 1 do
	for x = -5, 5, 1 do
		local z = 1.0 - (dist(locus_x, locus_y, x, y) / maxima)
		z = z * z * 3.0
		local tile =
			cube(1.2)
			:rotate_x(random(0, 360))
			:rotate_y(random(0, 360))
			:rotate_z(random(0, 360))
			:move(x, y, z)
		model = model and model:union(tile) or tile
	end
end



local hit = model:magnet(
	0, 0, 10,
	locus_x, locus_y, 3.5)


if hit then
	local d = random(1.5, 3.5)
	local shell = model:gradient(hit) * vec3(d * .5)
	hit = hit + shell

	model = model:union(sphere(d):move(hit):paint("#F00"))

	for i = 1, 500, 1 do
		local hit = model:ray_cast(
			random(-3.0, 3.0),
			random(-3.0, 3.0),
			10,
			random(-.5, .5),
			random(-.5, .5),
			-1)

		if hit then
			local r = .5
			local pebble = sphere(r):move(hit):paint("#0FF")
			local cut = sphere(r * 1.2):move(hit)
			model = model:diff(cut):union(pebble)
		end
	end
end
