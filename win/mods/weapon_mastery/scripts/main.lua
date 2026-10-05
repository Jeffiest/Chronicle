-- Weapon Mastery: every weapon type you use earns mastery with kills. Each mastery level adds 2% damage with that weapon (up to +10%).
-- Mastery belongs to the weapon type (for example every Baselard), so it carries over when you upgrade or find another copy.
local function setting(key, default) -- the in-game settings screen stores values under this mod's name; fall back to the default
  local v = dc.shared_get and dc.shared_get("weapon_mastery", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end
local IDS = require("ids")
local UI = require("listui")
local NAMES = {}
for name, id in pairs(IDS) do
  if id >= 257 and id <= 376 then NAMES[id] = (name:gsub("^%l", string.upper)) end
end

local THRESHOLDS = { 25, 75, 150, 300, 600 } -- kills for mastery levels 1-5

local function get_table() return dc.store_get("wm") or {} end

local function level_of(kills)
  local lv = 0
  for i, need in ipairs(THRESHOLDS) do if kills >= need then lv = i end end
  return lv
end

dc.on("monster_killed", function(i, kind, attacker)
  if not attacker or attacker < 0 then return end
  local w = dc.weapon(attacker)
  if not w then return end
  local t = get_table()
  local key = tostring(w.item)
  local before = t[key] or 0
  t[key] = before + 1
  dc.store_set("wm", t)
  local lv0, lv1 = level_of(before), level_of(before + 1)
  if lv1 > lv0 then
    dc.toast("MASTERY " .. lv1 .. ": " .. (NAMES[w.item] or ("weapon " .. w.item)) .. "  (+" .. math.floor(lv1 * setting("per_level", 0.02) * 100 + 0.5) .. "% damage)", 4)
  end
end)

dc.on("monster_hit", function(i, kind, attacker, dmg, element)
  if dmg <= 0 or not attacker or attacker < 0 then return dmg end
  local w = dc.weapon(attacker)
  if not w then return dmg end
  local lv = level_of(get_table()[tostring(w.item)] or 0)
  if lv > 0 then return math.max(1, math.floor(dmg * (1 + setting("per_level", 0.02) * lv) + 0.5)) end
  return dmg
end)

dc.log("Weapon Mastery loaded")

dc.on("tick", function() UI.tick() end)

local function mastery_rows()
  local t, list = get_table(), {}
  for key, kills in pairs(t) do list[#list + 1] = { item = tonumber(key), kills = kills } end
  table.sort(list, function(a, b) return a.kills > b.kills end)
  local rows = {}
  for _, e in ipairs(list) do
    local lv = level_of(e.kills)
    local nxt = THRESHOLDS[lv + 1]
    rows[#rows + 1] = {
      text = string.format("%-22s Lv %d/%d   %5d kills   +%d%%   %s", NAMES[e.item] or ("weapon " .. e.item), lv, #THRESHOLDS, e.kills,
        math.floor(lv * PER_LEVEL * 100 + 0.5), nxt and ("next at " .. nxt) or "MAX"),
      color = lv >= #THRESHOLDS and 0xFFD060FF or (lv > 0 and 0xFFFFFFFF or 0xB0B0B0FF),
    }
  end
  if #rows == 0 then rows[1] = { text = "No weapon has earned mastery yet. Defeat enemies to start.", color = 0x909090FF } end
  return rows
end

dc.msg_on("hub_collect", function() dc.msg("hub_entry", "Weapon Mastery", "mastery.list") end)
dc.msg_on("hub_open", function(id)
  if id == "mastery.list" then UI.open({ title = "WEAPON MASTERY", rows = mastery_rows() }) end
end)
