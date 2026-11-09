#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

// C callbacks carry monotonic tickets, never addresses of objects whose lifetime
// is controlled by Java. A callback that acquired a shared owner can finish;
// callbacks arriving after removal find no target. IDs are never reused.
template<class T> class CallbackRegistry {
public:
    uintptr_t add(std::shared_ptr<T> value) {
        std::lock_guard guard(mutex_);
        const auto id = ++next_;
        values_.emplace(id, std::move(value));
        return id;
    }
    std::shared_ptr<T> get(uintptr_t id) {
        std::lock_guard guard(mutex_);
        const auto found = values_.find(id);
        return found == values_.end() ? nullptr : found->second;
    }
    std::shared_ptr<T> take(uintptr_t id) {
        std::lock_guard guard(mutex_);
        const auto found = values_.find(id);
        if (found == values_.end()) return nullptr;
        auto value = std::move(found->second);
        values_.erase(found);
        return value;
    }
    // Native event threads may outlive JNI library static destructors.
    static CallbackRegistry& instance() {
        static auto* registry = new CallbackRegistry;
        return *registry;
    }
private:
    std::mutex mutex_;
    uintptr_t next_ = 0;
    std::unordered_map<uintptr_t, std::shared_ptr<T>> values_;
};
