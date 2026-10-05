-- listui: a scrollable list screen drawn with dc.text / dc.rect. The hub and every screen it opens use their own copy of this file
-- (mods cannot share code). While a list is open the game does not see the pad: dc.freeze in a dungeon (the world holds still),
-- dc.block_input in a town. Up/Down move, Left/Right page, Cross selects (when there is an on_select), Circle or Triangle closes.
local UI = { is_open = false }

local NS = "lui_"
local VISIBLE = 13
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
  if st.held == "freeze" then dc.freeze(false) else dc.block_input(false) end
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

-- opts: title, rows = { {text=, color=0xRRGGBBAA, id=anything}, ... }, footer, on_select = function(row, index)
function UI.open(opts)
  st = { title = opts.title or "", rows = opts.rows or {}, footer = opts.footer, on_select = opts.on_select, cur = 1, top = 1 }
  hold()
  UI.is_open = true
end

function UI.close()
  if not UI.is_open then return end
  clear()
  release()
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

-- one call per tick; returns true while the list owns the screen and the pad
function UI.tick()
  if not UI.is_open then return false end
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
  if dc.key_pressed("circle") or dc.key_pressed("triangle") then
    UI.close()
    return false
  end
  draw()
  return true
end

return UI
