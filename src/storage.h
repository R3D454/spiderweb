#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>

class Storage {
public:
    // Append a serialized envelope for (topic, seq).
    void append(const std::string& topic, uint64_t seq,
                const std::string& serialized);

    // Return all serialized envelopes for topic in [from, to] inclusive.
    std::vector<std::string> fetch(const std::string& topic,
                                   uint64_t from, uint64_t to) const;

    // Return the highest seq seen for topic, or 0 if none.
    uint64_t last_seq(const std::string& topic) const;

private:
    mutable std::mutex mutex_;
    // topic -> seq -> serialized envelope
    std::map<std::string, std::map<uint64_t, std::string>> store_;
};
