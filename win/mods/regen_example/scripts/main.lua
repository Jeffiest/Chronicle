-- Example mod: regenerate 1 HP per second and announce floor changes. Delete or disable it ({"enabled": false} in mod.json).
dc.on("init", function() dc.log("regen_example loaded, api " .. dc.api_version) end)
dc.on("tick", function(n)
  if n % 50 == 0 and dc.hp() > 0 then dc.set_hp(dc.hp() + 1) end
end)
dc.on("floor_change", function(d, f, pd, pf) dc.log(("dungeon %d floor %d (was %d/%d)"):format(d, f, pd, pf)) end)
