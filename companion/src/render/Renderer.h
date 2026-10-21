#pragma once
#include <atomic>
class Renderer {
 std::atomic_bool visible_{};
public:
 static Renderer& get(){static Renderer r;return r;}
 bool isGuiOpen() const noexcept{return visible_.load(std::memory_order_acquire);}
 void setGuiOpen(bool value) noexcept{visible_.store(value,std::memory_order_release);}
};
