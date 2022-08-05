
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


function clip(model)
	return model:inter(cube(10))
end


function append(model, thing)
	return model and model:union(thing) or thing
end


seascape = (function ()
	local sea = box(10, 10, 50):align(0, 0, 1)

	local land = blend_inter(
		cylinder(20, 10)
		:move(6, 7, 0)
		:rotate_x(-10)
		:rotate_y(10),
		sphere(100)
		:move(20, 20, -42),
		1)

	local fill = land:move_z(-.1):paint("#5f4d2e")
	local crust = land:diff(fill):paint("#3da447")
	land = union(crust, fill)

	local depths_dist = -.05
	local depths = sea:move_z(depths_dist)
	local depths_cut = plane(0, 0, 1):move_z(depths_dist)
	local water = union(
		sea
		:diff(land, depths_cut)
		:paint("#4974fc"),
		depths
		:diff(land)
		:paint("#0c32ab"))

	return clip(union(water, land))
end)()


function tree (x, y, z, size)
	local tuft =
		sphere(size)
		:align(0, 0, -random())
		:move(x, y, z)
		:paint("green")
	if size < 0.1 then
		return tuft
	else
		return tuft:blend_union(tree(x, y, z + size / 2, size / 2), size / 3)
	end
end


function house (x, y, z, side, height)
	return
		box(side, side, height)
		:rotate_z(random(44.0, 46.0))
		:align(0, 0, -1)
		:move(x, y, z)
end


cityscape = (function ()
	local town = nil
	for y = -5.125, 5.125, .25 do
		for x = -5.125, 5.125, .25 do
			local hit = seascape:ray_cast(x, y, 50, 0, 0, -1)
			if hit then
				local z = hit[3]
				if z > 0.1 then
					local side = random(0.0, 0.25)
					if side > 0.125 then
						local height = random(side, side * 2.1)
						town = append(town, house(x, y, z, side, height))
					else
						local size = random(0.15, 0.4)
						town = append(town, tree(x, y, z, size))
					end
				end
			end
		end
	end
	return clip(town)
end)()


model = seascape:union(cityscape)
set_bg("#8ad1f6")
