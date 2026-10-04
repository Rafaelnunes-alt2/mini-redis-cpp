// feito por: maquinzz
#include "kv.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#endif
namespace {
std::atomic<int> gConns{0};
constexpr int kMaxConns = 128;
constexpr std::size_t kMaxLine = 4096;
constexpr std::size_t kMaxVal = 1024 * 1024;
void closeSock(sock_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}
bool sendAll(sock_t s, const std::string &m) {
    std::size_t sent = 0;
    while (sent < m.size()) {
        int chunk = static_cast<int>(m.size() - sent > 65536 ? 65536 : m.size() - sent);
#ifdef _WIN32
        int r = send(s, m.data() + sent, chunk, 0);
#else
        ssize_t r = send(s, m.data() + sent, static_cast<std::size_t>(chunk), 0);
#endif
        if (r <= 0) return false;
        sent += static_cast<std::size_t>(r);
    }
    return true;
}
std::string splitCmd(const std::string &line, std::vector<std::string> &parts) {
    parts.clear();
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else if (c == '\r' || c == '\n') {
            continue;
        } else {
            cur.push_back(c);
            if (cur.size() > kMaxVal) return "too_big";
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return "empty";
    return "";
}
std::string toUpper(std::string s) {
    for (auto &c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}
std::string handle(kvr::Store &st, const std::string &line) {
    std::vector<std::string> p;
    std::string err = splitCmd(line, p);
    if (!err.empty()) return "-ERR " + err + "\r\n";
    std::string cmd = toUpper(p[0]);
    if (cmd == "PING") return "+PONG\r\n";
    if (cmd == "GET") {
        if (p.size() != 2) return "-ERR wrong_number_of_arguments\r\n";
        std::string v;
        if (st.get(p[1], v)) return "$" + std::to_string(v.size()) + "\r\n" + v + "\r\n";
        return "$-1\r\n";
    }
    if (cmd == "SET" && (p.size() == 3 || p.size() == 5)) {
        std::uint64_t ttl = 0;
        if (p.size() == 5) {
            if (toUpper(p[3]) != "EX") return "-ERR syntax\r\n";
            try {
                long long s = std::stoll(p[4]);
                if (s <= 0 || s > 365 * 86400) return "-ERR bad_ttl\r\n";
                ttl = static_cast<std::uint64_t>(s) * 1000ULL;
            } catch (...) {
                return "-ERR bad_ttl\r\n";
            }
        }
        if (st.set(p[1], p[2], ttl)) return "+OK\r\n";
        return "-ERR bad_key\r\n";
    }
    if (cmd == "DEL") {
        if (p.size() != 2) return "-ERR wrong_number_of_arguments\r\n";
        return ":" + std::to_string(st.del(p[1])) + "\r\n";
    }
    if (cmd == "EXISTS") {
        if (p.size() != 2) return "-ERR wrong_number_of_arguments\r\n";
        return ":" + std::string(st.exists(p[1]) ? "1" : "0") + "\r\n";
    }
    if (cmd == "SIZE" && p.size() == 1) return ":" + std::to_string(st.size()) + "\r\n";
    if (cmd == "SET") return "-ERR wrong_number_of_arguments\r\n";
    if (cmd == "QUIT") return "+OK\r\n";
    return "-ERR unknown\r\n";
}
void serveOne(sock_t s, kvr::Store &st) {
    struct Guard {
        sock_t s_;
        ~Guard() { closeSock(s_); --gConns; }
    };
    Guard g{s};
#ifdef _WIN32
    DWORD tv = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    char buf[8192] = {};
    std::string line;
    while (true) {
        if (line.size() > kMaxLine) { sendAll(s, "-ERR too_big\r\n"); break; }
#ifdef _WIN32
        int r = recv(s, buf, sizeof(buf), 0);
#else
        ssize_t r = recv(s, buf, sizeof(buf), 0);
#endif
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; ++i) {
            char c = buf[i];
            if (c == '\n') {
                std::string reply = handle(st, line);
                line.clear();
                if (!sendAll(s, reply)) return;
            } else if (c != '\r') {
                line.push_back(c);
                if (line.size() > kMaxLine) { sendAll(s, "-ERR too_big\r\n"); return; }
            }
        }
    }
}
}
int main(int argc, char **argv) {
    int port = 6399;
    if (argc == 2) {
        try { port = std::stoi(argv[1]); } catch (...) { std::cerr << "bad_port\n"; return 1; }
    }
    if (port < 1 || port > 65535) { std::cerr << "bad_port\n"; return 1; }
#ifdef _WIN32
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return 1;
#endif
    sock_t ls =
#ifdef _WIN32
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
        socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (ls ==
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ) return 1;
    int one = 1;
#ifdef _WIN32
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in ad;
    std::memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = htonl(INADDR_ANY);
    ad.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(ls, reinterpret_cast<sockaddr*>(&ad), sizeof(ad)) != 0) { closeSock(ls); return 1; }
    if (listen(ls, 64) != 0) { closeSock(ls); return 1; }
    std::cout << "mini-redis listening port=" << port << "\n";
    kvr::Store st;
    while (true) {
        sock_t c = accept(ls, nullptr, nullptr);
        if (c ==
#ifdef _WIN32
            INVALID_SOCKET
#else
            -1
#endif
        ) continue;
        if (gConns.load() >= kMaxConns) { sendAll(c, "-ERR busy\r\n"); closeSock(c); continue; }
        ++gConns;
        std::thread(serveOne, c, std::ref(st)).detach();
    }
    return 0;
}
