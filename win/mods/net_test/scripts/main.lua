-- net_test: phase 1 of the multiplayer mod. F8 = host on port 7777, F9 = join 127.0.0.1:7777, F10 = disconnect.
-- Every 6 ticks each side broadcasts "P x y z"; the HUD shows what the other players sent.
local PORT = 7777
local HOST_IP = "127.0.0.1"   -- change to the host's Tailscale IP to play across the internet
local remote = {}             -- peer id -> {x,y,z, seen_tick}
local ticks = 0
local recv_count = 0

local function log(...) dc.log("net_test: " .. table.concat({...}, " ")) end

dc.on("tick", function()
  ticks = ticks + 1
  if (dc.key_pressed("f8") or dc.key_pressed("ctrl+h")) then
    local ok, err = net.host(PORT)
    dc.toast(ok and ("HOSTING :" .. PORT) or ("HOST FAILED " .. tostring(err)), 3)
    log("host", tostring(ok), tostring(err))
  elseif (dc.key_pressed("f9") or dc.key_pressed("ctrl+j")) then
    local ok, err = net.join(HOST_IP, PORT)
    dc.toast(ok and ("JOINING " .. HOST_IP) or ("JOIN FAILED " .. tostring(err)), 3)
    log("join", tostring(ok), tostring(err))
  elseif (dc.key_pressed("f10") or dc.key_pressed("ctrl+k")) then
    net.stop(); remote = {}
    dc.toast("NET STOPPED", 2)
  end

  while true do
    local kind, peer, data = net.poll()
    if not kind then break end
    if kind == "message" then
      recv_count = recv_count + 1
      local x, y, z = data:match("^P ([%-%d%.]+) ([%-%d%.]+) ([%-%d%.]+)")
      if x then remote[peer] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), at = ticks} end
    else
      log(kind, peer, data)
      dc.toast("NET " .. kind .. " " .. peer .. " " .. data, 3)
      if kind == "leave" or kind == "disconnected" then remote[peer] = nil end
    end
  end

  if net.status() ~= "idle" and ticks % 6 == 0 then
    local x, y, z = dc.player_pos()
    net.send(-1, string.format("P %.2f %.2f %.2f", x, y, z))
  end

  local line = "NET " .. net.status() .. " id=" .. net.id() .. " peers=" .. #net.peers() .. " rx=" .. recv_count
  dc.text("net_status", line, 8, 420, 2, 0x00FFFFFF)
  dc.text("net_help", "F8 or CTRL+H HOST   F9 or CTRL+J JOIN   F10 or CTRL+K STOP", 8, 440, 1, 0xFFFFFFFF)
  local row = 0
  for id, r in pairs(remote) do
    dc.text("net_peer" .. id, string.format("P%d %.0f %.0f %.0f", id, r.x, r.y, r.z), 8, 400 - row * 16, 2, 0x00FF00FF)
    row = row + 1
  end
end)
