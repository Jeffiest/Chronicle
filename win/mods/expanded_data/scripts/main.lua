-- Expanded Data: shop changes after DarkCloud-Expanded (BSD-2-Clause, (c) Gundorada Workshop): the base shop edits and a daily
-- rotating item in seven shops. Prices are in data/prices.json. Item ids: ids.lua, weapon pools: pools.lua.
if not (dc.day and dc.shop_list and dc.set_shop_list) then
  dc.log("Expanded Data: this build has no shop API; daily shop items are off (prices still apply)")
  return
end
local I = require("ids")
local P = require("pools")

local gems = { I.garnet, I.amethyst, I.aquamarine, I.diamond, I.emerald, I.pearl, I.ruby, I.peridot, I.sapphire, I.opal, I.topaz, I.turquoise }
local useful = { I.amulet_antifreeze, I.amulet_anticurse, I.amulet_antigoo, I.amulet_antidote, I.staminadrink, I.mightyhealing }
local slayers = { I.dragonslayer, I.undeadbuster, I.seakiller, I.stonebreaker, I.plantbuster, I.beastbuster, I.skyhunter, I.metalbreaker, I.mimicbreaker, I.mageslayer }

-- shop number (this game's list index) -> five daily pools, one per day of a five-day cycle
local ROTATION = {
  [0]  = { { I.treasurechestkey, I.treasurechestkey, I.tramoil }, gems, { I.mimi, I.prickly }, useful, P.dbcSecondHalfWeapons },                 -- Gaffer
  [1]  = { { I.carrot, I.minon, I.battan }, useful, P.wiseowlSecondHalfWeapons, { I.treasurechestkey, I.treasurechestkey, I.sundew }, gems },  -- Wise Owl
  [8]  = { P.shipwreckSecondHalfWeapons, P.shipwreckSecondHalfWeapons, P.shipwreckSecondHalfWeapons, P.shipwreckSecondHalfWeapons, P.shipwreckSecondHalfWeapons }, -- Jack
  [12] = { gems, { I.carrot, I.minon, I.battan, I.mimi, I.prickly }, useful, slayers, { 181 } },                                               -- Joker
  [9]  = { { I.amulet_antifreeze, I.amulet_anticurse, I.amulet_antigoo, I.amulet_antidote, I.staminadrink }, P.sunmoonSecondHalfWeapons,
           { I.treasurechestkey, I.treasurechestkey, I.secretpathkey }, gems, { I.poisonousapple, I.carrot, I.minon, I.battan, I.evy, I.mimi, I.prickly } }, -- Brooke
  [10] = { { I.treasurechestkey, I.treasurechestkey, I.braverylaunch }, gems, { I.metalbreaker, I.mimicbreaker }, useful, P.moonseaFirstHalfWeapons }, -- Ledan
  [11] = { P.galleryWeapons, { I.treasurechestkey, I.treasurechestkey, I.flappingduster },
           { I.poisonousapple, I.carrot, I.potatocake, I.minon, I.battan, I.evy, I.mimi, I.prickly }, useful, { I.treasurechestkey, I.treasurechestkey, I.flappingduster } }, -- Fairy King
}

local base = {}      -- the shops' lists as the game starts them, with the base edits below
local last_day = nil
local inited = false

local function without(list, item)
  local out = {}
  for _, v in ipairs(list) do if v ~= item then out[#out + 1] = v end end
  return out
end

local function init()
  for n = 0, 17 do base[n] = dc.shop_list(n) end
  -- Gaffer's shop: no Endurance attachment, Fire..Holy, Dinoslayer and Gold Bullion kept (the daily item takes the freed slot)
  base[0] = without(base[0], I.endurance)
  -- Fairy King's attachment shop also sells Metal Breaker and Mimic Breaker
  base[17][#base[17] + 1] = I.metalbreaker
  base[17][#base[17] + 1] = I.mimicbreaker
end

local function apply_day(day)
  local cycle = day % 5 + 1
  math.randomseed(day * 7919 + 17) -- the same day always gives the same pick, so loading a save shows the same item
  for n = 0, 17 do
    local list = {}
    for _, v in ipairs(base[n]) do list[#list + 1] = v end
    local rot = ROTATION[n]
    if rot then
      local pool = rot[cycle]
      if pool and #pool > 0 and #list < 20 then list[#list + 1] = pool[math.random(#pool)] end
    end
    dc.set_shop_list(n, list)
  end
  last_day = day
  local g = dc.shop_list(0)
  dc.log("day " .. day .. ": Gaffer's shop now sells " .. #g .. " items, daily item " .. tostring(g[#g]))
end

dc.on("tick", function(t)
  if not inited then init(); inited = true end
  local day = dc.day()
  if day ~= last_day then apply_day(day) end
end)

dc.on("game_loaded", function() last_day = nil end)

dc.log("Expanded Data: base shop edits and a daily item in seven shops")
