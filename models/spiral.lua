
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


local ball_joint = sphere(.25):paint("#afff00")
local segment = box(.3, .19, .19):paint("#ff00e4")

function spiral(mag, tail)
	tail = tail or ball_joint
	return
		tail:move_x(.25)
			:union(segment:move_x(.125))
			:rotate_x(10)
			:rotate_y(mag)
			:union(ball_joint)
end

function descend(fn, steps, mag)
	mag = mag or 0
	if steps > .1 then
		return fn(mag, descend(fn, steps -1, mag + 1))
	else
		return fn(mag)
	end
end

function spin(steps, tree)
	local step = 360 / steps
	local thing = nil
	for i = 1, steps, 1 do
		local stamp = tree:rotate_z(i * step)
		thing = thing and thing:union(stamp) or stamp
	end
	return thing
end

model = spin(11, descend(spiral, 70))
