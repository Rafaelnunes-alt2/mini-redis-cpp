// feito por: maquinzz
#include "kv.hpp"
#include <chrono>
namespace kvr {
std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
bool Store::live(const Entry &e) {
    if (e.expireMs == 0) return true;
    return nowMs() < e.expireMs;
}
bool Store::set(const std::string &k, const std::string &v, std::uint64_t ttlMs) {
    if (k.empty() || k.size() > 512 || v.size() > 1024 * 1024) return false;
    Entry e;
    e.val = v;
    e.expireMs = ttlMs > 0 ? nowMs() + ttlMs : 0;
    std::lock_guard<std::mutex> lk(m_);
    data_[k] = e;
    return true;
}
bool Store::get(const std::string &k, std::string &out) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = data_.find(k);
    if (it == data_.end()) return false;
    if (!live(it->second)) {
        data_.erase(it);
        return false;
    }
    out = it->second.val;
    return true;
}
long Store::del(const std::string &k) {
    std::lock_guard<std::mutex> lk(m_);
    return static_cast<long>(data_.erase(k));
}
bool Store::exists(const std::string &k) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = data_.find(k);
    if (it == data_.end()) return false;
    if (!live(it->second)) {
        data_.erase(it);
        return false;
    }
    return true;
}
int Store::size() {
    std::lock_guard<std::mutex> lk(m_);
    std::uint64_t t = nowMs();
    for (auto it = data_.begin(); it != data_.end(); ) {
        if (it->second.expireMs != 0 && t >= it->second.expireMs) it = data_.erase(it);
        else ++it;
    }
    return static_cast<int>(data_.size());
}
}
