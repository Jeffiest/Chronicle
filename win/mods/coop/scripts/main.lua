-- coop: online co-op for Dark Cloud.
--   Ctrl+H (or F8)  host a session on PORT        Ctrl+J (or F9)  join HOST_IP:PORT        Ctrl+K (or F10)  leave
-- Phase 2: every player sees the others as blue Toan ghosts when they are in the same scene (same dungeon floor, or same town
--          map/interior). The host's floor seeds go to guests, so a guest entering a floor the host has built gets the same map.
-- Phase 3: the host runs the monsters. Guests mirror them (position, animation, life) and their hits are applied on the host, so
--          everyone fights the same monsters. Guests' monsters do not attack them yet. Both players must be on the same floor.
local UI = require("listui")
local LAUNCH_MODE = dc.launch and dc.launch("coop") -- set by the launcher: "host" or "join"
local PORT = (dc.launch and tonumber(dc.launch("port"))) or 7777
local DEFAULT_IP = (dc.launch and dc.launch("ip")) or "127.0.0.1"   -- used until you set the host's Tailscale / LAN IP in the menu
local SEND_EVERY = 3          -- game ticks between state messages (20 per second)

local peers = {}              -- id -> last state received
local seeds = {}              -- "dungeon:floor" -> seed the host built it with
local last_seed = 0
local last_send = 0
local ticks = 0
local host_mon = nil          -- guest: last monster snapshot from the host {scene=, t=, list={...}}
local killed_at = {}          -- guest: slot -> tick we asked for a kill, so we do not repeat it
local mon_mismatch = false
local remote_frozen = false     -- we are holding the world because another player paused
local pause_by = nil
local unmatched = {}          -- host: peer id -> bitmask of monster slots that guest cannot mirror
local mon_stats = { host = 0, puppets = 0 }
local ip_edit = nil
local hidden_seen = {}      -- monster slots that were hiding in a chest on this floor (mimics)
local hidden_scene = nil
local last_scene, last_real = nil, 0
local last_state = { 0, 0, 0, 0, 0, 0, 0, 0 }
local last_mask = -1
local SHOW_WEAPON = true       -- draw other players' weapons in dungeons (set false if it ever misbehaves)

local function log(...) dc.log("coop: " .. table.concat({...}, " ")) end
local function key(d, f) return d .. ":" .. f end

local function slot_of(id)
  local me = net.id()
  if id < me then return id + 1 end
  return id
end

local pending_tunic = nil
local last_world = nil        -- host: the last world blob sent
local host_npcs = nil         -- guest: last villager snapshot from the host {scene=, list=, at=}
local npc_talk_sent = -1
local first_seen = {}         -- peer id -> tick their ghost was first wanted
local tunics = {}             -- peer id -> tunic colour (0 natural, 1..15 presets, 16 custom pictures)

local function drop_peer(id)
  if peers[id] then dc.ghost_clear(slot_of(id)) end
  peers[id] = nil
  tunics[id] = nil
  first_seen[id] = nil
end

-- Tell others our tunic colour; custom pictures go as one binary message (the launcher already shrank them). to: a peer id or -1.
local function send_tunic(to, with_pictures)
  if not dc.my_tunic then return end
  local delay = tonumber(dc.launch and dc.launch("tunic_delay") or 0) or 0
  if delay > 0 and ticks < delay then pending_tunic = to; return end -- testing aid: pretend the colour message arrives late
  local c = dc.my_tunic()
  net.send(to, "T " .. c)
  if c == 16 and with_pictures then
    local front, back = dc.launch_file("tunic_front") or "", dc.launch_file("tunic_back") or ""
    if #front + #back > 0 then net.send(to, "U" .. string.pack("<I4I4", #front, #back) .. front .. back) end
  end
end

local function reset_session()
  if dc.world_leave then dc.world_leave() end -- a guest goes back to their own world (keeping the items and gilda they have)
  dc.set_tunic(false)
  if remote_frozen then dc.freeze(false); remote_frozen = false end
  unmatched = {}
  for id in pairs(peers) do drop_peer(id) end
  dc.ghost_clear(); dc.puppet_clear(); dc.clear_floor_seeds()
  seeds = {}; host_mon = nil; killed_at = {}
end

local function parse_monsters(s)
  local scene, rest = s:match("^M (%S+) (.*)$")
  if not scene then return nil end
  local list = {}
  for i, st, kind, hp, max, x, y, z, ry, m, fl, rv in rest:gmatch("(%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%S-),(%S-),(%S-),(%S-),(%-?%d+),(%-?%d+),(%-?%d+);") do
    st = tonumber(st)
    hp = tonumber(hp)
    -- state -1 = empty/dead slot, 1 = asleep (too far from every player), 2 = active. Only a dead monster is killed here.
    list[#list + 1] = {i = tonumber(i), st = st, rv = tonumber(rv), dead = (st == -1 or hp <= 0), kind = tonumber(kind), hp = hp, max = tonumber(max),
                       x = tonumber(x), y = tonumber(y), z = tonumber(z), ry = tonumber(ry), m = tonumber(m), fl = tonumber(fl)}
  end
  return {scene = scene, list = list}
end

local function handle(kind, peer, data)
  if kind == "message" then
    local tag = data:sub(1, 1)
    if tag == "S" then
      local sc, x, y, z, rx, ry, rz, m, fl, w, pz = data:match("^S (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%-?%d+) (%-?%d+) (%-?%d+) (%d)")
      if sc then
        peers[peer] = {scene = sc, x = tonumber(x), y = tonumber(y), z = tonumber(z),
                       rx = tonumber(rx), ry = tonumber(ry), rz = tonumber(rz), m = tonumber(m), fl = tonumber(fl),
                       weapon = tonumber(w), paused = pz == "1", at = ticks}
      end
    elseif tag == "T" then
      local c = tonumber(data:match("^T (%d+)"))
      if c and c >= 0 and c <= 16 then tunics[peer] = c; log("tunic of", peer, "is", c) end
    elseif tag == "U" and #data >= 9 then
      local fl, bl = string.unpack("<I4I4", data, 2)
      if fl <= 400000 and bl <= 400000 and #data >= 9 + fl + bl then
        dc.ghost_tunic(slot_of(peer), 16, data:sub(10, 9 + fl), data:sub(10 + fl, 9 + fl + bl))
        tunics[peer] = 16
        log("custom tunic pictures from", peer, fl, bl)
      end
    elseif tag == "V" and peer == 0 then
      local sc, clk, rest = data:match("^V (%S+) (%S+) (.*)$")
      if sc then
        local list = {}
        for id, x, y, z, ry, m, fl, sp in rest:gmatch("(%-?%d+),(%S-),(%S-),(%S-),(%S-),(%-?%d+),(%-?%d+),(%S-);") do
          list[#list + 1] = { id = tonumber(id), x = tonumber(x), y = tonumber(y), z = tonumber(z), ry = tonumber(ry), m = tonumber(m), fl = tonumber(fl), sp = tonumber(sp) }
        end
        host_npcs = { scene = sc, list = list, at = ticks, clock = tonumber(clk) }
      end
    elseif tag == "W" and peer == 0 and dc.world_join then
      if not dc.world_active() then dc.toast("ENTERING THE HOST'S WORLD", 4) end
      dc.world_join(data:sub(2))
    elseif tag == "G" then
      local kind, map, parts, x, y, z, rot = data:match("^G (%d) (%-?%d+) (%-?%d+) (%S+) (%S+) (%S+) (%-?%d+)")
      if kind and dc.georama_apply then
        if dc.georama_apply(tonumber(kind), tonumber(map), tonumber(parts), tonumber(x), tonumber(y), tonumber(z), tonumber(rot)) then
          dc.toast("PLAYER " .. peer .. (kind == "1" and " BUILT SOMETHING" or " REMOVED SOMETHING"), 2)
        end
      end
    elseif tag == "N" and net.status() == "hosting" then
      local id = tonumber(data:match("^N (%-?%d+)"))
      if id and dc.npc_hold then dc.npc_hold(id, 150) end -- a guest is talking to this villager: it stands still
    elseif tag == "F" then
      local d, f, seed = data:match("^F (%-?%d+) (%-?%d+) (%-?%d+)")
      if d then
        seeds[key(d, f)] = tonumber(seed)
        dc.set_floor_seed(tonumber(d), tonumber(f), tonumber(seed))
        log("host floor", d, f, "seed", seed)
      end
    elseif tag == "M" and peer == 0 then
      local snap = parse_monsters(data)
      if snap then snap.t = ticks; host_mon = snap end
    elseif tag == "D" and peer == 0 then
      local mon, dmg, kind, fl = data:match("^D (%d+) (%d+) (%-?%d+) (%-?%d+)")
      if mon then dc.hurt_player(tonumber(dmg), tonumber(kind), tonumber(fl), tonumber(mon)) end
    elseif tag == "C" then
      local idx, kind2 = data:match("^C (%d+) (%-?%d+)")
      idx, kind2 = tonumber(idx), tonumber(kind2)
      if idx then
        local alive, k, _, _, _, _, _, _, _, _, _, rv = dc.monster_state(idx)
        if k == kind2 then
          if rv == 0 then
            dc.remove_chest_monster(idx)            -- still in its chest here: chest and mimic vanish
            hidden_seen[idx] = nil
          elseif alive then
            hidden_seen[idx] = nil                  -- it is already out here: let it die with the other one
            dc.hurt_monster(idx, 1000000, -1)
          end
        end
      end
    elseif tag == "E" then
      local sc, script, mode, ext, px, py, pz, dx, dy, dz = data:match("^E (%S+) (%-?%d+) (%-?%d+) (%-?%d+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+)")
      if sc and sc == dc.scene() then
        dc.run_event(tonumber(script), tonumber(mode), tonumber(ext), tonumber(px), tonumber(py), tonumber(pz), tonumber(dx), tonumber(dy), tonumber(dz))
        dc.toast("PLAYER " .. peer .. " OPENED A DOOR", 2)
      end
    elseif tag == "K" and net.status() == "hosting" then
      local idx, kind2 = data:match("^K (%d+) (%-?%d+)")
      local alive, k, _, _, _, _, _, _, _, _, _, rv = dc.monster_state(tonumber(idx) or 0)
      if idx and alive and k == tonumber(kind2) and rv ~= 0 then   -- not one still hiding in a chest here
        dc.hurt_monster(tonumber(idx), 1000000, -1)
      end
    elseif tag == "X" and net.status() == "hosting" then
      unmatched[peer] = tonumber(data:match("^X (%d+)")) or 0
    elseif tag == "H" and net.status() == "hosting" then
      local idx, dmg = data:match("^H (%d+) (%d+)")
      local _, _, _, _, _, _, _, _, _, _, _, rv = dc.monster_state(tonumber(idx) or 0)
      if idx and rv ~= 0 then dc.hurt_monster(tonumber(idx), tonumber(dmg), -1) end
    end
  elseif kind == "join" then
    dc.toast("PLAYER " .. peer .. " JOINED", 3)
    send_tunic(peer, true)
    if net.status() == "hosting" and dc.world_capture then
      local w = dc.world_capture()
      if w then net.send(peer, "W" .. w) end -- the new guest plays in this world
    end
    if net.status() == "hosting" then
      for k, seed in pairs(seeds) do
        local d, f = k:match("^(%-?%d+):(%-?%d+)$")
        net.send(peer, string.format("F %d %d %d", tonumber(d), tonumber(f), seed))
      end
    end
  elseif kind == "leave" then
    dc.toast("PLAYER " .. peer .. " LEFT", 3)
    drop_peer(peer)
  elseif kind == "connected" then
    dc.set_tunic(true) -- a guest with no colour of their own shows in blue
    send_tunic(-1, true)
    dc.toast("CONNECTED AS PLAYER " .. peer .. "   (tunic colour shows after the next door)", 4)
  elseif kind == "failed" or kind == "disconnected" then
    dc.toast("NET " .. kind .. ": " .. data, 4)
    reset_session()
  end
end

-- A guest tells the host when its own weapon killed a monster, so the host's copy dies too even if a hit was missed.
dc.on("monster_killed", function(idx, kind, attacker)
  if net.status() == "connected" and attacker >= 0 then net.send(0, string.format("K %d %d", idx, kind)) end
  -- A mimic that dies takes its chest with it, for every player (each player's chest is their own, but the mimic is one monster).
  if hidden_seen[idx] and net.status() ~= "idle" then net.send(-1, string.format("C %d %d", idx, kind)) end
end)

-- A guest forwards its own weapon hits to the host, which applies them to the real monster.
dc.on("monster_hit", function(idx, kind, attacker, damage, element)
  if net.status() == "connected" and attacker >= 0 and damage > 0 then
    net.send(0, string.format("H %d %d", idx, damage))
  end
end)

local function pressed(a, b) return dc.key_pressed(a) or dc.key_pressed(b) end

-- Hub entry: a small menu to host, join, set the host's address, or leave. ----------------------------------------------------
local function current_ip() return dc.store_get("ip") or DEFAULT_IP end

local function open_coop_menu()
  local status = net.status()
  local rows = {
    { text = "Host a game (port " .. PORT .. ")", id = "host" },
    { text = "Join " .. current_ip(), id = "join" },
    { text = "Set the host's address", id = "ip" },
    { text = status == "idle" and "Leave session (not connected)" or ("Leave session (" .. status .. ")"), id = "leave" },
  }
  UI.open({ title = "CO-OP", rows = rows, footer = "Up/Down: choose   Cross: select   Circle: close",
    on_select = function(row)
      if row.id == "host" then
        local ok, err = net.host(PORT)
        dc.toast(ok and ("HOSTING ON PORT " .. PORT) or ("HOST FAILED: " .. tostring(err)), 3)
      elseif row.id == "join" then
        local ok, err = net.join(current_ip(), PORT)
        dc.toast(ok and ("JOINING " .. current_ip()) or ("JOIN FAILED: " .. tostring(err)), 3)
      elseif row.id == "leave" then
        net.stop(); reset_session()
        dc.toast("LEFT THE SESSION", 2)
      elseif row.id == "ip" then
        local o = {}
        for n in current_ip():gmatch("%d+") do o[#o + 1] = tonumber(n) end
        while #o < 4 do o[#o + 1] = 0 end
        ip_edit = { o = o, pos = 1 }
        dc.block_input(true)
      end
    end })
end

local function tick_ip_editor()
  local e = ip_edit
  if dc.key_pressed("left") then e.pos = math.max(1, e.pos - 1) end
  if dc.key_pressed("right") then e.pos = math.min(4, e.pos + 1) end
  if dc.key_pressed("up") then e.o[e.pos] = (e.o[e.pos] + 1) % 256 end
  if dc.key_pressed("down") then e.o[e.pos] = (e.o[e.pos] - 1) % 256 end
  if dc.key_pressed("r1") then e.o[e.pos] = (e.o[e.pos] + 10) % 256 end
  if dc.key_pressed("l1") then e.o[e.pos] = (e.o[e.pos] - 10) % 256 end
  local parts = {}
  for i = 1, 4 do parts[i] = (i == e.pos) and ("[" .. e.o[i] .. "]") or tostring(e.o[i]) end
  dc.rect("coop_ip_bg", 70, 160, 500, 150, 0x0C1220F0, 5)
  dc.text("coop_ip_t", "HOST ADDRESS", 90, 170, 2, 0xFFD060FF, false)
  dc.text("coop_ip_v", table.concat(parts, " . "), 90, 210, 3, 0xFFFFFFFF, false)
  dc.text("coop_ip_h", "Left/Right: part   Up/Down: +/-1   L1/R1: -/+10   Cross: save   Circle: cancel", 90, 280, 1.2, 0x909090FF, false)
  if dc.key_pressed("cross") then
    dc.store_set("ip", e.o[1] .. "." .. e.o[2] .. "." .. e.o[3] .. "." .. e.o[4])
    dc.toast("HOST ADDRESS " .. e.o[1] .. "." .. e.o[2] .. "." .. e.o[3] .. "." .. e.o[4], 3)
  end
  if dc.key_pressed("cross") or dc.key_pressed("circle") then
    ip_edit = nil
    dc.block_input(false)
    dc.rect("coop_ip_bg", 0, 0, 0, 0, 0)
    dc.text("coop_ip_t", "", 0, 0, 1, 0); dc.text("coop_ip_v", "", 0, 0, 1, 0); dc.text("coop_ip_h", "", 0, 0, 1, 0)
  end
end

dc.msg_on("hub_collect", function() dc.msg("hub_entry", "Co-op", "coop.menu") end)
dc.msg_on("hub_open", function(id) if id == "coop.menu" then open_coop_menu() end end)

local launched = false
dc.on("tick", function()
  ticks = ticks + 1
  if pending_tunic and (tonumber(dc.launch and dc.launch("tunic_delay") or 0) or 0) <= ticks then local to = pending_tunic; pending_tunic = nil; send_tunic(to, true) end
  if LAUNCH_MODE and not launched and ticks > 60 then -- started from the launcher: host or join once the game is up
    launched = true
    if LAUNCH_MODE == "host" then
      local ok, err = net.host(PORT)
      dc.toast(ok and ("HOSTING ON PORT " .. PORT) or ("HOST FAILED: " .. tostring(err)), 4)
    elseif LAUNCH_MODE == "join" then
      local ok, err = net.join(DEFAULT_IP, PORT)
      dc.toast(ok and ("JOINING " .. DEFAULT_IP .. ":" .. PORT) or ("JOIN FAILED: " .. tostring(err)), 4)
    end
  end
  if ip_edit then tick_ip_editor() else UI.tick() end
  if pressed("ctrl+h", "f8") then
    local ok, err = net.host(PORT)
    dc.toast(ok and ("HOSTING ON PORT " .. PORT) or ("HOST FAILED: " .. tostring(err)), 3)
  elseif pressed("ctrl+j", "f9") then
    local ip = dc.store_get("ip") or DEFAULT_IP
    local ok, err = net.join(ip, PORT)
    dc.toast(ok and ("JOINING " .. ip) or ("JOIN FAILED: " .. tostring(err)), 3)
  elseif pressed("ctrl+k", "f10") then
    net.stop(); reset_session()
    dc.toast("LEFT THE SESSION", 2)
  end

  while true do
    local kind, peer, data = net.poll()
    if not kind then break end
    handle(kind, peer, data)
  end

  local status = net.status()
  local d, f = dc.dungeon(), dc.floor()
  local seed = dc.floor_seed()
  local scene = dc.scene()                -- "D<dungeon>:<floor>", "T<town>:<map>:<interior>", or "-" (menu/loading)
  -- A menu or a loading screen shows "-"; keep telling the others where we were (for up to 20 s) so we do not vanish from their screen.
  if scene ~= "-" then
    last_scene, last_real = scene, ticks
  elseif last_scene and ticks - last_real < 1200 then
    scene = last_scene
  end
  local in_dungeon = scene:sub(1, 1) == "D"
  local now = dc.logic_ticks()
  if scene ~= hidden_scene then hidden_scene, hidden_seen = scene, {} end
  if in_dungeon then
    for i = 0, 15 do
      local _, k, _, _, _, _, _, _, _, _, st, rv = dc.monster_state(i)
      if k and st ~= -1 and rv == 0 then hidden_seen[i] = true end
    end
  end

  -- The host records every floor it builds and tells everyone.
  if status == "hosting" and seed ~= 0 and seed ~= last_seed then
    seeds[key(d, f)] = seed
    net.send(-1, string.format("F %d %d %d", d, f, seed))
    log("built", d, f, "seed", seed)
  end
  last_seed = seed

  if status == "hosting" or status == "connected" then
    if now - last_send >= SEND_EVERY then
      last_send = now
      local x, y, z, rx, ry, rz, m, fl
      if dc.scene() ~= "-" then
        x, y, z, rx, ry, rz, m, fl = dc.player_state()
        last_state = { x, y, z, rx, ry, rz, m, fl }
      else
        x, y, z, rx, ry, rz, m, fl = table.unpack(last_state)   -- in a menu: the last real pose
      end
      local wt = (SHOW_WEAPON and in_dungeon and dc.chara() == 0) and dc.weapon() or nil
      local menu_up = in_dungeon and (dc.menu_open() or (dc.frozen() and not remote_frozen))
      net.send(-1, string.format("S %s %.3f %.3f %.3f %.4f %.4f %.4f %d %d %d %d", scene, x, y, z, rx, ry, rz, m, fl,
        wt and wt.item or 0, menu_up and 1 or 0))
      if status == "hosting" and in_dungeon and #net.peers() > 0 then
        local parts = {}
        for i = 0, 15 do
          local alive, kind, hp, max, mx, my, mz, mry, mm, mfl, st, rv = dc.monster_state(i)
          if kind and (st ~= -1 or hp > 0 or dc.monster_count() > 0) then
            parts[#parts + 1] = string.format("%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.3f,%d,%d,%d;", i, st, kind, hp, max, mx, my, mz, mry, mm, mfl, rv)
          end
        end
        net.send(-1, "M " .. scene .. " " .. table.concat(parts))
      end
    end
    -- Dark-Souls style: the host's world (story flags, unlocked places, Georama, time) is sent to guests; their items and gilda stay their own.
    if status == "hosting" and dc.world_capture and ticks % 180 == 0 and #net.peers() > 0 then
      local w = dc.world_capture()
      if w and w ~= last_world then last_world = w; net.send(-1, "W" .. w) end
    end

    -- Georama: what this player builds or removes goes to everyone in the session (their town applies it if it is the same map).
    if dc.georama_take then
      while true do
        local kind, map, parts, x, y, z, rot = dc.georama_take()
        if not kind then break end
        net.send(-1, string.format("G %d %d %d %.3f %.3f %.3f %d", kind, map, parts, x, y, z, rot))
      end
    end
    -- Town villagers: the host's are the real ones; a guest's follow the host's snapshot. A guest talking to one tells the host to hold it still.
    if dc.npc_list and scene:sub(1, 1) == "T" then
      if status == "hosting" and #net.peers() > 0 and ticks % SEND_EVERY == 0 then
        local parts = {}
        for _, v in ipairs(dc.npc_list()) do
          parts[#parts + 1] = string.format("%d,%.2f,%.2f,%.2f,%.3f,%d,%d,%.3f;", v.id, v.x, v.y, v.z, v.ry, v.m, v.fl, v.sp)
        end
        net.send(-1, string.format("V %s %.4f ", scene, dc.town_clock()) .. table.concat(parts))
      elseif status == "connected" then
        if host_npcs and host_npcs.scene == scene and ticks - host_npcs.at < 120 then
          local mine = dc.town_clock()
          if host_npcs.clock and math.abs(host_npcs.clock - mine) > 0.02 and math.abs(host_npcs.clock - mine) < 6 then dc.town_clock(host_npcs.clock) end -- same time of day as the host
          for _, v in ipairs(host_npcs.list) do dc.npc_puppet(v.id, v.x, v.y, v.z, v.ry, v.m, v.fl, v.sp) end
          if ticks % 300 == 0 then
            local mine, off = dc.npc_list(), {}
            for _, v in ipairs(host_npcs.list) do
              for _, m in ipairs(mine) do
                if m.id == v.id then off[#off + 1] = string.format("%d:%.0f", v.id, math.sqrt((m.x - v.x) ^ 2 + (m.z - v.z) ^ 2)) end
              end
            end
            log("villagers following the host:", #host_npcs.list, "mode", dc.edit_mode and dc.edit_mode() or "?", "distance from host copy:", table.concat(off, " "))
          end
        else
          dc.npc_puppet_clear()
        end
        local talking = dc.npc_talking()
        if talking >= 0 and ticks % 30 == 0 then net.send(0, "N " .. talking) end
      end
    elseif dc.npc_puppet_clear then
      dc.npc_puppet_clear()
    end

    for id, p in pairs(peers) do
      if ticks - p.at > 600 then
        drop_peer(id)                         -- silent for a while: treat as gone
      elseif scene ~= "-" and p.scene == scene then
        first_seen[id] = first_seen[id] or ticks
        if tunics[id] or ticks - first_seen[id] >= 180 then -- wait up to 3 s for their tunic colour, so the ghost is built once, in the right colour
          dc.ghost(slot_of(id), p.x, p.y, p.z, p.rx, p.ry, p.rz, p.m, p.fl, tunics[id] or (id == 0 and 0 or 1), p.weapon or 0)
        end
      else
        dc.ghost_clear(slot_of(id))
      end
    end
  end

  -- Doors and gates this player opened are opened for everyone on the same floor.
  while true do
    local script, mode, ext, px, py, pz, dx, dy, dz = dc.take_event()
    if not script then break end
    if status ~= "idle" and scene:sub(1, 1) == "D" then
      net.send(-1, string.format("E %s %d %d %d %.2f %.2f %.2f %.3f %.3f %.3f", scene, script, mode, ext, px, py, pz, dx, dy, dz))
      log("door script", script, "mode", mode)
    end
  end

  -- Host: monster attacks that landed on a ghost are damage for that guest. (Guests just drain the queue.)
  while true do
    local slot, mon, dmg, kind, fl = dc.take_ghost_hit()
    if not slot then break end
    local blind = ((unmatched[slot] or 0) >> mon) & 1 == 1   -- that guest cannot see this monster: it must not hurt them
    if status == "hosting" and not blind then net.send(slot, string.format("D %d %d %d %d", mon, dmg, kind, fl)) end
  end

  -- Guest: mirror the host's monsters while on the same floor.
  mon_mismatch = false
  mon_stats.host, mon_stats.puppets = 0, 0
  local mask = 0
  if status == "connected" and in_dungeon and host_mon and host_mon.scene == scene and ticks - host_mon.t < 120 then
    for _, e in ipairs(host_mon.list) do
      if not e.dead then
        mon_stats.host = mon_stats.host + 1
        if dc.puppet(e.i, e.kind, e.x, e.y, e.z, e.ry, e.m, e.fl, e.hp, e.max, e.st, e.rv) then
          mon_stats.puppets = mon_stats.puppets + 1
        else
          mon_mismatch = true
          mask = mask | (1 << e.i)
        end
      elseif dc.monster_alive(e.i) and dc.monster_kind(e.i) == e.kind and (not killed_at[e.i] or ticks - killed_at[e.i] > 60) then
        killed_at[e.i] = ticks
        dc.hurt_monster(e.i, 1000000, -1)   -- the host's one is dead: let ours die on the game's own path
      end
    end
  elseif status ~= "connected" or not in_dungeon then
    dc.puppet_clear()
  end
  if status == "connected" and (mask ~= last_mask or ticks % 30 == 0) then net.send(0, "X " .. mask); last_mask = mask end

  -- Shared pause: while another player has the pause or battle menu up, hold the world here too.
  local who = nil
  if in_dungeon then
    for id, p in pairs(peers) do
      if p.paused and p.scene:sub(1, 1) == "D" then who = id end
    end
  end
  if who and not remote_frozen and not dc.menu_open() and dc.freeze(true) then
    remote_frozen = true
  elseif not who and remote_frozen then
    dc.freeze(false)
    remote_frozen = false
  end
  pause_by = who
  if remote_frozen then
    dc.text("coop_pause", "PAUSED BY PLAYER " .. tostring(who), 200, 200, 2, 0xFFD060FF)
  else
    dc.text("coop_pause", "", 0, 0, 1, 0)
  end

  -- HUD
  local line = "COOP " .. status
  if status ~= "idle" then line = line .. " P" .. net.id() end
  dc.text("coop_status", line, 8, 446, 1, 0x80C0FFFF)
  local row = 0
  for id, p in pairs(peers) do
    local same = (scene ~= "-" and p.scene == scene)
    local txt = string.format("P%d  %s  %s", id, p.scene, same and "HERE" or "ELSEWHERE")
    dc.text("coop_peer" .. id, txt, 8, 430 - row * 10, 1, same and 0x60FF60FF or 0xFFC060FF)
    row = row + 1
  end
  if status == "connected" and in_dungeon then
    dc.text("coop_mon", string.format("MONSTERS host %d  mirrored %d  here %d", mon_stats.host, mon_stats.puppets, dc.monster_count()),
      8, 410 - row * 10, 1, 0x9090B0FF)
  else
    dc.text("coop_mon", "", 0, 0, 1, 0)
  end
  local host_seed = seeds[key(d, f)]
  if status == "connected" and in_dungeon and host_seed and seed ~= 0 and seed ~= host_seed then
    dc.text("coop_warn", "THIS FLOOR DIFFERS FROM THE HOST'S: LEAVE AND RE-ENTER IT", 8, 398 - row * 10, 1, 0xFF6060FF)
  elseif mon_mismatch then
    dc.text("coop_warn", "SOME MONSTERS DIFFER FROM THE HOST'S (they cannot hurt you): RE-ENTER THE FLOOR", 8, 398 - row * 10, 1, 0xFF6060FF)
  else
    dc.text("coop_warn", "", 0, 0, 1, 0)
  end
end)
