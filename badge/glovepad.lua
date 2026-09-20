--[==[badge-app
slug=glovepad
name=GlovePad
icon=GP
api=2
heap_kb=48
wake_lock=1
]==]

-- GlovePad: turns the Hacker Badge into an extra Minecraft controller.
-- Plug the badge into the laptop with USB, open this app, then run
-- glovepad_bridge.py on the laptop. The bridge reads these log lines
-- and presses the keys.
--
-- D-pad = walk (hold W / A / S / D)
-- A = jump (hold Space)   B = inventory (E)
-- START = pause menu (Esc)   AUX1 = sprint (hold Ctrl)
-- Shake = switch camera view (F5)      HOME = exit

local names = {}
local status, count_label
local presses = 0

-- LEDs that light up for each button (Lua indices 1-6)
local led_groups = {}
local led_colors = {
  UP = {0, 160, 255}, DOWN = {255, 140, 0}, LEFT = {140, 0, 255}, RIGHT = {140, 0, 255},
  A = {0, 255, 80}, B = {255, 40, 40}, START = {255, 255, 255}, AUX1 = {255, 220, 0},
}

local function send(name, kind)
  badge.sys.log("GGB:" .. name .. ":" .. kind)
end

local function light(name)
  local c = led_colors[name] or {255, 255, 255}
  local group = led_groups[name] or {1, 2, 3, 4, 5, 6}
  badge.led.clear()
  for _, i in ipairs(group) do
    badge.led.set(i, c[1], c[2], c[3])
  end
  badge.led.show()
end

local function lights_off()
  badge.led.clear()
  badge.led.show()
end

function on_enter(root)
  local B = badge.input.BUTTON
  names[B.UP] = "UP"
  names[B.DOWN] = "DOWN"
  names[B.LEFT] = "LEFT"
  names[B.RIGHT] = "RIGHT"
  names[B.A] = "A"
  names[B.B] = "B"
  names[B.START] = "START"
  names[B.AUX1] = "AUX1"

  led_groups = {
    UP = {1, 2}, DOWN = {5, 4}, LEFT = {1, 6, 5}, RIGHT = {2, 3, 4},
    A = {3, 6}, B = {3, 6}, START = {1, 2, 3, 4, 5, 6}, AUX1 = {1, 2, 3, 4, 5, 6},
  }

  local title = badge.ui.label(root, "GlovePad")
  title:style({text_font = 24})
  title:align("top_mid", 0, 10)

  local help = badge.ui.label(root,
    "D-pad = walk (W A S D)\n" ..
    "A jump   B inventory\n" ..
    "AUX1 sprint   START menu\n" ..
    "shake = camera")
  help:style({text_font = 16})
  help:align("center", 0, -4)

  status = badge.ui.label(root, "Ready - plug into laptop")
  status:style({text_font = 18})
  status:set_color(0x44ff66)
  status:align("bottom_mid", 0, -30)

  count_label = badge.ui.label(root, "0 presses")
  count_label:style({text_font = 14})
  count_label:align("bottom_mid", 0, -10)

  send("HELLO", "P")
  lights_off()
end

function on_tick()
  if badge.sensor.shake() then
    send("SHAKE", "P")
    status:set_text("Shake - camera")
    light("START")
  end
end

function on_button(button, kind)
  local name = names[button]
  if not name then return end
  if kind == badge.input.KIND.PRESSED then
    send(name, "P")
    presses = presses + 1
    count_label:set_text(presses .. " presses")
    status:set_text(name)
    light(name)
  elseif kind == badge.input.KIND.RELEASED then
    send(name, "R")
    lights_off()
  end
end

function on_exit()
  send("BYE", "P")
  lights_off()
end
