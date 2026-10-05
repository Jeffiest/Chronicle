-- Floor Bounty: the first enemy kind seen on a floor becomes its bounty target; kill enough of them for a payout.
local function setting(key, default)
  local v = dc.shared_get and dc.shared_get("floor_bounty", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end
local NAMES = require("names")

local target, count, need, paid = nil, 0, 6, false
local function label(model) return NAMES[model] or ("kind " .. tostring(model)) end

local function show()
  if target == nil then dc.text("bounty", ""); return end
  local t = paid and "BOUNTY DONE" or string.format("BOUNTY: %s  %d / %d", label(target), count, need)
  dc.text("bounty", t, 320 - #t * 4.5, 28, 1.2, paid and 0x80FF80FF or 0xFFD060FF, false)
end

dc.on("floor_change", function() target, count, paid = nil, 0, false; show() end)

dc.on("tick", function()
  if target == nil and dc.dungeon() >= 0 then
    for i = 0, 15 do
      if dc.monster_alive(i) and (dc.monster_drop(i) or -1) < 0 then
        target, count, paid = dc.monster_model(i), 0, false
        need = 6
        dc.toast("Floor bounty: " .. label(target) .. " x" .. need, 3)
        break
      end
    end
  end
  show()
end)

dc.on("monster_killed", function(i)
  if target == nil or paid or dc.monster_model(i) ~= target then return end
  count = count + 1
  if count >= need then
    paid = true
    local pay = math.floor((150 + 60 * math.max(0, dc.floor())) * (1 + 0.5 * math.max(0, dc.dungeon())) * setting("reward_scale", 1.0))
    dc.set_gilda(dc.gilda() + pay)
    dc.toast("BOUNTY COMPLETE!  +" .. pay .. " gilda", 3)
  end
end)

dc.log("Floor Bounty loaded")
