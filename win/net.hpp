#pragma once

// Multiplayer transport (Windows fork). TCP, host plus up to 3 guests. See net_win.cpp and win-save/mods/MULTIPLAYER.md.
// Host side only: plain types, no game headers.
#include <cstddef>
#include <string>
#include <vector>

enum NetStatus { NET_IDLE = 0, NET_HOSTING = 1, NET_CONNECTING = 2, NET_CONNECTED = 3 };
enum NetEventKind { NET_EV_PEER_JOIN = 1, NET_EV_PEER_LEAVE = 2, NET_EV_MESSAGE = 3, NET_EV_CONNECTED = 4, NET_EV_FAILED = 5, NET_EV_DISCONNECTED = 6 };

struct NetEvent {
    int         kind = 0;
    int         peer = 0;  // PEER_JOIN/LEAVE: the peer; MESSAGE: the sender; CONNECTED: our own id
    std::string data;      // MESSAGE: the bytes; FAILED/DISCONNECTED: the reason
};

constexpr int kNetHostId = 0;
constexpr int kNetAll = -1;
constexpr int kNetMaxGuests = 3;

bool NetHost(int port, std::string *err);
bool NetJoin(const std::string &host, int port, std::string *err); // returns at once; watch for CONNECTED / FAILED
void NetStop();
int NetStatusNow();
int NetLocalId();                 // 0 for the host, 1..3 for a guest, -1 while idle or connecting
std::vector<int> NetPeers();      // everyone else currently in the session (a guest sees only the host, 0)
bool NetSend(int to, const void *data, size_t len); // to: a peer id or kNetAll
void NetPump();                   // accept, connect, read, flush; call once per tick
bool NetPoll(NetEvent &out);      // next queued event, false when empty
