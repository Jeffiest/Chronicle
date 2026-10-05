-- Ascension skill tree: passives, three active skills, the Focus resource and the tree screen. main.lua wires it into combat.
-- Open the tree with  L1+R1+Select  (controller) or  K  (keyboard). Move with the d-pad/arrows, Cross buys a rank,
-- Square respecs (costs gilda), Circle closes. Active skills: hold L2 and press Square / Triangle / Circle.
local cfg = require("config")
local S = {}
local HAVE_V4 = (dc.api_version or 0) >= 4 -- freeze, block_input, player_pos, hurt_monster; without them the tree and active skills are off

-- the tree ---------------------------------------------------------------------------------------------------------------
-- id, branch, name, max rank, cost per rank, requires {id, rank}, description. `kind` is what main.lua reads from S.agg().
S.branches = { "MIGHT", "VITALITY", "FOCUS" }
S.nodes = {
  { id = "m1", b = 1, name = "Keen Edge",     max = 5, cost = 1, desc = "+4% damage per rank." },
  { id = "m2", b = 1, name = "Deadly Aim",    max = 5, cost = 1, req = { "m1", 1 }, desc = "+3% crit chance per rank." },
  { id = "m3", b = 1, name = "Ruthless",      max = 3, cost = 1, req = { "m2", 2 }, desc = "+20% crit damage per rank." },
  { id = "m4", b = 1, name = "Executioner",   max = 1, cost = 1, req = { "m1", 3 }, desc = "+30% damage to enemies under 30% HP." },
  { id = "m5", b = 1, name = "Siphon",        max = 3, cost = 1, req = { "m3", 1 }, desc = "Heal 2% of damage dealt per rank." },
  { id = "a1", b = 1, name = "SHOCKWAVE",     max = 1, cost = 2, req = { "m1", 2 }, desc = "ACTIVE  L2+Square: blast all nearby enemies. 30 Focus." },
  { id = "v1", b = 2, name = "Toughness",     max = 5, cost = 1, desc = "+4% max HP per rank." },
  { id = "v2", b = 2, name = "Iron Skin",     max = 5, cost = 1, req = { "v1", 1 }, desc = "-2% damage taken per rank." },
  { id = "v3", b = 2, name = "SECOND WIND",   max = 1, cost = 2, req = { "v1", 2 }, desc = "ACTIVE  L2+Triangle: heal 30% max HP. 40 Focus." },
  { id = "v4", b = 2, name = "Regeneration",  max = 3, cost = 1, req = { "v2", 2 }, desc = "Recover 0.4% max HP per second per rank." },
  { id = "v5", b = 2, name = "Last Stand",    max = 1, cost = 1, req = { "v2", 3 }, desc = "+25% damage while under 25% HP." },
  { id = "f1", b = 3, name = "Deep Focus",    max = 5, cost = 1, desc = "+10 max Focus per rank." },
  { id = "f2", b = 3, name = "Quick Recovery", max = 5, cost = 1, req = { "f1", 1 }, desc = "+8% Focus regeneration per rank." },
  { id = "f3", b = 3, name = "BERSERK",       max = 1, cost = 2, req = { "f1", 2 }, desc = "ACTIVE  L2+Circle: 10s of +40% damage, +20% damage taken. 25 Focus." },
  { id = "f4", b = 3, name = "Treasure Hunter", max = 3, cost = 1, req = { "f2", 1 }, desc = "+10% gilda from kills per rank." },
  { id = "f5", b = 3, name = "Scholar",       max = 3, cost = 1, req = { "f2", 2 }, desc = "+5% XP from kills per rank." },
  { id = "f6", b = 3, name = "Cooldown Mastery", max = 3, cost = 1, req = { "f3", 1 }, desc = "-8% skill cooldowns per rank." },
}
local by_id = {}
for _, n in ipairs(S.nodes) do by_id[n.id] = n end
local columns = { {}, {}, {} }
for _, n in ipairs(S.nodes) do columns[n.b][#columns[n.b] + 1] = n end

-- per-character state (kept in the store) ---------------------------------------------------------------------------------
local ranks_cache = {}
local function ranks(c)
  if not ranks_cache[c] then ranks_cache[c] = dc.store_get("sk" .. c) or {} end
  return ranks_cache[c]
end
local function save_ranks(c) dc.store_set("sk" .. c, ranks_cache[c]) end
function S.reload() ranks_cache = {} end -- after a game is loaded

function S.rank(c, id) return ranks(c)[id] or 0 end
local function spent(c)
  local total = 0
  for id, r in pairs(ranks(c)) do total = total + r * (by_id[id] and by_id[id].cost or 0) end
  return total
end
function S.points_total(level)
  return (level - 1) * cfg.points_per_level + math.floor((level - 1) / 5) * cfg.bonus_points_every_5
end
function S.points(c, level) return S.points_total(level) - spent(c) end

local function req_ok(c, n)
  if not n.req then return true end
  return S.rank(c, n.req[1]) >= n.req[2]
end

-- aggregated passive values for a character ------------------------------------------------------------------------------
function S.agg(c)
  local r = function(id) return S.rank(c, id) end
  return {
    damage = 0.04 * r("m1"),
    crit = 0.03 * r("m2"),
    crit_damage = 0.20 * r("m3"),
    execute = 0.30 * r("m4"),
    lifesteal = 0.02 * r("m5"),
    mitigation = 0.02 * r("v2"),
    regen = 0.004 * r("v4"),
    last_stand = 0.25 * r("v5"),
    focus_max = 10 * r("f1"),
    focus_regen = 0.08 * r("f2"),
    gilda = 0.10 * r("f4"),
    xp = 0.05 * r("f5"),
    cooldown = 0.08 * r("f6"),
  }
end

-- buying and respeccing ---------------------------------------------------------------------------------------------------
function S.buy(c, level, id)
  local n = by_id[id]
  if not n then return false, "no such skill" end
  if S.rank(c, id) >= n.max then return false, "already at max rank" end
  if not req_ok(c, n) then return false, "needs " .. by_id[n.req[1]].name .. " rank " .. n.req[2] end
  if S.points(c, level) < n.cost then return false, "not enough skill points" end
  ranks(c)[id] = S.rank(c, id) + 1
  save_ranks(c)
  if id == "v1" then -- Toughness changes max HP for real; the game saves it with the slot, so track what we added
    local add = math.max(1, math.floor(dc.max_hp(c) * 0.04 + 0.5))
    dc.set_max_hp(dc.max_hp(c) + add, c)
    dc.store_set("hpb" .. c, (dc.store_get("hpb" .. c) or 0) + add)
  end
  return true
end

function S.respec(c)
  local bonus = dc.store_get("hpb" .. c) or 0
  if bonus > 0 then
    dc.set_max_hp(math.max(1, dc.max_hp(c) - bonus), c)
    dc.store_set("hpb" .. c, 0)
  end
  ranks_cache[c] = {}
  save_ranks(c)
end

-- Focus and the active skills ---------------------------------------------------------------------------------------------
local focus = 100
local cool = { square = 0, triangle = 0, circle = 0 }
local berserk_until = 0
function S.berserk_active(now) return now < berserk_until end
function S.focus() return focus end

local SKILLS = {
  square   = { id = "a1", name = "Shockwave",   cost = 30, cd = 360 },
  triangle = { id = "v3", name = "Second Wind", cost = 40, cd = 1200 },
  circle   = { id = "f3", name = "Berserk",     cost = 25, cd = 1500 },
}

local function dist2(ax, az, bx, bz) return (ax - bx) ^ 2 + (az - bz) ^ 2 end

local function cast(button, c, level, now, a, dmg_mult)
  local sk = SKILLS[button]
  if S.rank(c, sk.id) < 1 then return end
  if cool[button] > 0 then return end
  if focus < sk.cost then dc.toast("Not enough Focus", 1); return end
  focus = focus - sk.cost
  cool[button] = math.floor(sk.cd * (1 - a.cooldown))
  if button == "square" then
    local px, _, pz = dc.player_pos()
    local w = dc.weapon(c)
    local base = ((w and w.attack or 10) * 1.5 + 20) * dmg_mult
    local hit = 0
    for i = 0, 15 do
      if dc.monster_alive(i) then
        local mx, _, mz = dc.monster_pos(i)
        if mx and dist2(px, pz, mx, mz) <= cfg.shockwave_radius ^ 2 then
          dc.hurt_monster(i, math.max(1, math.floor(base + 0.5)), c)
          hit = hit + 1
        end
      end
    end
    dc.toast("SHOCKWAVE!  (" .. hit .. " hit)", 1.5)
  elseif button == "triangle" then
    dc.set_hp(math.min(dc.max_hp(c), dc.hp(c) + math.floor(dc.max_hp(c) * 0.30)), c)
    dc.toast("SECOND WIND", 1.5)
  elseif button == "circle" then
    berserk_until = now + 600
    dc.toast("BERSERK!", 1.5)
  end
end

-- the tree screen --------------------------------------------------------------------------------------------------------
local open = false
local cur_b, cur_i = 1, 1
local message, message_until = "", 0
function S.is_open() return open end
local want_open = false
function S.request_open() want_open = true end -- asked for by the hub menu

local function clear_ui()
  for i = 1, 70 do dc.text("ui" .. i, "") end
  for i = 1, 40 do dc.rect("uir" .. i, 0, 0, 0, 0, 0) end
end

local function draw_tree(c, level, now)
  local nrect, ntext = 0, 0
  local function R(x, y, w, h, rgba) nrect = nrect + 1; dc.rect("uir" .. nrect, x, y, w, h, rgba, 20) end
  local function T(s, x, y, scale, rgba) ntext = ntext + 1; dc.text("ui" .. ntext, s, x, y, scale, rgba, false) end
  R(0, 0, 640, 480, 0x000000D0)
  R(20, 16, 600, 448, 0x20283CFF)
  R(22, 18, 596, 444, 0x10141EFF)
  T("SKILL TREE  -  " .. ({ [0] = "Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond" })[c] .. "  Lv " .. level, 36, 26, 2, 0xFFE070FF)
  T("Points: " .. S.points(c, level), 470, 26, 2, 0x80E0FFFF)
  for b = 1, 3 do
    local x = 32 + (b - 1) * 196
    T(S.branches[b], x, 56, 2, 0xA0A0FFFF)
    R(x, 74, 184, 2, 0x606080FF)
    for i, n in ipairs(columns[b]) do
      local y = 84 + (i - 1) * 30
      local rk = S.rank(c, n.id)
      local avail = req_ok(c, n)
      local selected = (b == cur_b and i == cur_i)
      if selected then R(x - 3, y - 3, 190, 28, 0x405080FF) end
      local color = rk >= n.max and 0xFFD060FF or (rk > 0 and 0xFFFFFFFF or (avail and 0xB0B0B0FF or 0x606060FF))
      T(n.name, x, y, 1.5, color)
      T(rk .. "/" .. n.max .. "  (" .. n.cost .. ")", x, y + 12, 1.2, avail and 0x80C0FFFF or 0x505050FF)
    end
  end
  local n = columns[cur_b][cur_i]
  R(30, 392, 580, 52, 0x181C28FF)
  if n then
    T(n.name, 38, 398, 2, 0xFFFFFFFF)
    T(n.desc, 38, 418, 1.5, 0xD0D0D0FF)
    if n.req then T("Requires: " .. by_id[n.req[1]].name .. " rank " .. n.req[2], 38, 432, 1.2, 0xFF9090FF) end
  end
  if now < message_until then T(message, 36, 448, 1.5, 0xFFB040FF) end
  T("Cross: buy   Square: respec (" .. cfg.respec_gilda_per_level * level .. " gilda)   Circle: close", 36, 468, 1.2, 0x909090FF)
end

-- one call per tick; returns true while the tree owns the screen and input
function S.tick(now, c, level, a, dmg_mult)
  if not HAVE_V4 then return false end
  -- Focus
  local fmax = 100 + a.focus_max
  focus = math.min(fmax, focus + (cfg.focus_regen_per_second * (1 + a.focus_regen)) / 60)
  if focus > fmax then focus = fmax end
  for k, v in pairs(cool) do if v > 0 then cool[k] = v - 1 end end
  if a.regen > 0 and now % 60 == 0 and dc.hp(c) > 0 then
    dc.set_hp(math.min(dc.max_hp(c), dc.hp(c) + math.max(1, math.floor(dc.max_hp(c) * a.regen))), c)
  end

  local in_dungeon = dc.dungeon() >= 0
  if open then
    -- navigation
    if dc.key_pressed("down") then cur_i = math.min(#columns[cur_b], cur_i + 1) end
    if dc.key_pressed("up") then cur_i = math.max(1, cur_i - 1) end
    if dc.key_pressed("right") then cur_b = math.min(3, cur_b + 1); cur_i = math.min(cur_i, #columns[cur_b]) end
    if dc.key_pressed("left") then cur_b = math.max(1, cur_b - 1); cur_i = math.min(cur_i, #columns[cur_b]) end
    if dc.key_pressed("cross") then
      local ok, why = S.buy(c, level, columns[cur_b][cur_i].id)
      message, message_until = ok and "Learned!" or why, now + 120
    end
    if dc.key_pressed("square") then
      local cost = cfg.respec_gilda_per_level * level
      if dc.gilda() >= cost then
        dc.set_gilda(dc.gilda() - cost)
        S.respec(c)
        message, message_until = "Skills reset", now + 120
      else
        message, message_until = "Respec costs " .. cost .. " gilda", now + 120
      end
    end
    if dc.key_pressed("circle") or not in_dungeon then
      open = false
      dc.freeze(false)
      clear_ui()
    else
      draw_tree(c, level, now)
      return true
    end
  elseif want_open then
    want_open = false
    if not in_dungeon then
      dc.toast("The skill tree opens inside a dungeon", 2)
    elseif dc.freeze(true) then
      open = true
      cur_b, cur_i = 1, 1
      return true
    end
  end

  -- active skills: hold L2, press a face button. While L2 is held the game does not see the face buttons.
  if in_dungeon and not open then
    local l2 = dc.key_down("l2")
    S.draw_overlay(l2, c, level)
    if l2 then
      dc.block_input({ "square", "triangle", "circle", "cross" })
      for _, button in ipairs({ "square", "triangle", "circle" }) do
        if dc.key_pressed(button) then cast(button, c, level, now, a, dmg_mult) end
      end
    else
      dc.block_input(false)
    end
  end
  return false
end

-- HUD for Focus and the skill slots ---------------------------------------------------------------------------------------
function S.draw_hud(now, c, a)
  if not HAVE_V4 then return end
  local fmax = 100 + a.focus_max
  dc.rect("fo_frame", 218, 472, 204, 8, 0x707070FF, 0)
  dc.rect("fo_back", 220, 474, 200, 4, 0x000000FF, 1)
  dc.rect("fo_fill", 220, 474, math.max(1, math.floor(200 * focus / fmax)), 4, 0xB060F0FF, 2)
  -- the learned skills sit in a row under the Focus bar, each centred in its own third of the frame
  local slots = { { "square", "SQ", 320 - 130 }, { "triangle", "TR", 320 }, { "circle", "CI", 320 + 130 } }
  for _, slot in ipairs(slots) do
    local button, tag, cx = slot[1], slot[2], slot[3]
    local sk = SKILLS[button]
    if S.rank(c, sk.id) >= 1 then
      local ready = cool[button] <= 0 and focus >= sk.cost
      local label = tag .. " " .. sk.name .. (cool[button] > 0 and (" " .. math.ceil(cool[button] / 60) .. "s") or "")
      dc.text("sk_" .. button, label, cx - #label * 6 * 1.2 / 2, 434, 1.2, ready and 0xFFFFFFFF or 0x808080FF, false)
    else
      dc.text("sk_" .. button, "")
    end
  end
  if S.berserk_active(now) then dc.text("sk_buff", "BERSERK", 320 - 7 * 6 * 1.5 / 2, 410, 1.5, 0xFF6040FF, false) else dc.text("sk_buff", "") end
end

-- shown while L2 is held: which button casts what, its cost and cooldown
function S.draw_overlay(show, c, level)
  local lines = {}
  if show then
    for _, button in ipairs({ "square", "triangle", "circle" }) do
      local sk = SKILLS[button]
      local tag = ({ square = "SQUARE  ", triangle = "TRIANGLE", circle = "CIRCLE  " })[button]
      if S.rank(c, sk.id) >= 1 then
        local ready = cool[button] <= 0 and focus >= sk.cost
        lines[#lines + 1] = { string.format("%s  %-12s %3d Focus%s", tag, sk.name, sk.cost, cool[button] > 0 and ("   cooling " .. math.ceil(cool[button] / 60) .. "s") or ""), ready and 0xFFFFFFFF or 0x808080FF }
      else
        lines[#lines + 1] = { tag .. "  (not learned)", 0x606060FF }
      end
    end
  end
  if #lines == 0 then
    dc.rect("ov_back", 0, 0, 0, 0, 0)
    for i = 1, 4 do dc.text("ov" .. i, "") end
    return
  end
  dc.rect("ov_back", 160, 330, 320, 62, 0x000000B0, 15)
  dc.text("ov0", "")
  for i, l in ipairs(lines) do dc.text("ov" .. i, l[1], 170, 336 + (i - 1) * 16, 1.5, l[2], false) end
end

function S.hide_hud()
  S.draw_overlay(false)
  for _, id in ipairs({ "sk_square", "sk_triangle", "sk_circle", "sk_buff" }) do dc.text(id, "") end
  dc.rect("fo_frame", 0, 0, 0, 0, 0); dc.rect("fo_back", 0, 0, 0, 0, 0); dc.rect("fo_fill", 0, 0, 0, 0, 0)
end

return S
