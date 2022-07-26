
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


function dot (lhs, rhs)
	local sum = 0
	for i = 1, math.min(#lhs, #rhs), 1 do
		sum = sum + (rhs[i] * lhs[i])
	end
	return sum
end


function make_quat (axis, degrees)
	local a = math.rad(degrees) * .5
	local s = math.sin(a)
	local c = math.cos(a)
	return {axis[1] * s, axis[2] * s, axis[3] * s, c}
end


function quat_product (lhs, rhs)
	local lx, ly, lz, lw = table.unpack(lhs)
	local rx, ry, rz, rw = table.unpack(rhs)

	return {
		dot({lw, lx, ly, lz}, {rx, rw, rz, -ry}),
		dot({lw, ly, lz, lx}, {ry, rw, rx, -rz}),
		dot({lw, lz, lx, ly}, {rz, rw, ry, -rx}),
		dot(lhs, {-rx, -ry, -rz, rw})}
end


function rotate_point (point, quat)
	local x, y, z, w = table.unpack(quat)
	local tmp = {
		dot(point, {w, -z, y}),
		dot(point, {z, w, -x}),
		dot(point, {-y, x, w}),
		dot(point, {-x, -y, -z})}

	return {
		dot(tmp, {w, -z, y, -x}),
		dot(tmp, {z, w, -x, -y}),
		dot(tmp, {-y, x, w, -z})}
end


function lerp (lhs, rhs, alpha)
	return lhs * (1.0 - alpha) + rhs * alpha
end


function shell_segment (alpha, z, quat)
	local diameter = lerp(2, .01, alpha)
	local radius = diameter * .5
	local thickness = lerp(.2, .01, alpha * alpha)
	local offset = rotate_point({radius, 0, z}, quat)

	function transform (node)
		return
			node
			:rotate_x(90)
			:quatate(table.unpack(quat))
			:move(table.unpack(offset))
	end

	local shape = transform(cylinder(diameter, thickness))
	local cut = nil

	if diameter - .1 > 0 then
		cut = transform(cylinder(diameter - .1, thickness + .1))
	end

	return shape, cut
end


function make_shell ()
	local rotation = make_quat({0, 0, 1}, 5)
	local iterations = 175

	local z = 0.0
	local quat = {0, 0, 0, 1}
	local shell = nil

	for i = 1, iterations, 1 do
		local alpha = i / iterations

		local shape, cut = shell_segment(alpha, z, quat)

		shell = shell and shell:union(shape) or shape
		if cut then
			shell = shell:diff(cut)
		end

		quat = quat_product(rotation, quat)
		z = z + lerp(0.0125, 0.00, alpha * alpha)
	end

	return shell
end


model =
	make_shell()
	:inter(
		plane(1, 0, 1.5)
		:move_z(1.5))
