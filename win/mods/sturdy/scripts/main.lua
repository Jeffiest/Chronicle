-- Sturdy: a gentler survival side of Dark Cloud. Weapons lose durability more slowly and thirst drains more slowly (each point lost has
-- a chance of being given straight back). Tune the two refunds below.
local function setting(key, default) -- the in-game settings screen stores values under this mod's name; fall back to the default
  local v = dc.shared_get and dc.shared_get("sturdy", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end

local last_key, last_dur, last_water = nil, nil, nil

dc.on("tick", function(t)
  if dc.dungeon() < 0 then last_key = nil; return end
  local c = dc.chara()

  -- weapon durability
  local w = dc.weapon(c)
  if w then
    local key = c .. ":" .. w.slot .. ":" .. w.item
    if key == last_key and last_dur and w.durability < last_dur and last_dur - w.durability <= 3 then
      local lost = last_dur - w.durability
      local back = 0
      for _ = 1, lost do if math.random() < setting("durability_refund", 0.5) then back = back + 1 end end
      if back > 0 then
        dc.set_weapon({ durability = w.durability + back }, c)
        w.durability = w.durability + back
      end
    end
    last_key, last_dur = key, w.durability
  else
    last_key = nil
  end

  -- water
  local water = dc.water(c)
  if last_water and water < last_water and last_water - water < 5 then
    dc.set_water(water + (last_water - water) * setting("water_refund", 0.3), c)
    water = dc.water(c)
  end
  last_water = water
end)

dc.log("Sturdy loaded: slower weapon wear and thirst")
