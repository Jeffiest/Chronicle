-- Enemy Variety: per-enemy random speed, and a one-time get-back-up chance for any ordinary enemy.
if not dc.set_monster_speed then dc.log("Enemy Variety: this build lacks dc.set_monster_speed; skipped"); return end
local function setting(key, default)
  local v = dc.shared_get and dc.shared_get("enemy_variety", key)
  if v == nil or type(v) ~= type(default) then return default end
  return v
end

local info = {} -- per enemy slot: { speed, revived }

local function key_holder(i) return (dc.monster_drop(i) or -1) >= 0 end

local function setup(i)
  local sv = setting("speed_variance", 0.25)
  local base = setting("base_speed", 1.0)
  info[i] = {
    speed = math.max(0.4, base * (1 + (math.random() * 2 - 1) * sv)),
    revived = key_holder(i) or dc.monster_max_hp(i) >= setting("revive_max_hp", 3000),
  }
end

dc.on("tick", function()
  for i = 0, 15 do
    if dc.monster_alive(i) then
      if not info[i] then setup(i) end
      local m = info[i]
      dc.set_monster_speed(i, m.speed)
    elseif info[i] then
      dc.text("ev" .. i, "")
      dc.set_monster_speed(i, 1)
      info[i] = nil
    end
  end
end)

dc.on("floor_change", function() info = {} end)

dc.on("monster_hit", function(i, kind, attacker, damage)
  local m = info[i]
  if not m or m.revived or damage <= 0 then return damage end
  if damage < dc.monster_hp(i) then return damage end
  if math.random() >= setting("revive_chance", 0.25) then m.revived = true; return damage end -- one roll per enemy
  m.revived = true
  dc.set_monster_hp(i, math.max(1, math.floor(dc.monster_max_hp(i) * 0.3)))
  if dc.set_monster_status then dc.set_monster_status(i, "stop", 70) end
  dc.toast("An enemy gets back up!", 2)
  return 0
end)

dc.log("Enemy Variety loaded: random speed and a chance to get back up")
