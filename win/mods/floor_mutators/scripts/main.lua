-- Floor Mutators: each new dungeon floor has a chance of a modifier that changes how it plays. A banner names it when you arrive.
local function setting(key, default) -- the in-game settings screen stores values under this mod's name; fall back to the default
  local v = dc.shared_get and dc.shared_get("floor_mutators", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end

local MUTATORS = {
  { id = "glass",  name = "Glass Cannon", text = "You deal and take 50% more damage" },
  { id = "iron",   name = "Iron Hide",    text = "Enemies take 25% less damage, but pay double gilda" },
  { id = "bounty", name = "Bounty",       text = "Enemies carry 2.5 times the gilda" },
  { id = "spring", name = "Fountain",     text = "Water slowly refills" },
  { id = "thirst", name = "Scorched",     text = "Water drains faster" },
  { id = "lucky",  name = "Lucky Day",    text = "15% of your hits strike twice as hard" },
}

local current = nil
local banner_until = 0
local done = {} -- enemy slots whose gilda was already scaled

local function pick() return MUTATORS[math.random(#MUTATORS)] end

dc.on("floor_change", function(dungeon, floor)
  done = {}
  current = nil
  local chance = setting("chance", 0.35) + 0.03 * math.max(0, dungeon) -- chance a floor gets a mutator
  if dungeon >= 0 and floor > 0 and math.random() < chance then
    current = pick()
    banner_until = dc.ticks() + 360
    dc.toast("FLOOR MUTATOR: " .. current.name .. " - " .. current.text, 5)
  end
end)

dc.on("monster_hit", function(i, kind, attacker, dmg, element)
  if not current or dmg <= 0 then return dmg end
  local id = current.id
  if id == "glass" then dmg = dmg * 1.5
  elseif id == "iron" then dmg = dmg * 0.75
  elseif id == "lucky" and attacker and attacker >= 0 and math.random() < 0.15 then dmg = dmg * 2 end
  return math.max(1, math.floor(dmg + 0.5))
end)

dc.on("player_damage", function(c, amount)
  if current and current.id == "glass" and amount > 0 then return math.max(1, math.floor(amount * 1.5 + 0.5)) end
  return amount
end)

dc.on("tick", function(t)
  if dc.dungeon() < 0 or not current then
    dc.text("mutator", "")
    return
  end
  -- gilda scaling of newly seen enemies
  local mult = (current.id == "iron" and 2.0) or (current.id == "bounty" and 2.5) or nil
  if mult then
    for i = 0, 15 do
      if dc.monster_alive(i) then
        if not done[i] then
          dc.set_monster_money(i, math.floor((dc.monster_money(i) or 0) * mult + 0.5))
          done[i] = true
        end
      else
        done[i] = nil
      end
    end
  end
  -- water effects, once a second
  if t % 60 == 0 then
    local c = dc.chara()
    if current.id == "spring" then dc.set_water(dc.water(c) + 1.5, c)
    elseif current.id == "thirst" then dc.set_water(math.max(0, dc.water(c) - 1.0), c) end
  end
  local label = "MUTATOR: " .. current.name
  dc.text("mutator", label, 320 - #label * 6 * 1.5 / 2, t < banner_until and 24 or 8, 1.5, 0xC080FFFF, false)
end)

dc.log("Floor Mutators loaded")
