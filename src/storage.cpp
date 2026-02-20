#include "storage.h"

void Storage::append(const std::string& topic, uint64_t seq,
                     const std::string& serialized) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_[topic][seq] = serialized;
}

std::vector<std::string> Storage::fetch(const std::string& topic,
                                        uint64_t from, uint64_t to) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    auto it = store_.find(topic);
    if (it == store_.end()) return result;
    for (auto jt = it->second.lower_bound(from);
         jt != it->second.end() && jt->first <= to; ++jt) {
        result.push_back(jt->second);
    }
    return result;
}

uint64_t Storage::last_seq(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(topic);
    if (it == store_.end() || it->second.empty()) return 0;
    return it->second.rbegin()->first;
}
