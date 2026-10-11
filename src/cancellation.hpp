#pragma once
#include <cstdint>
#include <atomic>
#include <functional>

namespace transcribe {
enum class CancellationReason : std::uint8_t { none, user, signal, connection_lost };
class CancellationToken {
public:
    void request(int signal = 2) noexcept;
    int signal() const noexcept;
    bool repeated() const noexcept { return repeated_.load(); }
    bool completed() const noexcept { return state_.load() == -1; }
    void commit();
    void reset() noexcept;
    void poll() { if (poll_ && !completed()) poll_(); }
    void set_poll(std::function<void()> poll) { poll_ = std::move(poll); }
    CancellationReason reason() const noexcept;
private:
    // One atomic transition arbitrates completion against signals/control EOF.
    std::atomic<int> state_{0};
    std::atomic<bool> repeated_{false};
    std::function<void()> poll_;
};
CancellationToken& cancellation_token();
}
