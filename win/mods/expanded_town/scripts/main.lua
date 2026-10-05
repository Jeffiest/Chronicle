-- Expanded Town: a bird's-eye camera in towns, after DarkCloud-Expanded's town overhead camera (BSD-2-Clause).
-- Press L1+Select (or the O key) in a town to look straight down at the player; press it again to go back.
local HEIGHT, BACK = 90, 22 -- how high above the player and how far behind (a little tilt so the view has an up direction)
local active = false

if not (dc.camera and dc.town_pos) then
  dc.log("Expanded Town: this build has no camera API; skipped")
  return
end

dc.on("tick", function(t)
  if dc.dungeon() >= 0 then -- only in towns
    if active then dc.camera(); active = false end
    return
  end
  if (dc.key_pressed("l1+select") and not dc.key_down("r1")) or dc.key_pressed("o") then
    active = not active
    if not active then dc.camera() end
  end
  if active then
    local x, y, z = dc.town_pos()
    dc.camera(x, y + HEIGHT, z + BACK, x, y, z)
  end
end)

dc.log("Expanded Town loaded: L1+Select toggles the overhead camera")
