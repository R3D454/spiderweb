#pragma once

#include <string>
#include <unordered_set>
#include <mutex>

class Deduplicator {
public:
    // Returns true if uuid16 has already been seen (is a duplicate) and marks
    // it as seen. Returns false the first time a uuid16 is encountered.
    bool is_duplicate_and_mark(const std::string& uuid16);

private:
    std::mutex              mutex_;
    std::unordered_set<std::string> seen_;
};
