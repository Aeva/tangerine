
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


model = cylinder(.5, 1)
	:rotate_x(90)
	:align(0, 1, 0)
	:paint("#666")
	:union(
		cylinder(9, .1)
		:rotate_x(90)
		:align(0, -1, 0)
		:paint("#EEE"))


for i = 1, 12, 1 do
	local pip = sphere(.5)
		:move_z(4)
		:rotate_y(360 / 12 * i)

	model = model:diff(pip)
end


-- Model instance for the second hand
second_hand = cylinder(.125, 5)
	:align(0, 1, -1)
	:move(0, -.75, -.5)
	:paint("#E66")
	:instance()


-- Model instance for the minute hand
minute_hand = cylinder(.2, 4)
	:align(0, 1, -1)
	:move(0, -.5, -.5)
	:paint("#666")
	:instance()


-- Model instance for the hour hand
hour_hand = cylinder(.3, 3)
	:align(0, 1, -1)
	:move(0, -.1, -.5)
	:paint("#333")
	:instance()


-- Model instance for the pendulum
pendulum = box(1, .25, 5)
	:align(0, -1, 1)
	:move_y(.125)
	:union(
		cylinder(2, .3)
		:rotate_x(90)
		:move(0, .25, -5.5))
	:paint("#222")
	:instance()


-- Advance the clock position by some number of seconds
function tick (seconds, phase)

	-- The clock hands accumulate rotation between frames
	local angle = (360 / 60) * seconds
	second_hand:rotate_y(angle)
	minute_hand:rotate_y(angle / 60)
	hour_hand:rotate_y(angle / 60 / 12)

	-- The pendulum is given a new rotation every frame
	pendulum:reset_transform()
	pendulum:rotate_y(15 * phase)
end


-- Set the clock to a random start position
tick(random(0, 60 * 60 * 12), 0)


-- Schedule an event to advance the clock every frame
set_advance_event(function (dt, elapsed)
	seconds = dt / 1000.0
	phase = math.sin(elapsed / 500.0)
	tick(seconds, phase)

end)
