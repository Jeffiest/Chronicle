-- Dungeon+: replay a dungeon's floors at a higher tier (1-5), floor by floor. A tiered floor has bigger rooms, tougher enemies and
-- richer gilda. On the floor-select screen put the cursor on a floor and press Square (tier up, after 5 it goes back to off) or
-- Triangle (tier down); a +n appears beside that floor only. Each floor keeps its own tier. Dungeons unlock for Dungeon+ once you have
-- reached their last floor (see UNLOCK_ALL below).
local MAX_TIER = 5
local NAMES = { [0] = "Divine Beast Cavern", "Wise Owl Forest", "Shipwreck", "Sun & Moon", "Moon Sea", "Gallery of Time", "Demon Shaft" }
local LAST_FLOOR = { [0] = 14, 16, 17, 17, 14, 24, 99 } -- index of each dungeon's last floor (the game's floor counts 15, 17, 18, 18, 15, 25, 100)

local function setting(key, default) -- the in-game settings screen stores values under this mod's name; fall back to the default
  local v = dc.shared_get and dc.shared_get("dungeon_plus", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end
local function unlock_all() return setting("unlock_all", true) end -- false: require reaching a dungeon's last floor first

if not (dc.set_floor_size and dc.floor_reached and dc.floor_select) then
  dc.log("Dungeon+: this build lacks the floor APIs; skipped")
  return
end

local tiers = nil          -- { ["dungeon:floor"] = tier }, from the store
local done = {}            -- enemy slots already scaled
local seen_unlock = {}
local pending = nil        -- { dungeon, floor }: the floor chosen on the floor-select screen, until it has been built
local shared_tier = -1     -- the tier last published to other mods (Ascension reads it)
local seen, cleared_floor, floor_start = 0, false, 0
local GEMS = { 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106 }
local SLAYERS = { 111, 112, 113, 114, 115, 116, 117, 118, 119, 120 }

local function get(key, default) local v = dc.store_get(key); if v == nil then return default end; return v end

local function cleared(d)
  if unlock_all() then return true end
  local r = dc.floor_reached(d)
  return r ~= nil and r >= LAST_FLOOR[d]
end

local function key(d, f) return d .. ":" .. f end
local function tier_of(d, f)
  if tiers == nil or d == nil or f == nil or d < 0 or f < 0 or not cleared(d) then return 0 end
  return tiers[key(d, f)] or 0
end
local function set_tier(d, f, t)
  tiers[key(d, f)] = (t > 0) and t or nil
  dc.store_set("ft", tiers)
end

-- floors are built before the game tells us which one it is, so size the NEXT floor ahead of time: the one chosen on the select screen,
-- or while you are in a dungeon the one below the current floor (taking the stairs down is the usual way on).
local function apply_sizes(fd, fsel)
  local d_now, f_now = dc.dungeon(), dc.floor()
  for d = 0, 6 do
    local f = nil
    if fd == d then f = fsel
    elseif pending and pending[1] == d then f = pending[2]
    elseif d == d_now and f_now ~= nil and f_now >= 0 then f = f_now + 1 end
    local t = tier_of(d, f)
    -- The builder fits only 5-6 rooms whatever is asked and the game already places 15 of its 16 enemy slots, so a floor grows through
    -- larger rooms, not through more rooms or more enemies.
    dc.set_floor_size(0, 1.0, math.min(2, (t + 1) // 2), d)
  end
end

local function scale(i, t)
  local hp = dc.monster_max_hp(i)
  if hp and hp > 0 then
    local n = math.max(1, math.floor(hp * (1 + setting("hp_per_tier", 0.35) * t) + 0.5))
    dc.set_monster_max_hp(i, n)
    dc.set_monster_hp(i, n)
  end
  dc.set_monster_defense(i, (dc.monster_defense(i) or 0) + setting("defense_per_tier", 2) * t)
  dc.set_monster_money(i, math.floor((dc.monster_money(i) or 0) * (1 + setting("gilda_per_tier", 0.4) * t) + 0.5))
end

dc.on("game_loaded", function() tiers = nil; done = {}; pending = nil end)
dc.on("floor_change", function() done = {}; pending = nil; seen, cleared_floor, floor_start = 0, false, dc.ticks() end)

dc.on("tick", function(t)
  if tiers == nil then tiers = get("ft", {}) end

  for d = 0, 6 do -- unlock notices
    if cleared(d) and not seen_unlock[d] then
      seen_unlock[d] = true
      if not get("unlocked" .. d, false) then
        dc.store_set("unlocked" .. d, true)
        dc.toast("DUNGEON+ unlocked: " .. NAMES[d] .. "  (Square / Triangle on the floor list)", 5)
      end
    end
  end

  local fd, fsel, ftop, flist_y, fcount = dc.floor_select()
  if fd ~= nil then
    pending = { fd, fsel }
    -- Square / Triangle: the game pages the list with L1/R1/L2/R2/Left/Right and uses Cross/Circle, so these two are free
    local cur = tier_of(fd, fsel)
    local change = nil
    if cleared(fd) then
      if dc.key_pressed("square") or dc.key_pressed("]") then change = (cur >= MAX_TIER) and 0 or cur + 1 end
      if dc.key_pressed("triangle") or dc.key_pressed("[") then change = (cur <= 0) and MAX_TIER or cur - 1 end
    end
    if change ~= nil then
      set_tier(fd, fsel, change)
      dc.log("floor " .. fsel .. " of dungeon " .. fd .. " is now tier " .. change)
    end

    -- the floor list: a +n beside each visible floor that has a tier, and a hint line for the floor under the cursor
    for k = 0, 4 do
      local f = ftop + k
      local tf = (f < fcount) and tier_of(fd, f) or 0
      if tf > 0 then
        dc.text("dpf" .. k, "+" .. tf, 186, flist_y + 40 * f + 12, 2, 0xFF8040FF, false)
      else
        dc.text("dpf" .. k, "")
      end
    end
    local shown = tier_of(fd, fsel)
    local hint
    if not cleared(fd) then
      hint = "DUNGEON+ unlocks when you reach this dungeon's last floor"
    else
      hint = (shown > 0 and ("THIS FLOOR: DUNGEON+ TIER " .. shown) or "THIS FLOOR: NORMAL") .. "     SQUARE: up   TRIANGLE: down"
    end
    dc.text("dpsel", hint, 320 - #hint * 6 * 1.5 / 2, 52, 1.5, shown > 0 and 0xFF8040FF or 0xC0C0C0FF, false)
  else
    for k = 0, 4 do dc.text("dpf" .. k, "") end
    dc.text("dpsel", "")
  end

  apply_sizes(fd, fsel)

  -- enemies of the floor you are on
  local d, f = dc.dungeon(), dc.floor()
  local eff = (fd == nil) and tier_of(d, f) or 0
  if eff > 0 then
    for i = 0, 15 do
      if dc.monster_alive(i) then
        if not done[i] then scale(i, eff); done[i] = true end
      else
        done[i] = nil
      end
    end
    local label = "DUNGEON+  TIER " .. eff
    dc.text("dplus", label, 320 - #label * 6 * 1.5 / 2, 8, 1.5, 0xFF8040FF, false)

    -- clearing a tiered floor (every enemy defeated) pays a bonus: one gem per tier, and from tier 3 a slayer attachment too
    if not cleared_floor then
      local alive = 0
      for i = 0, 15 do if dc.monster_alive(i) then alive = alive + 1 end end
      if alive > seen then seen = alive end
      if seen > 0 and alive == 0 and t - floor_start > 180 then
        cleared_floor = true
        local given = 0
        for _ = 1, eff do dc.give_item(GEMS[math.random(#GEMS)], 1); given = given + 1 end
        if eff >= 3 then dc.give_item(SLAYERS[math.random(#SLAYERS)], 1); given = given + 1 end
        dc.toast("DUNGEON+ CLEAR BONUS: " .. given .. " item" .. (given == 1 and "" or "s"), 4)
      end
    end
  else
    dc.text("dplus", "")
  end

  -- tell the other mods which tier this floor is (only when it changes; the shared store is a file)
  local publish = (fd == nil) and eff or 0
  if publish ~= shared_tier and dc.shared_set then
    shared_tier = publish
    dc.shared_set("dungeon_plus", "current_tier", publish)
  end
end)

dc.log("Dungeon+ loaded: per-floor tiers 0-" .. MAX_TIER)
