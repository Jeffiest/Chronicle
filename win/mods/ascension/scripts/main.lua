-- Ascension: a modern-RPG layer for Dark Cloud. Character levels and XP, a skill tree with passives and active skills, crits and
-- damage variance, enemies that scale with the dungeon and floor, champions (mini-bosses), and a small HUD. All tunables are in config.lua.
-- Progress is kept in the per-slot store, so it is saved and loaded with your game.
local cfg = require("config")
local S = require("skills")
local UI = require("listui")
local LOOT = require("loot") -- mini-boss loot, ported from DarkCloud-Expanded (BSD-2-Clause)

local NAMES = { [0] = "Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond" }
local info = {}   -- per enemy slot: { champion, base_hp, exp, loot, boost }
local pending_roll = nil -- a weapon was just picked up: roll its stats when it shows up in a bag
local just_boosted = nil -- item id of a champion weapon boost applied this tick (it does not also get a random roll)
local pending_boost = nil -- a champion dropped a boosted weapon: watch the bags for it
local pops = {}   -- short-lived callouts ("CRIT!"): { id, text, x, y, rgba, born, scale }
local pop_seq = 0
local seeded = false

local function name(c) return NAMES[c] or ("#" .. tostring(c)) end
local function floor0(x) return math.floor(x + 0.5) end

-- progression ------------------------------------------------------------------------------------------------------------
local function plus_tier() return (dc.shared_get and dc.shared_get("dungeon_plus", "current_tier")) or 0 end -- Dungeon+ tier of this floor
local function level(c) return dc.store_get("lv" .. c) or 1 end
local function xp(c) return dc.store_get("xp" .. c) or 0 end
local function paragon(c) return dc.store_get("pg" .. c) or 0 end
local function paragon_need(p) return 1500 + 150 * p end -- after max level, XP keeps building Paragon ranks: +1.5% damage and +4 max HP each

local function add_xp(c, amount)
  if amount <= 0 or c < 0 or c > 5 then return end
  local lv, x = level(c), xp(c) + amount
  local leveled = false
  while lv < cfg.max_level and x >= cfg.xp_to_next(lv) do
    x = x - cfg.xp_to_next(lv)
    lv = lv + 1
    leveled = true
    dc.set_max_hp(dc.max_hp(c) + cfg.hp_per_level, c) -- the game saves max HP with the slot, so this stays in step with the store
  end
  local pg, pg_up = paragon(c), false
  if lv >= cfg.max_level then
    while pg < 50 and x >= paragon_need(pg) do
      x = x - paragon_need(pg)
      pg = pg + 1
      pg_up = true
      dc.set_max_hp(dc.max_hp(c) + 4, c)
    end
    if pg >= 50 then x = 0 end
    dc.store_set("pg" .. c, pg)
  end
  dc.store_set("lv" .. c, lv)
  dc.store_set("xp" .. c, x)
  if pg_up then
    dc.set_hp(dc.max_hp(c), c)
    dc.toast("PARAGON " .. pg .. "!  " .. name(c) .. " grows stronger (+1.5% damage, +4 max HP each rank)", 4)
  end
  if leveled then
    dc.set_hp(dc.max_hp(c), c)
    dc.toast("LEVEL UP!  " .. name(c) .. " is now level " .. lv .. "   (" .. S.points(c, lv) .. " skill points)", 4)
  end
end

local function level_damage(c) return 1 + cfg.damage_per_level * (level(c) - 1) + 0.015 * paragon(c) end
local function level_mitigation(c) return cfg.mitigation_per_level * (level(c) - 1) end

-- callouts ---------------------------------------------------------------------------------------------------------------
local function pop(i, text, rgba, scale)
  local x, y = dc.monster_screen(i, 20)
  if not x then return end
  pop_seq = pop_seq + 1
  pops[#pops + 1] = { id = "pop" .. (pop_seq % 16), text = text, x = x - #text * 6 * (scale or 3) / 2, y = y, rgba = rgba, born = dc.ticks(), scale = scale or 3 }
  if #pops > 12 then
    local old = table.remove(pops, 1)
    dc.text(old.id, "")
  end
end

local function update_pops(now)
  local keep = {}
  for _, p in ipairs(pops) do
    local age = now - p.born
    if age > 50 then
      dc.text(p.id, "")
    else
      local alpha = age < 30 and 255 or floor0(255 * (50 - age) / 20)
      dc.text(p.id, p.text, p.x, p.y - age * 0.9, p.scale, (p.rgba & 0xFFFFFF00) | alpha, false)
      keep[#keep + 1] = p
    end
  end
  pops = keep
end

-- combat -----------------------------------------------------------------------------------------------------------------
local function hp_fraction(c) local m = dc.max_hp(c); return m > 0 and dc.hp(c) / m or 1 end

dc.on("monster_hit", function(i, kind, attacker, dmg, element)
  if dmg <= 0 then return dmg end
  local c = (attacker and attacker >= 0) and attacker or dc.chara()
  local a = S.agg(c)
  local bonus = a.damage
  if S.berserk_active(dc.ticks()) then bonus = bonus + 0.40 end
  if hp_fraction(c) < 0.25 then bonus = bonus + a.last_stand end
  local mhp, mmax = dc.monster_hp(i), dc.monster_max_hp(i)
  if mmax and mmax > 0 and mhp / mmax < 0.30 then bonus = bonus + a.execute end
  local d = dmg * level_damage(c) * (1 + bonus) * (1 + (math.random() * 2 - 1) * cfg.variance)
  local crit = false
  if attacker and attacker >= 0 then
    local w = dc.weapon(c)
    local chance = cfg.crit_chance + a.crit + cfg.crit_per_speed * ((w and w.speed) or 0)
    if math.random() < chance then
      crit = true
      d = d * (cfg.crit_multiplier + a.crit_damage)
    end
  end
  d = math.max(1, floor0(d))
  if crit then pop(i, "CRIT!", 0xFFD030FF, 3) end
  if a.lifesteal > 0 and attacker and attacker >= 0 then
    dc.set_hp(math.min(dc.max_hp(c), dc.hp(c) + math.max(1, floor0(d * a.lifesteal))), c)
  end
  return d
end)

-- the damage event does not say who hit you, so a champion within reach of the player is taken to be the one hitting
local function champion_near()
  local px, _, pz = dc.player_pos()
  if not px then return false end
  local r2 = cfg.champion_threat_range ^ 2
  for i = 0, 15 do
    if info[i] and info[i].champion and dc.monster_alive(i) then
      local mx, _, mz = dc.monster_pos(i)
      if mx and (px - mx) ^ 2 + (pz - mz) ^ 2 <= r2 then return true end
    end
  end
  return false
end

dc.on("player_damage", function(c, amount)
  if amount <= 0 then return amount end
  local a = S.agg(c)
  local f = 1 + cfg.damage_taken_per_floor * math.max(0, dc.floor()) + cfg.damage_taken_per_dungeon * math.max(0, dc.dungeon())
  local mit = math.min(cfg.mitigation_cap, level_mitigation(c) + a.mitigation)
  if S.berserk_active(dc.ticks()) then f = f * 1.20 end
  if champion_near() then f = f * cfg.champion_melee end
  return math.max(1, floor0(amount * f * (1 - mit)))
end)

dc.on("monster_killed", function(i, kind, attacker)
  local m = info[i]
  if not m then return end
  local c = (attacker and attacker >= 0) and attacker or dc.chara()
  local a = S.agg(c)
  local amount = (m.base_hp * cfg.xp_per_max_hp + m.exp) * (m.champion and cfg.champion_xp or 1) * (1 + a.xp) * (1 + 0.25 * plus_tier())
  add_xp(c, floor0(amount))
  local gilda = dc.monster_money(i)
  if gilda and gilda > 0 then
    dc.set_gilda(dc.gilda() + floor0(gilda * (m.champion and cfg.champion_gilda or cfg.gilda_bonus) * (1 + a.gilda)))
  end
  dc.text("champ" .. i, "")
  if m.champion and m.boost and m.loot then
    pending_boost = { item = m.loot, boost = m.boost, count = nil, dungeon = dc.dungeon(), floor = dc.floor() }
  end
  info[i] = nil
end)

-- champions and their loot ------------------------------------------------------------------------------------------------
local function pick(list) return list[math.random(#list)] end

-- the order DarkCloud-Expanded rolls a champion's drop: the enemy's rare themed item, its common themed item, then the dungeon's
-- back-floor key, a weapon from the dungeon's pool, an attachment, or a consumable.
local function roll_loot(i, model)
  local d, f = dc.dungeon(), math.max(0, dc.floor())
  local D = LOOT.dungeons[d]
  if not D then return nil end
  local rare, common = D.rare[model], D.common[model]
  local item
  if rare and math.random(100) <= cfg.flavor_rare_chance then
    item = pick(rare)
  elseif common and math.random(100) <= cfg.flavor_common_chance then
    item = pick(common)
  end
  if item then return item, D.boosts[item] end
  if D.back_key and math.random(100) <= 35 then return D.back_key end
  if math.random(100) <= 15 then
    local pool = f <= D.split_floor and D.weapons_early or D.weapons_late
    if #pool > 0 then return pick(pool) end
  end
  if math.random(100) <= 80 then
    return pick(math.random(100) <= 60 and LOOT.attachmentsTableLucky or LOOT.attachmentsTableUnlucky)
  end
  return pick(math.random(100) <= 60 and LOOT.itemTableLucky or LOOT.itemTableUnlucky)
end

-- rolled weapon stats ----------------------------------------------------------------------------------------------------
local RARITY = {
  { name = "Fine",      weight = 25, lo = 0.05, hi = 0.10, color = 0x60FF60FF },
  { name = "Superior",  weight = 10, lo = 0.10, hi = 0.20, color = 0x60A0FFFF },
  { name = "Exquisite", weight = 4,  lo = 0.20, hi = 0.30, color = 0xC070FFFF },
  { name = "Legendary", weight = 1,  lo = 0.35, hi = 0.45, color = 0xFFB030FF },
}

local function roll_rarity()
  local r = math.random(100)
  local acc = 0
  for i = #RARITY, 1, -1 do -- rarest first
    acc = acc + RARITY[i].weight
    if r <= acc then return RARITY[i] end
  end
  return nil -- common: no change
end

-- weapons: count the copies of an item across every character's bag, and find the newest
local function weapon_count(item)
  local n = 0
  for c = 0, 5 do
    for s = 0, 10 do
      local w = dc.weapon(c, s)
      if w and w.item == item then n = n + 1 end
    end
  end
  return n
end

local function apply_boost(item, b)
  local best_c, best_s
  for c = 0, 5 do
    for s = 0, 10 do
      local w = dc.weapon(c, s)
      if w and w.item == item then best_c, best_s = c, s end
    end
  end
  if not best_c then return false end
  local w = dc.weapon(best_c, best_s)
  local t = {}
  if b.attack then t.attack = w.attack + b.attack end
  if b.endurance then t.endurance = w.endurance + b.endurance end
  if b.speed then t.speed = w.speed + b.speed end
  if b.magic then t.magic = w.magic + b.magic end
  if b.durability then t.durability = w.durability + b.durability end
  if b.elem then
    t.elem = {}
    for k = 1, 5 do t.elem[k] = math.min(127, (w.elem[k] or 0) + (b.elem[k] or 0)) end
  end
  if b.vs then
    t.vs_monster = {}
    for k = 1, 10 do t.vs_monster[k] = math.min(127, (w.vs_monster[k] or 0) + (b.vs[k - 1] or 0)) end
  end
  if b.s1 or b.s2 then t.flags = w.flags | (b.s1 or 0) | ((b.s2 or 0) << 8) end
  dc.set_weapon(t, best_c, best_s)
  dc.toast("The champion's weapon is stronger than usual!", 3)
  return true
end

local function roll_weapon(item)
  local rar = cfg.loot_rolls and roll_rarity()
  if not rar then return end
  local best_c, best_s
  for c = 0, 5 do
    for s = 0, 10 do
      local w = dc.weapon(c, s)
      if w and w.item == item then best_c, best_s = c, s end
    end
  end
  if not best_c then return end
  local w = dc.weapon(best_c, best_s)
  local f = 1 + rar.lo + math.random() * (rar.hi - rar.lo)
  dc.set_weapon({ attack = floor0(w.attack * f), endurance = floor0(w.endurance * f), speed = floor0(w.speed * f),
                  magic = floor0(w.magic * f), durability = floor0(w.durability * f) }, best_c, best_s)
  dc.toast(string.format("%s weapon!  +%d%% stats", rar.name, floor0((f - 1) * 100)), 4)
end

local function watch_roll()
  if not pending_roll then return end
  local p = pending_roll
  if dc.floor() ~= p.floor then pending_roll = nil; return end
  if weapon_count(p.item) > p.count then
    if just_boosted == p.item then just_boosted = nil
    elseif not (pending_boost and pending_boost.item == p.item) then roll_weapon(p.item) end
    pending_roll = nil
  elseif dc.ticks() - p.t > 600 then
    pending_roll = nil
  end
end

dc.on("item_pickup", function(item, qty)
  if item >= 257 and item <= 376 and cfg.loot_rolls then
    pending_roll = { item = item, count = weapon_count(item), floor = dc.floor(), t = dc.ticks() }
  end
  return item, qty
end)

local function watch_boost()
  if not pending_boost then return end
  local p = pending_boost
  if p.dungeon ~= dc.dungeon() or p.floor ~= dc.floor() then pending_boost = nil; return end -- like theirs: cancels on leaving the floor
  if not p.count then p.count = weapon_count(p.item) - 0; p.ready = true; return end
  if weapon_count(p.item) > p.count then
    apply_boost(p.item, p.boost)
    just_boosted = p.item
    pending_boost = nil
  end
end

-- enemy scaling ----------------------------------------------------------------------------------------------------------
local function scale_monster(i)
  local d, f = math.max(0, dc.dungeon()), math.max(0, dc.floor())
  local base_hp = dc.monster_max_hp(i)
  local key_holder = (dc.monster_drop(i) or -1) >= 0
  local champion = (not key_holder) and base_hp < cfg.champion_max_base_hp and math.random() < (cfg.champion_chance + 0.04 * plus_tier())
  local mult = (1 + cfg.hp_per_dungeon * d + cfg.hp_per_floor * f) * (champion and cfg.champion_hp or 1)
  local hp = math.max(1, floor0(base_hp * mult))
  dc.set_monster_max_hp(i, hp)
  dc.set_monster_hp(i, hp)
  local def = dc.monster_defense(i) or 0
  dc.set_monster_defense(i, floor0((def + cfg.defense_per_floor * f) * (champion and cfg.champion_defense or 1)))
  info[i] = { champion = champion, base_hp = base_hp, exp = dc.monster_exp(i) or 0 }
  if champion then
    local item, boost = roll_loot(i, dc.monster_model(i))
    if item then
      dc.set_monster_drop(i, item)
      info[i].loot, info[i].boost = item, boost
    end
    dc.set_monster_money(i, floor0((dc.monster_money(i) or 0) * cfg.champion_gilda))
  end
end

local function scan_monsters()
  for i = 0, 15 do
    if dc.monster_alive(i) then
      if not info[i] then
        local ok, err = pcall(scale_monster, i)
        if not ok then dc.log("scale_monster failed:", err); info[i] = { champion = false, base_hp = dc.monster_max_hp(i), exp = 0 } end
      end
      if info[i].champion then
        if dc.set_monster_scale then dc.set_monster_scale(i, cfg.champion_scale) end
        local x, y = dc.monster_screen(i, 26 * cfg.champion_scale)
        if x then dc.text("champ" .. i, "CHAMPION", x - 24, y, 2, 0xFF3030FF, false) else dc.text("champ" .. i, "") end
      end
    elseif info[i] then
      dc.text("champ" .. i, "")
      info[i] = nil
    end
  end
end

dc.on("floor_change", function()
  for i = 0, 15 do dc.text("champ" .. i, "") end
  info = {}
end)

dc.on("game_loaded", function() S.reload() end)

-- HUD --------------------------------------------------------------------------------------------------------------------
local function centered(text, scale) return 320 - #text * 6 * scale / 2 end -- x that centres text in the 640-wide frame

local function draw_hud(now, c, a)
  if dc.dungeon() < 0 then
    dc.rect("xp_frame", 0, 0, 0, 0, 0); dc.rect("xp_back", 0, 0, 0, 0, 0); dc.rect("xp_fill", 0, 0, 0, 0, 0)
    dc.text("xp_label", ""); dc.text("xp_points", "")
    S.hide_hud()
    return
  end
  local lv, x = level(c), xp(c)
  local need = cfg.xp_to_next(lv)
  local frac = lv >= cfg.max_level and math.min(1, x / paragon_need(paragon(c))) or math.min(1, x / need)
  dc.rect("xp_frame", 218, 460, 204, 10, 0x707070FF, 0)
  dc.rect("xp_back", 220, 462, 200, 6, 0x000000FF, 1)
  dc.rect("xp_fill", 220, 462, math.max(1, floor0(200 * frac)), 6, 0x40C8F0FF, 2)
  local pts = S.points(c, lv)
  local label = string.format("%s  Lv %d%s", name(c), lv, lv >= cfg.max_level and string.format("  MAX  Paragon %d   %d / %d", paragon(c), x, paragon_need(paragon(c))) or string.format("   %d / %d", x, need))
  dc.text("xp_label", label, centered(label, 1.5), 446, 1.5, 0xE0E0E0FF, false)
  if pts > 0 then
    local hint = pts .. " skill points   (K  or  L1+R1+R3)"
    dc.text("xp_points", hint, centered(hint, 1.2), 422, 1.2, 0xFFE070FF, false)
  else
    dc.text("xp_points", "")
  end
  S.draw_hud(now, c, a)
end

dc.on("tick", function(t)
  if not seeded then math.randomseed(t + 4242); seeded = true end
  local c = dc.chara()
  if c < 0 or c > 5 then c = 0 end -- a town reports no party member: it is Toan
  local lv = level(c)
  local a = S.agg(c)
  local owns_screen = S.tick(t, c, lv, a, level_damage(c))
  if owns_screen then return end
  update_pops(t)
  if dc.dungeon() >= 0 then
    scan_monsters()
    local ok, err = pcall(watch_boost)
    if not ok then dc.log("watch_boost failed:", err); pending_boost = nil end
    ok, err = pcall(watch_roll)
    if not ok then dc.log("watch_roll failed:", err); pending_roll = nil end
  end
  draw_hud(t, c, a)
end)

dc.log("Ascension loaded: levels, skill tree, crits, scaling enemies and champions")

-- Hub entries: the skill tree and a character level overview.
dc.on("tick", function() UI.tick() end)
local CHARA = { [0] = "Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond" }
dc.msg_on("hub_collect", function()
  dc.msg("hub_entry", "Skill Tree", "ascension.tree")
  dc.msg("hub_entry", "Character Levels", "ascension.levels")
end)
dc.msg_on("hub_open", function(id)
  if id == "ascension.tree" then
    S.request_open()
  elseif id == "ascension.levels" then
    local rows = {}
    for c = 0, 5 do
      local lv = level(c)
      rows[#rows + 1] = { text = string.format("%-8s Level %2d   XP %d   HP %d/%d", CHARA[c], lv, xp(c), dc.hp(c), dc.max_hp(c)),
        color = lv > 1 and 0xFFFFFFFF or 0x909090FF }
    end
    UI.open({ title = "CHARACTER LEVELS", rows = rows })
  end
end)
