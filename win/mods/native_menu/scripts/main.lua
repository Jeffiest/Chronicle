-- Native Menu: the MODS entry of the game's main menu (the Manuals slot). The engine opens the game's own manual screen for it (its slide-in, header,
-- message windows, hand cursor and sounds); this script fills the window (dc.nui_set) and handles the keys. The game's hand follows the mouse, a click is
-- Cross, a right click is Circle. A page for a mod is that mod's own: it takes the window with dc.nui_set and gives it back with dc.nui_release() when
-- Circle is pressed, and this list comes back.
if not (dc.mods_menu_requested and dc.manual_pass and dc.msg and dc.nui_set) then
  dc.log("Native Menu: this build has no native page support; skipped")
  return
end

local entries = {}
dc.msg_on("hub_entry", function(title, id) entries[#entries + 1] = { title = title, id = id } end)

local VISIBLE = 5
local active = false
local rows = {}
local cur, top = 1, 1
local last_sig = nil
local opened_at = 0
local pending_manuals = 0 -- ticks until the main menu is back and Cross opens the real manuals

local function build()
  entries = {}
  dc.msg("hub_collect")
  rows = {}
  for _, e in ipairs(entries) do rows[#rows + 1] = { text = e.title, id = e.id, help = "Open " .. e.title .. "." } end
  rows[#rows + 1] = { text = "Manuals (how to play)", id = "native.manuals", help = "The game's original manuals." }
  rows[#rows + 1] = { text = "Back", id = "native.back", help = "Return to the menu." }
  cur, top = 1, 1
end

local function show()
  local shown = {}
  for i = top, math.min(#rows, top + VISIBLE - 1) do shown[#shown + 1] = rows[i].text end
  local help = rows[cur] and rows[cur].help or ""
  dc.nui_set({ rows = shown, cursor = cur - top + 1, help = help })
end

local function choose(row)
  if row.id == "native.back" then
    active = false
    dc.nui_close()
  elseif row.id == "native.manuals" then
    active = false
    dc.manual_pass()
    dc.nui_close()
    pending_manuals = 60
  else
    dc.msg("hub_open", row.id)
  end
end

dc.on("tick", function()
  if pending_manuals > 0 then
    pending_manuals = pending_manuals - 1
    if pending_manuals == 0 then dc.press("cross", 2) end
    return
  end
  if dc.mods_menu_requested() then
    build()
    active = true
    last_sig = nil
    opened_at = dc.ticks()
  end
  if not active then return end
  if not dc.nui_ready() then return end
  local owner = dc.nui_owner()
  if owner ~= "" and owner ~= "native_menu" then return end -- another mod's page is showing
  if dc.input_blocked() then return end                     -- an overlay screen has the pad
  if owner == "" then last_sig = nil end
  local n = #rows
  local settled = dc.ticks() - opened_at > 20 -- the press that opened this page must not choose a row
  if settled and dc.key_pressed("down") then cur = cur % n + 1 end
  if settled and dc.key_pressed("up") then cur = (cur - 2) % n + 1 end
  if cur < top then top = cur end
  if cur > top + VISIBLE - 1 then top = cur - VISIBLE + 1 end
  if settled and dc.key_pressed("cross") then
    choose(rows[cur])
    return
  end
  if settled and dc.key_pressed("circle") then
    active = false
    dc.nui_close()
    return
  end
  local sig = cur .. ":" .. top
  if sig ~= last_sig or owner ~= "native_menu" then
    last_sig = sig
    show()
  end
end)

dc.log("Native Menu loaded: MODS is in the main menu")
