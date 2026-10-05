-- Expanded Abilities: weapon effects after DarkCloud-Expanded (BSD-2-Clause), rebuilt on this port's combat and status hooks.
-- Only effects that can be done from damage, status timers and weapon data are here; ones that need new visuals, clones or
-- charge mechanics are not.
local UNDEAD = { [0] = true, [1] = true, [23] = true, [24] = true, [36] = true, [44] = true, [45] = true, [67] = true,
                 [81] = true, [82] = true, [96] = true, [105] = true, [122] = true, [144] = true } -- monster table indexes
local AGAS_SWORD, HEAVENS_CLOUD, BRAVE_ARK, CROSS_HINDER, FROZEN_TUNA, MOBIUS_RING, CACTUS, SNAIL = 281, 271, 274, 294, 321, 341, 360, 374
local CURABLE = 0x04 | 0x10 | 0x20 | 0x40 -- freeze, poison, curse, goo
local ICE = 1
local HAVE_STATUS = dc.set_monster_status ~= nil and dc.ailments ~= nil -- API added after v4; without it the status abilities are skipped

local streak = { start = 0, last = 0 }

dc.on("monster_hit", function(i, kind, attacker, dmg, element)
  if dmg <= 0 or not attacker or attacker < 0 then return dmg end
  local w = dc.weapon(attacker)
  if not w then return dmg end
  local now = dc.ticks()
  local item = w.item
  if item == CROSS_HINDER then
    -- Sanctifier: about twice the damage against the undead
    local model = dc.monster_model(i)
    if model and UNDEAD[model] then dmg = dmg * 2 end
  elseif item == MOBIUS_RING then
    -- The longer it goes, the harder it hits: +2.5% per second of an unbroken fight, up to +60%; resets after 6s without a hit
    if now - streak.last > 360 then streak.start = now end
    streak.last = now
    dmg = dmg * (1 + math.min(0.60, 0.025 * (now - streak.start) / 60))
  elseif not HAVE_STATUS and (item == BRAVE_ARK or item == HEAVENS_CLOUD or item == SNAIL or item == FROZEN_TUNA) then
    -- needs the status API
  elseif item == BRAVE_ARK then
    -- sealed against every affliction: each hit cures freeze, poison, curse and goo
    local a = dc.ailments(attacker)
    if a & CURABLE ~= 0 then dc.set_ailments(a & ~CURABLE, attacker) end
  elseif item == HEAVENS_CLOUD then
    if math.random() < 0.50 then dc.set_monster_status(i, "slow", 300) end   -- gooey proc
  elseif item == SNAIL then
    if math.random() < 0.05 then dc.set_monster_status(i, "slow", 300) end   -- goo proc
  elseif item == FROZEN_TUNA then
    if element ~= ICE and math.random() < 0.05 then dc.set_monster_status(i, "stop", 180) end
  elseif item == CACTUS then
    -- one drop of water (10 units) absorbed per 100 damage
    dc.set_water(dc.water(attacker) + dmg / 10, attacker)
  end
  return math.floor(dmg + 0.5)
end)

dc.on("player_damage", function(c, amount)
  if amount <= 0 then return amount end
  local w = dc.weapon(c)
  if w and w.item == AGAS_SWORD then return math.max(1, amount - 15) end -- the Hero Aga's will guards you: +15 defense
  return amount
end)

dc.log("Expanded Abilities loaded")
