#include "deduplicator.h"

bool Deduplicator::is_duplicate_and_mark(const std::string& uuid16) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = seen_.insert(uuid16);
    // inserted == true means it was NOT previously seen (not a duplicate).
    return !inserted;
}
