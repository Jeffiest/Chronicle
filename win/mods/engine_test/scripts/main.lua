-- Exercises the API v3 additions: dc.rect (with z order), text without backdrop, dc.monster_screen / player_screen, dc.weapon,
-- dc.store_get / store_set and the game_loaded event. Disabled by default (see mod.json).
local logged_weapon, logged_player = false, false

dc.on("init", function()
  dc.log("api", dc.api_version, "store counter at init:", dc.store_get("counter"))
end)

dc.on("game_loaded", function()
  dc.log("game_loaded: slot", dc.store_slot(), "counter", dc.store_get("counter"))
end)

dc.on("tick", function(t)
  if t == 50 then dc.store_set("counter", (dc.store_get("counter") or 0) + 1) end

  -- bottom-left bar: grey frame (z 0), dark back, then the red fill on top (z 2): the fill must look bright red
  dc.rect("frame", 18, 438, 208, 18, 0x808080FF, 0)
  dc.rect("back", 20, 440, 204, 14, 0x000000FF, 1)
  dc.rect("fill", 22, 442, 160, 10, 0xE02020FF, 2)

  if dc.dungeon() >= 0 then
    if not logged_weapon then
      local w = dc.weapon()
      if w then
        dc.log("weapon: slot", w.slot, "item", w.item, "level", w.level, "attack", w.attack, "endurance", w.endurance,
               "speed", w.speed, "magic", w.magic, "durability", w.durability)
        logged_weapon = true
      end
    end
    local px, py, pscale = dc.player_screen(0)
    if px then
      dc.text("player", "YOU", px - 12, py, 2, 0x80FF80FF, false)
      if not logged_player then dc.log("player_screen", px, py, "px per unit", pscale); logged_player = true end
    end
    for i = 0, 15 do
      if dc.monster_alive(i) then
        local x, y, scale = dc.monster_screen(i, 0)
        if x then
          dc.text("m" .. i, "feet", x - 12, y, 2, 0xFFFFFFFF, false)
          local _, y10 = dc.monster_screen(i, 10)
          local _, y20 = dc.monster_screen(i, 20)
          local _, y30 = dc.monster_screen(i, 30)
          dc.text("m" .. i .. "a", "10", x - 6, y10, 2, 0xFFFF00FF, false)
          dc.text("m" .. i .. "b", "20", x - 6, y20, 2, 0x00FFFFFF, false)
          dc.text("m" .. i .. "c", "30", x - 6, y30, 2, 0xFF80FFFF, false)
          if t % 120 == 0 then dc.log("monster", i, "kind", dc.monster_kind(i), "screen", x, y, "px per unit", scale) end
        end
      end
    end
  end
end)
