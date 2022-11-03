
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


set_bg("#88A")

local cursor_color = "#F0F"
local terrain = box(10, 1, 10):move_x(.5):paint("#BBB")
local terrain_model = nil

function mouse_event (event)
	if event.cursor then
		terrain = terrain
			:diff(sphere(.5):move(table.unpack(event.cursor)))
			:union(sphere(.4):paint(cursor_color):move(table.unpack(event.cursor)))
		refresh_canvas()
	end
end

function refresh_canvas ()
	if terrain_model then
		terrain_model:on_mouse_down(nil)
		terrain_model:hide()
	end
	terrain_model = terrain:instance()
	terrain_model:on_mouse_down(mouse_event)
end

refresh_canvas()

pallet_pots = {}
function pallet (color, offset)
	local model = sphere(1):paint(color):instance()
		:move(-5.2, 0.0, 4.5 - offset)
		:on_mouse_down(function (event)
			if event.picked then
				cursor_color = color
			end
		end)
	table.insert(pallet_pots, model)
end

pallet("#444", 0)
pallet("#888", 1)
pallet("#FFF", 2)
pallet("#F00", 3)
pallet("#0F0", 4)
pallet("#00F", 5)
pallet("#FF0", 6)
pallet("#0FF", 7)
pallet("#F0F", 8)
pallet("tangerine", 9)
