-- Combat demo: damage multiplier, lifesteal, kill counter, HUD text, hotkey.
local mult = 2.0
local lifesteal = 0.10
local kills = 0
local last = "NONE"
local enabled = true
local ticks = 0
dc.log("combat_demo v2 loaded")

dc.on("monster_hit", function(idx, kind, attacker, damage, element)
  if not enabled then return end
  local dealt = math.floor(damage * mult)
  local heal = math.floor(dealt * lifesteal)
  if heal > 0 then
    dc.set_hp(math.min(dc.hp() + heal, dc.max_hp()))
  end
  dc.log("monster_hit idx="..idx.." kind="..kind.." dmg="..damage.." -> "..dealt)
  last = "HIT " .. dealt .. " (KIND " .. kind .. ")"
  return dealt
end)

dc.on("monster_killed", function(idx, kind, attacker)
  kills = kills + 1
  dc.log("monster_killed idx="..idx.." kind="..kind)
  dc.toast("KILL " .. kills, 2)
end)

dc.on("player_damage", function(chara, amount)
  if not enabled then return end
  return math.floor(amount * 0.75)   -- take 25% less
end)

dc.on("tick", function()
  ticks = ticks + 1
  if ticks % 250 == 1 then dc.log("tick "..ticks) end
  if dc.key_pressed("f6") then
    enabled = not enabled
    dc.toast(enabled and "COMBAT DEMO ON" or "COMBAT DEMO OFF", 2)
  end
  dc.text("hud", "TICK " .. ticks .. "  KILLS " .. kills .. "  " .. last, 8, 440, 2, 0xFFFF00FF)
end)
