-- Hub: one menu for everything the mods add. Open it with L1+R1+Select (or K on the keyboard). Other mods put themselves on it:
--   dc.msg_on("hub_collect", function() dc.msg("hub_entry", "Title shown", "my.id") end)
--   dc.msg_on("hub_open", function(id) if id == "my.id" then ... open the screen ... end end)
-- The hub asks everyone ("hub_collect"), lists the answers, and tells the chosen owner ("hub_open").
local UI = require("listui")

local entries = {}
dc.msg_on("hub_entry", function(title, id) entries[#entries + 1] = { title = title, id = id } end)

local function open_hub()
  entries = {}
  dc.msg("hub_collect")
  local rows = {}
  for _, e in ipairs(entries) do rows[#rows + 1] = { text = e.title, id = e.id } end
  if #rows == 0 then rows[1] = { text = "(nothing is installed here yet)", color = 0x808080FF } end
  UI.open({
    title = "MENU",
    rows = rows,
    footer = "Up/Down: choose   Cross: open   Circle: close",
    on_select = function(row)
      if row.id then dc.msg("hub_open", row.id) end
    end,
  })
end

dc.on("tick", function()
  if UI.tick() then return end
  if dc.key_pressed("l1+r1+select") or dc.key_pressed("k") then open_hub() end
end)

dc.log("Hub loaded: L1+R1+Select or K")
