-- Dungeon Feats: floor-clear rewards, kill streaks, a bestiary with per-species milestones, and trophies. All progress is kept in
-- the per-slot store. Enemy names come from DarkCloud-Expanded's data (BSD-2-Clause).
local NAMES = require("names")
local function setting(key, default) -- the in-game settings screen stores values under this mod's name; fall back to the default
  local v = dc.shared_get and dc.shared_get("dungeon_feats", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end
local UI = require("listui")

local STREAK_WINDOW = 480 -- ticks (8 seconds) between kills to keep a streak going
local MIN_FLOOR_TICKS = 180 -- a floor cannot count as cleared in its first 3 seconds

local streak, streak_last = 0, -10000
local seen, cleared, floor_start = 0, false, 0

local function get(key, default) local v = dc.store_get(key); if v == nil then return default end; return v end
local function reward(gilda) dc.set_gilda(dc.gilda() + math.floor(gilda * setting("reward_scale", 1.0) + 0.5)) end

-- trophies ---------------------------------------------------------------------------------------------------------------
local TROPHIES = {
  { id = "first_blood", text = "First Blood",       gilda = 100,   test = function(s) return s.kills >= 1 end },
  { id = "hunter",      text = "Hunter (100 kills)", gilda = 500,   test = function(s) return s.kills >= 100 end },
  { id = "slayer",      text = "Slayer (500 kills)", gilda = 2000,  test = function(s) return s.kills >= 500 end },
  { id = "exterminator", text = "Exterminator (2000 kills)", gilda = 10000, test = function(s) return s.kills >= 2000 end },
  { id = "streak10",    text = "On a Roll (10 streak)",  gilda = 500,  test = function(s) return s.best_streak >= 10 end },
  { id = "streak25",    text = "Unstoppable (25 streak)", gilda = 2500, test = function(s) return s.best_streak >= 25 end },
  { id = "clear5",      text = "Floor Sweeper (5 floors cleared)",  gilda = 500,  test = function(s) return s.floors >= 5 end },
  { id = "clear25",     text = "Dungeon Cleaner (25 floors cleared)", gilda = 3000, test = function(s) return s.floors >= 25 end },
  { id = "clear100",    text = "Purifier (100 floors cleared)",     gilda = 12000, test = function(s) return s.floors >= 100 end },
  { id = "species20",   text = "Collector (20 kinds of enemy)",   gilda = 1500, test = function(s) return s.species >= 20 end },
  { id = "species60",   text = "Completionist (60 kinds of enemy)", gilda = 8000, test = function(s) return s.species >= 60 end },
}

local function stats()
  local sp, n = get("sp", {}), 0
  for _ in pairs(sp) do n = n + 1 end
  return { kills = get("kills", 0), best_streak = get("best_streak", 0), floors = get("floors", 0), species = n }
end

local function check_trophies()
  local done = get("tr", {})
  local s = stats()
  for _, t in ipairs(TROPHIES) do
    if not done[t.id] and t.test(s) then
      done[t.id] = true
      dc.store_set("tr", done)
      reward(t.gilda)
      dc.toast("TROPHY: " .. t.text .. "   +" .. t.gilda .. " gilda", 4)
    end
  end
end

-- events -----------------------------------------------------------------------------------------------------------------
dc.on("monster_killed", function(i, kind, attacker)
  local now = dc.ticks()
  dc.store_set("kills", get("kills", 0) + 1)

  -- kill streak
  streak = (now - streak_last <= STREAK_WINDOW) and (streak + 1) or 1
  streak_last = now
  if streak > get("best_streak", 0) then dc.store_set("best_streak", streak) end
  if streak == 5 or streak == 10 or streak % 20 == 0 then
    local bonus = streak * 5 * (1 + math.max(0, dc.dungeon()))
    reward(bonus)
    dc.toast("KILL STREAK x" .. streak .. "!   +" .. bonus .. " gilda", 2.5)
  end

  -- bestiary
  local model = dc.monster_model(i)
  if model then
    local sp = get("sp", {})
    local key = tostring(model)
    sp[key] = (sp[key] or 0) + 1
    dc.store_set("sp", sp)
    local n = sp[key]
    if n == 1 then
      dc.toast("Bestiary: " .. (NAMES[model] or ("enemy #" .. model)) .. " discovered", 3)
    elseif n == 10 or n == 50 or n == 150 then
      local g = n * 2
      reward(g)
      dc.toast("Bestiary: " .. (NAMES[model] or ("enemy #" .. model)) .. " x" .. n .. "   +" .. g .. " gilda", 3)
    end
  end
  check_trophies()
end)

dc.on("floor_change", function(dungeon, floor)
  seen, cleared, floor_start = 0, false, dc.ticks()
  streak = 0
end)

dc.on("tick", function(t)
  UI.tick()
  if dc.dungeon() < 0 then
    dc.text("streak", "")
    return
  end
  -- floor clear
  if not cleared then
    local alive = 0
    for i = 0, 15 do if dc.monster_alive(i) then alive = alive + 1 end end
    if alive > seen then seen = alive end
    if seen > 0 and alive == 0 and t - floor_start > MIN_FLOOR_TICKS then
      cleared = true
      dc.store_set("floors", get("floors", 0) + 1)
      local g = (50 + 25 * math.max(0, dc.floor())) * (1 + math.max(0, dc.dungeon()))
      reward(g)
      dc.toast("FLOOR CLEARED!   +" .. g .. " gilda", 3.5)
      check_trophies()
    end
  end
  -- streak HUD
  if streak >= 3 and t - streak_last <= STREAK_WINDOW then
    local label = "STREAK x" .. streak
    dc.text("streak", label, 320 - #label * 6 * 2 / 2, 52, 2, 0xFFC040FF, false)
  else
    dc.text("streak", "")
    if t - streak_last > STREAK_WINDOW then streak = 0 end
  end
end)

dc.log("Dungeon Feats loaded: floor clears, streaks, bestiary, trophies")

-- Hub screens ------------------------------------------------------------------------------------------------------------
local MILESTONES = { 10, 50, 150 }

local function bestiary_rows()
  local sp = get("sp", {})
  local ids, found, total = {}, 0, 0
  for id in pairs(NAMES) do ids[#ids + 1] = id; total = total + 1 end
  table.sort(ids)
  local rows = {}
  for _, id in ipairs(ids) do
    local kills = sp[tostring(id)]
    if kills then
      found = found + 1
      local nxt = "complete"
      for _, m in ipairs(MILESTONES) do
        if kills < m then nxt = "next reward at " .. m; break end
      end
      rows[#rows + 1] = { text = string.format("%-24s %5d kills   %s", NAMES[id], kills, nxt), color = 0xFFFFFFFF }
    else
      rows[#rows + 1] = { text = string.format("%-24s not seen yet", "???"), color = 0x707070FF }
    end
  end
  table.insert(rows, 1, { text = string.format("Discovered %d of %d kinds of enemy", found, total), color = 0xFFD060FF })
  return rows
end

local function trophy_rows()
  local done, rows, n = get("tr", {}), {}, 0
  local s = stats()
  for _, t in ipairs(TROPHIES) do
    local ok = done[t.id]
    if ok then n = n + 1 end
    rows[#rows + 1] = { text = string.format("%s %-38s %6d gilda", ok and "[X]" or "[ ]", t.text, t.gilda), color = ok and 0xFFD060FF or 0xB0B0B0FF }
  end
  table.insert(rows, 1, { text = string.format("%d of %d trophies   kills %d   best streak %d   floors cleared %d", n, #TROPHIES, s.kills, s.best_streak, s.floors), color = 0x80C0FFFF })
  return rows
end

dc.msg_on("hub_collect", function()
  dc.msg("hub_entry", "Bestiary", "feats.bestiary")
  dc.msg("hub_entry", "Trophies", "feats.trophies")
end)
dc.msg_on("hub_open", function(id)
  if id == "feats.bestiary" then
    UI.open({ title = "BESTIARY", rows = bestiary_rows() })
  elseif id == "feats.trophies" then
    UI.open({ title = "TROPHIES", rows = trophy_rows() })
  end
end)
