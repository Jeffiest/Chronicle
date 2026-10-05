-- Mod Settings: an in-game screen for the tunables of the other mods. Open it with F1 or L2+R2+Select. Up/Down choose a row, Left/Right change
-- the value, Cross applies the highlighted preset, Circle (or the open key again) closes it. Changes take effect at once and are kept in
-- mods/_shared.json, not per save slot. A mod that has no value set here uses its own default.
if not (dc.shared_get and dc.shared_set and dc.block_input) then
  dc.log("Mod Settings: this build has no shared settings API; skipped")
  return
end

-- { mod folder, key, label, kind, min, max, step, default, help }
local S = {
  { "ascension", "champion_chance", "Ascension: champion chance", "num", 0, 0.30, 0.02, 0.08, "Chance an enemy is a champion (mini-boss)" },
  { "ascension", "champion_hp", "Ascension: champion HP x", "num", 1, 6, 0.5, 3, "Champion health compared with a normal enemy" },
  { "ascension", "hp_per_floor", "Ascension: enemy HP per floor", "num", 0, 0.10, 0.005, 0.035, "Extra enemy HP for each floor deeper" },
  { "ascension", "hp_per_dungeon", "Ascension: enemy HP per dungeon", "num", 0, 0.30, 0.02, 0.10, "Extra enemy HP for each dungeon" },
  { "ascension", "damage_taken_per_floor", "Ascension: damage taken per floor", "num", 0, 0.06, 0.005, 0.02, "Enemies hit harder by this much per floor" },
  { "ascension", "damage_taken_per_dungeon", "Ascension: damage taken per dungeon", "num", 0, 0.20, 0.01, 0.08, "...and per dungeon" },
  { "ascension", "damage_per_level", "Ascension: damage per level", "num", 0, 0.06, 0.005, 0.025, "Your damage bonus for each character level" },
  { "ascension", "crit_chance", "Ascension: crit chance", "num", 0, 0.30, 0.01, 0.08, "Base chance of a critical hit" },
  { "ascension", "loot_rolls", "Ascension: rolled weapon loot", "bool", 0, 1, 1, true, "Picked-up weapons may roll bonus stats" },
  { "floor_mutators", "chance", "Mutators: chance per floor", "num", 0, 1, 0.05, 0.35, "Chance a floor rolls a modifier" },
  { "sturdy", "durability_refund", "Sturdy: weapon wear refund", "num", 0, 1, 0.1, 0.5, "Chance each lost durability point is restored" },
  { "sturdy", "water_refund", "Sturdy: water loss refund", "num", 0, 1, 0.1, 0.3, "Share of thirst drain given back" },
  { "weapon_mastery", "per_level", "Mastery: damage per level", "num", 0, 0.05, 0.005, 0.02, "Damage bonus per mastery level" },
  { "dungeon_feats", "reward_scale", "Feats: gilda rewards x", "num", 0, 3, 0.25, 1, "Scales floor-clear, streak, bestiary and trophy gilda" },
  { "dungeon_plus", "unlock_all", "Dungeon+: unlocked everywhere", "bool", 0, 1, 1, true, "Off: a dungeon unlocks when you reach its last floor" },
  { "dungeon_plus", "hp_per_tier", "Dungeon+: enemy HP per tier", "num", 0, 1, 0.05, 0.35, "Extra enemy HP for each tier" },
  { "dungeon_plus", "gilda_per_tier", "Dungeon+: gilda per tier", "num", 0, 1, 0.1, 0.4, "Extra gilda from enemies for each tier" },
  { "floor_bounty", "reward_scale", "Bounty: payout x", "num", 0, 4, 0.25, 1.0, "Gilda paid for completing a floor bounty" },
  { "enemy_variety", "base_speed", "Variety: enemy speed x", "num", 0.6, 2, 0.05, 1.0, "How fast enemies move and animate" },
  { "enemy_variety", "speed_variance", "Variety: speed spread", "num", 0, 0.6, 0.05, 0.25, "Each enemy gets its own random speed within this spread" },
  { "enemy_variety", "revive_chance", "Variety: get-back-up chance", "num", 0, 1, 0.05, 0.25, "Chance an ordinary enemy survives a killing blow once" },
}

-- presets: values by mod and key; Normal puts every setting back to its default
local PRESETS = {
  { name = "Relaxed", values = {
      ascension = { champion_chance = 0.04, champion_hp = 2.5, hp_per_floor = 0.02, hp_per_dungeon = 0.06, damage_taken_per_floor = 0.01,
                    damage_taken_per_dungeon = 0.04, damage_per_level = 0.035, crit_chance = 0.12 },
      floor_mutators = { chance = 0.2 }, sturdy = { durability_refund = 0.8, water_refund = 0.6 }, dungeon_feats = { reward_scale = 1.5 },
      dungeon_plus = { hp_per_tier = 0.25, gilda_per_tier = 0.6 },
      enemy_variety = { base_speed = 0.95, speed_variance = 0.15, revive_chance = 0.1 } } },
  { name = "Normal", values = {} },
  { name = "Hardcore", values = {
      ascension = { champion_chance = 0.15, champion_hp = 4, hp_per_floor = 0.05, hp_per_dungeon = 0.16, damage_taken_per_floor = 0.035,
                    damage_taken_per_dungeon = 0.12, damage_per_level = 0.02, crit_chance = 0.05 },
      floor_mutators = { chance = 0.55 }, sturdy = { durability_refund = 0, water_refund = 0 }, dungeon_feats = { reward_scale = 0.75 },
      dungeon_plus = { hp_per_tier = 0.5, gilda_per_tier = 0.3 },
      enemy_variety = { base_speed = 1.15, speed_variance = 0.35, revive_chance = 0.4 } } },
}

local open, cur, offset, preset_i, message, message_until = false, 1, 0, 2, "", 0
local VISIBLE = 13

local function current(row)
  local v = dc.shared_get(row[1], row[2])
  if v == nil or type(v) ~= type(row[8]) then return row[8] end
  return v
end

local function fmt(row, v)
  if row[4] == "bool" then return v and "ON" or "OFF" end
  local s = string.format("%.3f", v)
  s = s:gsub("0+$", ""):gsub("%.$", "")
  return s
end

local function apply_preset(p)
  for _, row in ipairs(S) do
    local mod_values = p.values[row[1]]
    local v = mod_values and mod_values[row[2]]
    if v == nil then v = row[8] end
    dc.shared_set(row[1], row[2], v)
  end
  message, message_until = "Preset applied: " .. p.name, dc.ticks() + 150
end

local function change(row, dir)
  local v = current(row)
  if row[4] == "bool" then
    dc.shared_set(row[1], row[2], not v)
    return
  end
  local n = v + dir * row[7]
  n = math.max(row[5], math.min(row[6], n))
  n = math.floor(n / row[7] + 0.5) * row[7]
  n = tonumber(string.format("%.4f", n))
  dc.shared_set(row[1], row[2], n)
end

local function clear_ui()
  dc.rect("ms_back", 0, 0, 0, 0, 0)
  dc.rect("ms_panel", 0, 0, 0, 0, 0)
  dc.rect("ms_sel", 0, 0, 0, 0, 0)
  for i = 0, VISIBLE * 2 + 4 do dc.text("ms" .. i, "") end
end

local function draw()
  local rows = #S + 1
  dc.rect("ms_back", 0, 0, 640, 480, 0x000000C0, 30)
  dc.rect("ms_panel", 70, 40, 500, 400, 0x161C2CFF, 31)
  dc.text("ms0", "MOD SETTINGS", 84, 48, 2, 0xFFE070FF, false)
  dc.text("ms1", "Up/Down: row   Left/Right: change   Cross: apply preset   Circle: close", 84, 70, 1.2, 0x909090FF, false)
  local y0 = 92
  for k = 0, VISIBLE - 1 do
    local i = offset + k + 1
    if i > rows then
      dc.text("ms" .. (2 + k * 2), "")
      dc.text("ms" .. (3 + k * 2), "")
    else
      local y = y0 + k * 22
      local label, value
      if i == 1 then
        label, value = "PRESET", "<  " .. PRESETS[preset_i].name .. "  >"
      else
        local row = S[i - 1]
        label, value = row[3], fmt(row, current(row))
      end
      if i == cur then dc.rect("ms_sel", 78, y - 3, 484, 20, 0x405080FF, 32) end
      local color = (i == cur) and 0xFFFFFFFF or 0xC0C0C0FF
      dc.text("ms" .. (2 + k * 2), label, 88, y, 1.5, color, false)
      dc.text("ms" .. (3 + k * 2), value, 430, y, 1.5, (i == 1) and 0xFFB040FF or 0x80D0FFFF, false)
    end
  end
  local help = (cur == 1) and "Relaxed / Normal / Hardcore set many values at once" or S[cur - 1][9]
  dc.text("ms" .. (2 + VISIBLE * 2), help, 84, 414, 1.5, 0xA0C0FFFF, false)
  dc.text("ms" .. (3 + VISIBLE * 2), dc.ticks() < message_until and message or "", 84, 428, 1.5, 0xFFB040FF, false)
end

dc.on("tick", function(t)
  local want = dc.key_pressed("f1") or dc.key_pressed("l2+r2+select")
  if open then
    if want or dc.key_pressed("circle") then
      open = false
      clear_ui()
      dc.freeze(false)
      dc.block_input(false)
      return
    end
    local rows = #S + 1
    if dc.key_pressed("down") then cur = math.min(rows, cur + 1) end
    if dc.key_pressed("up") then cur = math.max(1, cur - 1) end
    if cur - 1 < offset then offset = cur - 1 end
    if cur > offset + VISIBLE then offset = cur - VISIBLE end
    local dir = 0
    if dc.key_pressed("right") then dir = 1 end
    if dc.key_pressed("left") then dir = -1 end
    if cur == 1 then
      if dir ~= 0 then preset_i = ((preset_i - 1 + dir) % #PRESETS) + 1 end
      if dc.key_pressed("cross") then apply_preset(PRESETS[preset_i]) end
    elseif dir ~= 0 then
      change(S[cur - 1], dir)
    end
    dc.block_input(true) -- keep the game from seeing the buttons (this mod loads last, after Ascension releases its own block)
    draw()
  elseif want then
    open = true
    cur, offset = 1, 0
    if not dc.freeze(true) then dc.block_input(true) end
  end
end)

dc.log("Mod Settings loaded: F1 or L2+R2+Select")
