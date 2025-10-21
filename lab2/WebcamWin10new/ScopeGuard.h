#pragma once
#include <functional>

class ScopeGuard {
    std::function<void()> fn_;
    bool active_{ true };
public:
    explicit ScopeGuard(std::function<void()> f) : fn_(std::move(f)) {}
    ~ScopeGuard() { if (active_) fn_(); }
    void dismiss() { active_ = false; }
};
