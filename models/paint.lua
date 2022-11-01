
set_bg("#88A")

local cursor_color = "#F0F"
local terrain = box(10, 1, 10):move_x(.5):paint("#BBB")
local terrain_model = nil

function refresh_canvas()
	if terrain_model then
		terrain_model:on_mouse_down(nil)
		terrain_model:hide()
	end
	terrain_model = terrain:instance()
	terrain_model:on_mouse_down(function (button, clicks, mouse_over, ...)
		if mouse_over then
			local hit = {...}
			terrain = terrain
				:diff(sphere(.5):move(table.unpack(hit)))
				:union(sphere(.4):paint(cursor_color):move(table.unpack(hit)))
			refresh_canvas()
		end
	end)
end

refresh_canvas()

pallet_pots = {}
function pallet(color, offset)
	local model = sphere(1):paint(color):instance()
		:move(-5.2, 0.0, 4.5 - offset)
		:on_mouse_down(function (button, clicks, mouse_over)
			cursor_color = color
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
