-- Autosave: writes the town into file slot 0, the AUTOSAVE board at the top of the save list (dc.autosave). Manual saves go below it.
-- When: a few seconds after arriving in a town map, and every INTERVAL seconds in the open town. Never in a dungeon, in a building, in a
-- menu, while the game is held, or while visiting another player's world (that would write their town over yours).
local ARRIVE_SECONDS = 6
local INTERVAL_SECONDS = 240
local TPS = 60

local ticks = 0
local last_scene = ""
local arrived_at = 0
local last_save = -1e9
local pending = false

local function town_open()
  local sc = dc.scene()
  local geo, map, interior = sc:match("^T(%d+):([^:]*):(.*)$")
  return geo ~= nil and interior == "_", sc
end

dc.on("tick", function()
  ticks = ticks + 1
  local open, sc = town_open()
  if sc ~= last_scene then
    last_scene = sc
    arrived_at = ticks
    pending = open
  end
  if not open or dc.world_active() or dc.menu_open() or dc.frozen() or dc.input_blocked() then return end
  local due = (pending and ticks - arrived_at > ARRIVE_SECONDS * TPS) or (ticks - last_save > INTERVAL_SECONDS * TPS and ticks - arrived_at > ARRIVE_SECONDS * TPS)
  if due and dc.autosave() then
    pending = false
    last_save = ticks
    dc.toast("Autosaved", 1.5)
  end
end)

dc.log("Autosave loaded: slot 0 of the save list")
