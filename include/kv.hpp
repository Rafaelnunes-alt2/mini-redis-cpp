// feito por: maquinzz
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
namespace kvr {
struct Entry {
    std::string val;
    std::uint64_t expireMs = 0;
};
class Store {
public:
    bool set(const std::string &k, const std::string &v, std::uint64_t ttlMs);
    bool get(const std::string &k, std::string &out);
    long del(const std::string &k);
    bool exists(const std::string &k);
    int size();
private:
    bool live(const Entry &e);
    std::mutex m_;
    std::unordered_map<std::string, Entry> data_;
};
std::uint64_t nowMs();
}
