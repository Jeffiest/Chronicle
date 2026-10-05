// Standalone check of the multiplayer transport: one host and two guests in one process over 127.0.0.1.
// Build: see win/tests/run_net_selftest.ps1. Prints PASS/FAIL lines; exit code 0 when all pass.
#include "../net.hpp"
#include <cstdio>
#include <cstring>
#include <windows.h>

static int fails = 0;
#define CHECK(c) do { if (c) std::printf("PASS %s\n", #c); else { std::printf("FAIL %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

// The module is a singleton (one session per process), so the self test drives a host in this process and spawns two guest
// processes of itself. Mode "guest <id-label>" joins, sends a message, expects the host's reply, then leaves.
static bool Wait(NetEvent &e, int kind, int ms) {
    for (int t = 0; t < ms; t += 5) {
        NetPump();
        while (NetPoll(e)) { if (e.kind == kind) return true; }
        Sleep(5);
    }
    return false;
}

static int guest(const char *label) {
    NetEvent e;
    std::string err;
    if (!NetJoin("127.0.0.1", 7791, &err)) return 10;
    if (!Wait(e, NET_EV_CONNECTED, 3000)) return 11;
    int id = e.peer;
    std::string hello = std::string("hi from ") + label;
    NetSend(kNetHostId, hello.data(), hello.size());
    if (!Wait(e, NET_EV_MESSAGE, 3000)) return 12;
    if (e.peer != 0 || e.data != "welcome") return 13;
    if (std::strcmp(label, "A") == 0) { // A also expects B's broadcast relayed through the host
        if (!Wait(e, NET_EV_MESSAGE, 4000)) return 14;
        if (e.data != "B says hello all") return 15;
    } else {
        std::string all = "B says hello all";
        Sleep(1500);
        NetSend(kNetAll, all.data(), all.size());
        for (int i = 0; i < 50; i++) { NetPump(); Sleep(10); }
    }
    NetStop();
    return 100 + id;
}

int main(int argc, char **argv) {
    if (argc >= 3 && std::strcmp(argv[1], "guest") == 0) return guest(argv[2]);
    std::string err;
    CHECK(NetHost(7791, &err));
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    PROCESS_INFORMATION pi[2] = {};
    const char *labels[2] = {"A", "B"};
    for (int i = 0; i < 2; i++) {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        std::string cmd = std::string("\"") + self + "\" guest " + labels[i];
        CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi[i]);
        Sleep(300);
    }
    int joins = 0, msgs = 0, leaves = 0;
    bool welcomed[2] = {false, false};
    for (int t = 0; t < 8000; t += 5) {
        NetPump();
        NetEvent e;
        while (NetPoll(e)) {
            if (e.kind == NET_EV_PEER_JOIN) joins++;
            if (e.kind == NET_EV_PEER_LEAVE) leaves++;
            if (e.kind == NET_EV_MESSAGE) { msgs++; NetSend(e.peer, "welcome", 7); }
        }
        if (leaves >= 2) break;
        Sleep(5);
    }
    CHECK(joins == 2);
    CHECK(msgs >= 3); // two hellos plus B's broadcast
    CHECK(leaves == 2);
    for (int i = 0; i < 2; i++) {
        WaitForSingleObject(pi[i].hProcess, 5000);
        DWORD code = 0;
        GetExitCodeProcess(pi[i].hProcess, &code);
        std::printf("guest %s exit %lu\n", labels[i], code);
        CHECK(code >= 101 && code <= 103);
    }
    NetStop();
    std::printf(fails ? "SELFTEST FAILED\n" : "SELFTEST OK\n");
    return fails;
}
