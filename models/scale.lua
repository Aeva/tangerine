
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

require("basic_thing")

local half_thing = diff(model, plane(0, 0, 1))

local scale = 1
for i = 1, 5, 1 do
	scale = scale * .75
	local offset = -2.4 * scale
	local threshold = .25 * scale
	model = blend_union(
		model:move_z(offset),
		half_thing:scale(scale),
		threshold)
end

model = model:align(0, 0, -1)
