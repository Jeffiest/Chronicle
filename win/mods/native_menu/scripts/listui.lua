-- listui: a scrollable list screen. Every mod that shows lists uses its own copy of this file (mods cannot share code).
-- In the game's MODS menu (the manual screen) the list is shown in the game's own message windows (dc.nui_set); there Up/Down move, Left/Right page, Cross
-- selects, Circle goes back to the MODS list. Anywhere else (the Hub on K) it is drawn with dc.text / dc.rect and holds the pad: dc.freeze in a dungeon
-- (the world holds still), dc.block_input in a town. Square calls on_alt when there is one.
local UI = { is_open = false }

local NS = "lui_"
local VISIBLE = 13
local NATIVE_VISIBLE = 5
local st = {}

local function T(id, text, x, y, s, c) dc.text(NS .. id, text, x, y, s, c, false) end
local function R(id, x, y, w, h, c, z) dc.rect(NS .. id, x, y, w, h, c, z or 0) end

local function hold()
  if dc.scene():sub(1, 1) == "D" and dc.freeze(true) then
    st.held = "freeze"
  else
    dc.block_input(true)
    st.held = "block"
  end
end

local function release()
  if st.held == "freeze" then dc.freeze(false) elseif st.held == "block" then dc.block_input(false) end
  st.held = nil
end

local function clear()
  R("bg", 0, 0, 0, 0, 0)
  R("sel", 0, 0, 0, 0, 0)
  T("title", "", 0, 0, 1, 0)
  T("footer", "", 0, 0, 1, 0)
  T("count", "", 0, 0, 1, 0)
  for i = 1, VISIBLE do T("row" .. i, "", 0, 0, 1, 0) end
end

-- opts: title, rows = { {text=, color=0xRRGGBBAA, id=anything}, ... }, footer, on_select = function(row, index), on_alt = same (Square), cur
function UI.open(opts)
  st = { title = opts.title or "", rows = opts.rows or {}, footer = opts.footer, on_select = opts.on_select, on_alt = opts.on_alt, cur = opts.cur or 1, top = 1 }
  if dc.nui_ready and dc.nui_ready() then
    st.native = true
    st.since = dc.ticks()
    st.last = nil
  else
    hold()
  end
  UI.is_open = true
end

function UI.close()
  if not UI.is_open then return end
  if st.native then
    dc.nui_release()
  else
    clear()
    release()
  end
  UI.is_open = false
end

local function draw()
  R("bg", 24, 24, 592, 432, 0x0C1220F0, 5)
  T("title", st.title, 40, 34, 2, 0xFFD060FF)
  local n = #st.rows
  T("count", n > 0 and (st.cur .. "/" .. n) or "", 540, 38, 1.2, 0x909090FF)
  if n > 0 and st.cur >= st.top and st.cur < st.top + VISIBLE then
    R("sel", 34, 66 + (st.cur - st.top) * 26, 572, 24, 0x405080FF, 6)
  else
    R("sel", 0, 0, 0, 0, 0)
  end
  for i = 1, VISIBLE do
    local row = st.rows[st.top + i - 1]
    if row then
      T("row" .. i, row.text, 44, 70 + (i - 1) * 26, 1.6, row.color or 0xFFFFFFFF)
    else
      T("row" .. i, "", 0, 0, 1, 0)
    end
  end
  T("footer", st.footer or (st.on_select and "Up/Down: choose   Cross: open   Circle: close" or "Up/Down: scroll   Left/Right: page   Circle: close"),
    40, 432, 1.2, 0x909090FF)
end

local function native_tick()
  if not dc.nui_ready() then -- the MODS screen was left under us
    st.native = false
    UI.is_open = false
    return false
  end
  local n = #st.rows
  local settled = dc.ticks() - st.since > 20
  if settled and n > 0 then
    if dc.key_pressed("down") then st.cur = st.cur % n + 1 end
    if dc.key_pressed("up") then st.cur = (st.cur - 2) % n + 1 end
    if dc.key_pressed("right") then st.cur = math.min(n, st.cur + NATIVE_VISIBLE) end
    if dc.key_pressed("left") then st.cur = math.max(1, st.cur - NATIVE_VISIBLE) end
  end
  if st.cur < st.top then st.top = st.cur end
  if st.cur >= st.top + NATIVE_VISIBLE then st.top = st.cur - NATIVE_VISIBLE + 1 end
  if settled and dc.key_pressed("cross") and st.on_select and n > 0 then
    local row, idx, cb = st.rows[st.cur], st.cur, st.on_select
    UI.close()
    cb(row, idx)
    return true
  end
  if settled and dc.key_pressed("square") and st.on_alt and n > 0 then
    local row, idx, cb = st.rows[st.cur], st.cur, st.on_alt
    UI.close()
    cb(row, idx)
    return true
  end
  if settled and (dc.key_pressed("circle") or dc.key_pressed("triangle")) then
    UI.close()
    return true
  end
  local sig = st.cur .. ":" .. st.top .. ":" .. n
  if sig ~= st.last or dc.nui_owner() ~= st.owner_name then
    st.last = sig
    local shown = {}
    for i = st.top, math.min(n, st.top + NATIVE_VISIBLE - 1) do
      local t = tostring(st.rows[i].text)
      shown[#shown + 1] = #t > 34 and t:sub(1, 34) or t
    end
    local help = st.title
    if n > 0 then help = help .. "  " .. st.cur .. "/" .. n end
    dc.nui_set({ rows = shown, cursor = st.cur - st.top + 1, help = help })
  end
  return true
end

-- one call per tick; returns true while the list owns the screen and the pad
function UI.tick()
  if not UI.is_open then return false end
  if st.native then return native_tick() end
  local n = #st.rows
  if dc.key_pressed("down") then st.cur = math.min(n, st.cur + 1) end
  if dc.key_pressed("up") then st.cur = math.max(1, st.cur - 1) end
  if dc.key_pressed("right") then st.cur = math.min(n, st.cur + VISIBLE) end
  if dc.key_pressed("left") then st.cur = math.max(1, st.cur - VISIBLE) end
  if st.cur < st.top then st.top = st.cur end
  if st.cur >= st.top + VISIBLE then st.top = st.cur - VISIBLE + 1 end
  if dc.key_pressed("cross") and st.on_select and n > 0 then
    local row, idx, cb = st.rows[st.cur], st.cur, st.on_select
    UI.close()
    cb(row, idx)
    return true
  end
  if dc.key_pressed("square") and st.on_alt and n > 0 then
    local row, idx, cb = st.rows[st.cur], st.cur, st.on_alt
    UI.close()
    cb(row, idx)
    return true
  end
  if dc.key_pressed("circle") or dc.key_pressed("triangle") or dc.key_pressed("escape") then
    UI.close()
    return false
  end
  draw()
  return true
end

return UI
