// Multiplayer transport. TCP with TCP_NODELAY, non-blocking, length-prefixed frames. The host is the hub: a guest talks to the
// host, and the host relays between guests. Chosen over UDP for phase 1 because it is reliable and ordered, which Tailscale and
// LAN latencies tolerate; snapshots are small. Frame: u32 length (of type+body), u8 type, body.
//   type 1 HELLO  host->guest: u8 your_id, u8 n, n x u8 existing guest ids (the host, 0, is implied)
//   type 2 DATA   guest->host: u8 to (0 host, 255 all), bytes   host->guest: u8 from, bytes
//   type 3 JOIN   host->guest: u8 id      type 4 LEAVE  host->guest: u8 id
#include "platform/net.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>

namespace {

constexpr uint32_t kMaxFrame = 1u << 20;
constexpr unsigned long long kConnectTimeoutMs = 6000;

struct Peer {
    SOCKET      s = INVALID_SOCKET;
    int         id = -1;
    std::string rx;
    std::string tx;
};

struct Net {
    bool                 wsa = false;
    int                  status = NET_IDLE;
    int                  local_id = -1;
    SOCKET               listener = INVALID_SOCKET;
    std::vector<Peer>    peers;                      // host: every guest. guest: one entry for the host (id 0)
    SOCKET               pending = INVALID_SOCKET;   // guest while the TCP connect is in flight
    unsigned long long   connect_started = 0;
    std::deque<NetEvent> events;
} n;

bool EnsureWsa() {
    if (!n.wsa) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
            return false;
        }
        n.wsa = true;
    }
    return true;
}

void Nonblock(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
}

void Push(int kind, int peer, std::string data = {}) {
    NetEvent e;
    e.kind = kind;
    e.peer = peer;
    e.data = std::move(data);
    n.events.push_back(std::move(e));
}

void Frame(Peer &p, uint8_t type, const std::string &body) {
    uint32_t len = static_cast<uint32_t>(body.size() + 1);
    p.tx.append(reinterpret_cast<const char *>(&len), 4);
    p.tx.push_back(static_cast<char>(type));
    p.tx += body;
}

void Flush(Peer &p) {
    while (!p.tx.empty() && p.s != INVALID_SOCKET) {
        int sent = send(p.s, p.tx.data(), static_cast<int>(p.tx.size()), 0);
        if (sent > 0) {
            p.tx.erase(0, static_cast<size_t>(sent));
        } else {
            break; // WOULDBLOCK or error; the read side notices a dead socket
        }
    }
}

Peer *FindPeer(int id) {
    for (Peer &p : n.peers) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

void CloseAll() {
    for (Peer &p : n.peers) {
        if (p.s != INVALID_SOCKET) {
            closesocket(p.s);
        }
    }
    n.peers.clear();
    if (n.listener != INVALID_SOCKET) {
        closesocket(n.listener);
        n.listener = INVALID_SOCKET;
    }
    if (n.pending != INVALID_SOCKET) {
        closesocket(n.pending);
        n.pending = INVALID_SOCKET;
    }
}

// Host: a guest left or was dropped. Tell everyone else.
void HostDrop(size_t index) {
    int id = n.peers[index].id;
    closesocket(n.peers[index].s);
    n.peers.erase(n.peers.begin() + static_cast<std::ptrdiff_t>(index));
    Push(NET_EV_PEER_LEAVE, id);
    for (Peer &o : n.peers) {
        Frame(o, 4, std::string(1, static_cast<char>(id)));
    }
}

// Returns false when the frame was malformed and the peer should be dropped.
bool HostFrame(size_t index, uint8_t type, const std::string &body) {
    int from_id = n.peers[index].id;
    if (type != 2 || body.empty()) {
        return false;
    }
    int to = static_cast<uint8_t>(body[0]);
    std::string payload = body.substr(1);
    std::string out(1, static_cast<char>(from_id));
    out += payload;
    if (to == 255) {
        Push(NET_EV_MESSAGE, from_id, payload);
        for (Peer &o : n.peers) {
            if (o.id != from_id) {
                Frame(o, 2, out);
            }
        }
    } else if (to == kNetHostId) {
        Push(NET_EV_MESSAGE, from_id, payload);
    } else if (Peer *o = FindPeer(to); o != nullptr) {
        Frame(*o, 2, out);
    }
    return true;
}

bool GuestFrame(uint8_t type, const std::string &body) {
    if (type == 1) {
        if (body.size() < 2) {
            return false;
        }
        n.local_id = static_cast<uint8_t>(body[0]);
        n.status = NET_CONNECTED;
        Push(NET_EV_CONNECTED, n.local_id);
        Push(NET_EV_PEER_JOIN, kNetHostId);
        int count = static_cast<uint8_t>(body[1]);
        for (int i = 0; i < count && 2 + i < static_cast<int>(body.size()); i++) {
            Push(NET_EV_PEER_JOIN, static_cast<uint8_t>(body[static_cast<size_t>(2 + i)]));
        }
    } else if (type == 2 && !body.empty()) {
        Push(NET_EV_MESSAGE, static_cast<uint8_t>(body[0]), body.substr(1));
    } else if ((type == 3 || type == 4) && !body.empty()) {
        Push(type == 3 ? NET_EV_PEER_JOIN : NET_EV_PEER_LEAVE, static_cast<uint8_t>(body[0]));
    } else {
        return false;
    }
    return true;
}

// Reads everything available from a peer; returns false if the connection died or sent garbage.
bool ReadPeer(size_t index, bool host) {
    Peer &p = n.peers[index];
    char buf[8192];
    for (;;) {
        int got = recv(p.s, buf, sizeof(buf), 0);
        if (got > 0) {
            p.rx.append(buf, static_cast<size_t>(got));
        } else if (got == 0) {
            return false;
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                return false;
            }
            break;
        }
    }
    while (p.rx.size() >= 5) {
        uint32_t len;
        std::memcpy(&len, p.rx.data(), 4);
        if (len < 1 || len > kMaxFrame) {
            return false;
        }
        if (p.rx.size() < 4 + static_cast<size_t>(len)) {
            break;
        }
        uint8_t type = static_cast<uint8_t>(p.rx[4]);
        std::string body = p.rx.substr(5, len - 1);
        p.rx.erase(0, 4 + static_cast<size_t>(len));
        bool ok = host ? HostFrame(index, type, body) : GuestFrame(type, body);
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

bool NetHost(int port, std::string *err) {
    NetStop();
    if (!EnsureWsa()) {
        if (err) *err = "WSAStartup failed";
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (err) *err = "socket failed";
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(static_cast<u_short>(port));
    if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0 || listen(s, 4) != 0) {
        if (err) *err = "cannot listen on port " + std::to_string(port) + " (in use? error " + std::to_string(WSAGetLastError()) + ")";
        closesocket(s);
        return false;
    }
    Nonblock(s);
    n.listener = s;
    n.status = NET_HOSTING;
    n.local_id = kNetHostId;
    return true;
}

bool NetJoin(const std::string &host, int port, std::string *err) {
    NetStop();
    if (!EnsureWsa()) {
        if (err) *err = "WSAStartup failed";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || res == nullptr) {
        if (err) *err = "cannot resolve " + host;
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        if (err) *err = "socket failed";
        return false;
    }
    Nonblock(s);
    connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)); // WOULDBLOCK expected
    freeaddrinfo(res);
    n.pending = s;
    n.status = NET_CONNECTING;
    n.local_id = -1;
    n.connect_started = GetTickCount64();
    return true;
}

void NetStop() {
    CloseAll();
    n.status = NET_IDLE;
    n.local_id = -1;
    n.events.clear();
}

int NetStatusNow() { return n.status; }
int NetLocalId() { return n.local_id; }

std::vector<int> NetPeers() {
    std::vector<int> out;
    for (const Peer &p : n.peers) {
        out.push_back(p.id);
    }
    return out;
}

bool NetSend(int to, const void *data, size_t len) {
    if (len + 2 > kMaxFrame) {
        return false;
    }
    std::string payload(static_cast<const char *>(data), len);
    if (n.status == NET_HOSTING) {
        std::string body(1, static_cast<char>(kNetHostId));
        body += payload;
        if (to == kNetAll) {
            for (Peer &p : n.peers) {
                Frame(p, 2, body);
            }
            return true;
        }
        Peer *p = FindPeer(to);
        if (p == nullptr) {
            return false;
        }
        Frame(*p, 2, body);
        return true;
    }
    if (n.status == NET_CONNECTED && !n.peers.empty()) {
        std::string body(1, static_cast<char>(to == kNetAll ? 255 : to));
        body += payload;
        Frame(n.peers[0], 2, body);
        return true;
    }
    return false;
}

void NetPump() {
    if (n.status == NET_IDLE) {
        return;
    }
    if (n.status == NET_HOSTING) {
        for (;;) {
            SOCKET c = accept(n.listener, nullptr, nullptr);
            if (c == INVALID_SOCKET) {
                break;
            }
            int id = 1;
            while (id <= kNetMaxGuests && FindPeer(id) != nullptr) {
                id++;
            }
            if (id > kNetMaxGuests) {
                closesocket(c); // full
                continue;
            }
            Nonblock(c);
            Peer p;
            p.s = c;
            p.id = id;
            std::string hello;
            hello.push_back(static_cast<char>(id));
            hello.push_back(static_cast<char>(n.peers.size()));
            for (const Peer &o : n.peers) {
                hello.push_back(static_cast<char>(o.id));
            }
            Frame(p, 1, hello);
            for (Peer &o : n.peers) {
                Frame(o, 3, std::string(1, static_cast<char>(id)));
            }
            n.peers.push_back(std::move(p));
            Push(NET_EV_PEER_JOIN, id);
        }
        for (size_t i = 0; i < n.peers.size();) {
            if (!ReadPeer(i, true)) {
                HostDrop(i);
            } else {
                i++;
            }
        }
        for (Peer &p : n.peers) {
            Flush(p);
        }
        return;
    }
    // Guest. Phase one: TCP connect in flight. Phase two: connected, waiting for the host's HELLO.
    bool timed_out = GetTickCount64() - n.connect_started > kConnectTimeoutMs;
    if (n.pending != INVALID_SOCKET) {
        fd_set w, x;
        FD_ZERO(&w);
        FD_ZERO(&x);
        FD_SET(n.pending, &w);
        FD_SET(n.pending, &x);
        timeval tv{0, 0};
        int r = select(0, nullptr, &w, &x, &tv);
        if (r > 0 && FD_ISSET(n.pending, &x)) {
            closesocket(n.pending);
            n.pending = INVALID_SOCKET;
            n.status = NET_IDLE;
            Push(NET_EV_FAILED, 0, "connection refused or unreachable");
        } else if (r > 0 && FD_ISSET(n.pending, &w)) {
            Peer p;
            p.s = n.pending;
            p.id = kNetHostId;
            n.pending = INVALID_SOCKET;
            n.peers.push_back(std::move(p));
        } else if (timed_out) {
            closesocket(n.pending);
            n.pending = INVALID_SOCKET;
            n.status = NET_IDLE;
            Push(NET_EV_FAILED, 0, "timed out");
        }
        return;
    }
    if (n.peers.empty()) {
        return;
    }
    if (!ReadPeer(0, false)) {
        bool was_connected = n.status == NET_CONNECTED;
        CloseAll();
        n.status = NET_IDLE;
        n.local_id = -1;
        Push(was_connected ? NET_EV_DISCONNECTED : NET_EV_FAILED, 0, "host closed the connection");
        return;
    }
    if (n.status == NET_CONNECTING && timed_out) {
        CloseAll();
        n.status = NET_IDLE;
        Push(NET_EV_FAILED, 0, "host did not answer");
        return;
    }
    if (!n.peers.empty()) {
        Flush(n.peers[0]);
    }
}

bool NetPoll(NetEvent &out) {
    if (n.events.empty()) {
        return false;
    }
    out = std::move(n.events.front());
    n.events.pop_front();
    return true;
}
