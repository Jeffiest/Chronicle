-- Controls: number keys for quick items and party members, the mouse wheel, and an in-game key remapping page (Hub > Controls).
-- The default layout is set in the engine (see README.txt); dc.rebind / dc.binding / dc.capture_key do the remapping and keep it in mods/_bindings.json.
if not (dc.rebind and dc.binding and dc.capture_key and dc.press) then
  dc.log("Controls: this build has no key binding API; skipped")
  return
end
local UI = require("listui")

-- what each action does in this game, in the order the remap page lists them
local ACTIONS = {
  { "cross", "Attack / talk / open / confirm" },
  { "circle", "Lock on  /  Back" },
  { "square", "Use the active item" },
  { "triangle", "Menu" },
  { "r1", "Guard (hold while locked on)" },
  { "l1", "Next target" },
  { "l2", "Camera behind you (hold)" },
  { "r2", "Aim view (Xiao, Ruby, Goro)" },
  { "start", "Pause" },
  { "select", "Switch character" },
  { "ly-", "Move forward" },
  { "ly+", "Move back" },
  { "lx-", "Move left" },
  { "lx+", "Move right" },
  { "rx-", "Camera left" },
  { "rx+", "Camera right" },
  { "ry-", "Camera down" },
  { "ry+", "Camera up" },
}
local PARTY = { [0] = "Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond" }

local ticks = 0
local capture = nil       -- { action=, add= } while waiting for the new key
local swap = nil          -- { target=, next=, tries= } while stepping to a party member
local page_cur = 1

local function names_of(action)
  local out = {}
  for n in (dc.binding(action) or ""):gmatch("[^,]+") do out[#out + 1] = (n:gsub("^%s+", ""):gsub("%s+$", "")) end
  return out
end

local function set_names(action, names)
  if #names == 0 then names = { "" } end
  return dc.rebind(action, names)
end

local open_page
local function start_capture(action, add)
  capture = { action = action, add = add, since = ticks }
  dc.block_input(true)
  dc.capture_key() -- clear what is held now
end

local function finish_capture(key, native)
  local c = capture
  capture = nil
  dc.rect("ctl_bg", 0, 0, 0, 0, 0)
  dc.text("ctl_a", ""); dc.text("ctl_b", "")
  if not native then dc.block_input(false) end
  if key == "Escape" then
    dc.toast("Cancelled", 1.5)
  else
    -- a key can only do one thing: take it from any other action that has it
    for _, a in ipairs(ACTIONS) do
      if a[1] ~= c.action then
        local ns, changed = names_of(a[1]), false
        for i = #ns, 1, -1 do
          if ns[i] == key then table.remove(ns, i); changed = true end
        end
        if changed then
          if #ns == 0 then ns = {} end
          if #ns > 0 then dc.rebind(a[1], ns) else dc.rebind(a[1], {}) end
          dc.toast(key .. " moved from \"" .. a[2] .. "\"", 2.5)
        end
      end
    end
    local mine = c.add and names_of(c.action) or {}
    local dup = false
    for _, n in ipairs(mine) do if n == key then dup = true end end
    if not dup then mine[#mine + 1] = key end
    if not dc.rebind(c.action, mine) then dc.toast("That key cannot be used for this action", 2.5) end
  end
  if not native then open_page() end
end

local function draw_capture()
  local what
  for _, a in ipairs(ACTIONS) do if a[1] == capture.action then what = a[2] end end
  dc.rect("ctl_bg", 90, 170, 460, 120, 0x0C1220F0, 40)
  dc.text("ctl_a", (capture.add and "ADD A KEY: " or "NEW KEY: ") .. what, 106, 190, 1.8, 0xFFD060FF, false)
  dc.text("ctl_b", "Press a key or a mouse button.   Esc: cancel", 106, 240, 1.4, 0xC0C0C0FF, false)
end

-- the same page in the game's own manual screen (the MODS entry of the main menu): rows are the game's message windows, Cross replaces a key,
-- Square adds a second one, Circle goes back to the MODS list. Opened from the Hub (K) there is no such screen, so the overlay list is used.
local NV = { on = false, cur = 1, top = 1, last = nil, since = 0 }
local VISIBLE = 5

local function native_rows()
  local rows = {}
  for _, a in ipairs(ACTIONS) do
    local keys = dc.binding(a[1])
    if keys == "" then keys = "none" end
    rows[#rows + 1] = { text = a[2] .. ":  " .. keys, id = a[1], help = "Cross: change   Square: add a key" }
  end
  rows[#rows + 1] = { text = "Reset every key to the default", id = "reset", help = "Puts all keys back." }
  rows[#rows + 1] = { text = "Back", id = "back", help = "Return to the MODS list." }
  return rows
end

local function native_show(rows)
  local shown = {}
  for i = NV.top, math.min(#rows, NV.top + VISIBLE - 1) do shown[#shown + 1] = rows[i].text end
  local help = rows[NV.cur] and rows[NV.cur].help or ""
  if capture then help = (capture.add and "Add a key" or "Press the new key") .. "   (Esc: cancel)" end
  dc.nui_set({ rows = shown, cursor = NV.cur - NV.top + 1, help = help })
end

local function native_tick()
  if not NV.on then return false end
  if not dc.nui_ready() then NV.on = false; return false end
  local rows = native_rows()
  if capture then
    local key = dc.capture_key()
    if key and ticks - capture.since > 5 then finish_capture(key, true) end
    native_show(rows)
    return true
  end
  local settled = ticks - NV.since > 20
  local n = #rows
  if settled and dc.key_pressed("down") then NV.cur = NV.cur % n + 1 end
  if settled and dc.key_pressed("up") then NV.cur = (NV.cur - 2) % n + 1 end
  if NV.cur < NV.top then NV.top = NV.cur end
  if NV.cur > NV.top + VISIBLE - 1 then NV.top = NV.cur - VISIBLE + 1 end
  if settled and dc.key_pressed("circle") then NV.on = false; dc.nui_release(); return true end
  local row = rows[NV.cur]
  if settled and dc.key_pressed("cross") then
    if row.id == "back" then NV.on = false; dc.nui_release(); return true end
    if row.id == "reset" then dc.reset_bindings(); dc.toast("Keys reset to the defaults", 2)
    else capture = { action = row.id, add = false, since = ticks }; dc.capture_key() end
  elseif settled and dc.key_pressed("square") and row.id ~= "reset" and row.id ~= "back" then
    capture = { action = row.id, add = true, since = ticks }
    dc.capture_key()
  end
  local sig = NV.cur .. ":" .. NV.top .. ":" .. row.text
  if sig ~= NV.last or dc.nui_owner() ~= "controls" then NV.last = sig; native_show(rows) end
  return true
end

open_page = function()
  local rows = {}
  for _, a in ipairs(ACTIONS) do
    local keys = dc.binding(a[1])
    if keys == "" then keys = "(none)" end
    rows[#rows + 1] = { text = string.format("%-34s %s", a[2], keys), id = a[1] }
  end
  rows[#rows + 1] = { text = "Reset all keys to the defaults", id = "reset", color = 0xFFB040FF }
  UI.open({
    title = "CONTROLS",
    rows = rows,
    footer = "Cross: replace the key   Square: add a second key   Circle / Esc: close",
    cur = page_cur,
    on_select = function(row, index)
      page_cur = index
      if row.id == "reset" then
        dc.reset_bindings()
        dc.toast("Keys reset to the defaults", 2)
        open_page()
      else
        start_capture(row.id, false)
      end
    end,
    on_alt = function(row, index)
      page_cur = index
      if row.id ~= "reset" then start_capture(row.id, true) else open_page() end
    end,
  })
end

dc.msg_on("hub_collect", function() dc.msg("hub_entry", "Controls", "controls.remap") end)
dc.msg_on("hub_open", function(id)
  if id ~= "controls.remap" then return end
  if dc.nui_ready() then
    NV.on, NV.cur, NV.top, NV.last, NV.since = true, 1, 1, nil, ticks
  else
    page_cur = 1
    open_page()
  end
end)

local function step_swap()
  if not swap then return end
  if dc.chara() == swap.target then swap = nil; return end
  if ticks >= swap.next then
    if swap.tries >= 7 then
      dc.toast(PARTY[swap.target] .. " is not in the party yet", 2)
      swap = nil
      return
    end
    dc.press("select", 2)
    swap.tries = swap.tries + 1
    swap.next = ticks + 24
  end
end

-- first person (V): the camera moves to the character's eyes; the mouse looks around, looking down shows the body and the weapon
local function first_person_tick(in_play)
  if not dc.first_person then return end
  local on = dc.first_person()
  if on and not in_play then dc.first_person(false); on = false end
  if in_play and not dc.menu_open() and not dc.frozen() and dc.key_pressed("v") then
    on = dc.first_person(not on)
    dc.toast(on and "First person" or "Third person", 1.2)
  end
  if on and not dc.menu_open() then
    dc.rect("fp_dot", 318, 238, 4, 4, 0xFFFFFFC0, 3)
  else
    dc.rect("fp_dot", 0, 0, 0, 0, 0)
  end
end

dc.on("tick", function()
  ticks = ticks + 1
  first_person_tick(dc.scene():sub(1, 1) == "D" or dc.scene():sub(1, 1) == "T")
  if native_tick() then return end
  if capture then
    local key = dc.capture_key()
    if key and ticks - capture.since > 5 then finish_capture(key) else draw_capture() end
    return
  end
  if UI.tick() then return end
  local in_dungeon = dc.scene():sub(1, 1) == "D"
  if dc.scene():sub(1, 1) == "T" and dc.town_chara and not dc.menu_open() and not dc.frozen() then
    -- towns: 4-9 play as that party member (once they have joined), Backspace (select) goes to the next one
    for k = 4, 9 do
      if dc.key_pressed(tostring(k)) then
        local want = k - 4
        if dc.town_chara(want) ~= want then dc.toast(PARTY[want] .. " is not in the party yet", 2) end
      end
    end
    if dc.key_pressed("select") then
      local cur = dc.town_chara()
      for step = 1, 5 do
        local nxt = (cur + step) % 6
        if dc.town_chara(nxt) == nxt then break end
      end
    end
  end
  dc.wheel_mode((in_dungeon and not dc.menu_open() and not dc.frozen()) and 0 or 1)
  if not in_dungeon or dc.menu_open() or dc.frozen() then return end
  for n = 1, 3 do
    if dc.key_pressed(tostring(n)) and dc.select_quick_item(n) > 0 then dc.press("square", 3) end -- choose quick item n and use it
  end
  for k = 4, 9 do
    if dc.key_pressed(tostring(k)) and not swap then
      local target = k - 4
      if dc.chara() ~= target then swap = { target = target, next = ticks, tries = 0 } end
    end
  end
  step_swap()
end)

dc.log("Controls loaded: 1-3 quick items, 4-9 party, Hub > Controls to remap")
